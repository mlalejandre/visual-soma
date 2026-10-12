#include <stdint.h>

#include "console.h"
#include "dns.h"
#include "net.h"
#include "udp.h"


#define DNS_PORT          53
#define DNS_LOCAL_PORT    40053
#define DNS_HEADER_LEN    12
#define DNS_TYPE_A        1
#define DNS_CLASS_IN      1


uint8_t net_dns[4] = { 10, 0, 2, 3 };

static uint16_t dns_next_id = 0x1000;


static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}


static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}


/* Devuelve el offset posterior al nombre, o -1 si es invalido. */
static int dns_skip_name(const uint8_t *m, int len, int off)
{
    while (off < len) {

        uint8_t l = m[off];

        if (l == 0) {
            return off + 1;
        }

        if ((l & 0xC0) == 0xC0) {
            return (off + 1 < len) ? off + 2 : -1;
        }

        if (l & 0xC0) {
            return -1;
        }

        off += 1 + l;
    }

    return -1;
}


static int dns_build_query(const char *name, uint16_t id, uint8_t *q, int cap)
{
    int n = 0;

    wr16(q + 0, id);
    wr16(q + 2, 0x0100);        /* recursion deseada */
    wr16(q + 4, 1);             /* QDCOUNT */
    wr16(q + 6, 0);
    wr16(q + 8, 0);
    wr16(q + 10, 0);

    n = DNS_HEADER_LEN;

    int label_pos = n++;
    int count = 0;

    for (const char *p = name; ; ++p) {

        if (*p == '.' || *p == '\0') {

            if (count == 0) {
                return -1;
            }

            q[label_pos] = (uint8_t)count;

            if (*p == '\0') {
                break;
            }

            if (n >= cap - 6) {
                return -1;
            }

            label_pos = n++;
            count = 0;

        } else {

            if (count >= 63 || n >= cap - 6) {
                return -1;
            }

            q[n++] = (uint8_t)*p;
            ++count;
        }
    }

    q[n++] = 0;
    wr16(q + n, DNS_TYPE_A);
    n += 2;
    wr16(q + n, DNS_CLASS_IN);
    n += 2;

    return n;
}


static int dns_resolve_unlocked(const char *name, uint8_t *ip_out, uint32_t timeout_ms)
{
    if (udp_bind(DNS_LOCAL_PORT) != 0) {
        kprint("DNS: cannot bind local port\n");
        return 0;
    }

    uint32_t per_try = timeout_ms / 3;
    if (per_try < 1000) {
        per_try = 1000;
    }

    int ok = 0;
    uint8_t query[256];
    uint8_t resp[512];
    uint8_t from_ip[4];
    uint16_t from_port;

    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        uint16_t id = dns_next_id++;
        int qlen = dns_build_query(name, id, query, sizeof(query));

        if (qlen < 0) {
            kprint("DNS: invalid name\n");
            udp_unbind(DNS_LOCAL_PORT);
            return 0;
        }

        if (udp_sendto(net_dns, DNS_PORT, DNS_LOCAL_PORT, query,
                       (uint16_t)qlen) != 0)
        {
            kprint("DNS: send failed\n");
            continue;
        }

        for (int tries = 0; tries < 4 && !ok; ++tries) {

            int len = udp_recvfrom(DNS_LOCAL_PORT, resp, sizeof(resp),
                                   from_ip, &from_port, per_try);

            if (len <= 0) {
                break;
            }

            if (len < DNS_HEADER_LEN || rd16(resp) != id) {
                continue;       /* respuesta ajena: seguir esperando */
            }

            uint16_t flags = rd16(resp + 2);
            uint16_t rcode = flags & 0x000F;

            if (!(flags & 0x8000) || rcode != 0) {
                kprint("DNS: server error, rcode=");
                kprint_dec(rcode);
                kprint("\n");
                udp_unbind(DNS_LOCAL_PORT);
                return 0;
            }

            int qd = rd16(resp + 4);
            int an = rd16(resp + 6);
            int off = DNS_HEADER_LEN;

            for (int i = 0; i < qd; ++i) {

                off = dns_skip_name(resp, len, off);

                if (off < 0) break;

                off += 4;
            }

            for (int i = 0; i < an && off > 0; ++i) {

                off = dns_skip_name(resp, len, off);

                if (off < 0 || off + 10 > len) break;

                uint16_t type  = rd16(resp + off);
                uint16_t cls   = rd16(resp + off + 2);
                uint16_t rdlen = rd16(resp + off + 8);

                off += 10;

                if (off + rdlen > len) break;

                if (type == DNS_TYPE_A && cls == DNS_CLASS_IN && rdlen == 4) {

                    for (int k = 0; k < 4; ++k) {
                        ip_out[k] = resp[off + k];
                    }

                    ok = 1;
                    break;
                }

                off += rdlen;
            }

            if (!ok) {
                kprint("DNS: no A record in response\n");
                udp_unbind(DNS_LOCAL_PORT);
                return 0;
            }
        }
    }

    if (!ok) {
        kprint("DNS: timeout\n");
    }

    udp_unbind(DNS_LOCAL_PORT);

    return ok;
}


void net_run_dns_test(void)
{
    kprint("\nNET: UDP + DNS TEST\n");
    kprint("-------------------\n");

    if (!net_init()) {
        kprint("NET: init FAILED\n");
        return;
    }

    static const char *names[] = { "example.com", "anthropic.com" };

    int resolved = 0;

    for (int i = 0; i < 2; ++i) {

        uint8_t ip[4];

        kprint("\nDNS ");
        kprint(names[i]);
        kprint(" via ");
        kprint_ip(net_dns);
        kprint(": ");

        if (dns_resolve(names[i], ip, 3000)) {
            kprint_ip(ip);
            kprint("\n");
            ++resolved;
        } else {
            kprint("FAILED\n");
        }
    }

    kprint("\n");

    if (resolved > 0) {
        kprint("NET: UDP layer OK\n");
    } else {
        kprint("NET: UDP layer FAILED\n");
    }
}


int dns_resolve(const char *name, uint8_t *ip_out, uint32_t timeout_ms)
{
    net_lock();
    int r = dns_resolve_unlocked(name, ip_out, timeout_ms);
    net_unlock();
    return r;
}
