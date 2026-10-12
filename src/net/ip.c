#include <stdint.h>

#include "console.h"
#include "io.h"
#include "ip.h"
#include "mem.h"
#include "net.h"
#include "udp.h"
#include "tcp.h"
#include "virtio_net.h"


#define ETH_HLEN          14
#define IP_HLEN           20
#define IP_MTU            1500
#define ETHERTYPE_IPV4    0x0800

#define IP_PROTO_ICMP     1

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8
#define ICMP_HLEN         8

#define PING_ID           0x534F
#define PING_DATA_LEN     32

/* Aproximacion bajo QEMU/TCG: ~1 tick de TSC por ns. */
extern uint64_t tsc_ticks_per_ms;
#define TSC_PER_MS        tsc_ticks_per_ms
#define TSC_PER_US        (tsc_ticks_per_ms / 1000ULL > 0 ? tsc_ticks_per_ms / 1000ULL : 1ULL)


static const uint8_t net_netmask[4] = { 255, 255, 255, 0 };

static uint16_t ip_ident;

static volatile int      ping_got;
static volatile uint16_t ping_seq;
static volatile uint8_t  ping_ttl;
static volatile uint16_t ping_len;
static uint8_t           ping_from[4];


static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}


static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}


uint16_t ip_checksum(const void *data, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += (uint32_t)(((uint32_t)p[0] << 8) | p[1]);
        p += 2;
        len = (uint16_t)(len - 2);
    }

    if (len == 1) {
        sum += (uint32_t)p[0] << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}


/* Construye y envia un datagrama IPv4 a una MAC ya conocida. */
static int ip_send_to_mac(
    const uint8_t *dst_mac,
    const uint8_t *dst_ip,
    uint8_t proto,
    const void *payload,
    uint16_t plen
)
{
    if (plen > (IP_MTU - IP_HLEN)) {
        return -1;
    }

    uint8_t f[ETH_HLEN + IP_MTU];

    memcpy(f, dst_mac, 6);
    memcpy(f + 6, net_mac, 6);
    wr16(f + 12, ETHERTYPE_IPV4);

    uint8_t *ip = f + ETH_HLEN;

    ip[0] = 0x45;                           /* IPv4, IHL = 5 */
    ip[1] = 0;                              /* TOS */
    wr16(ip + 2, (uint16_t)(IP_HLEN + plen));
    wr16(ip + 4, ip_ident++);
    wr16(ip + 6, 0x4000);                   /* Don't Fragment */
    ip[8] = 64;                             /* TTL */
    ip[9] = proto;
    wr16(ip + 10, 0);
    memcpy(ip + 12, net_ip, 4);
    memcpy(ip + 16, dst_ip, 4);

    wr16(ip + 10, ip_checksum(ip, IP_HLEN));

    memcpy(ip + IP_HLEN, payload, plen);

    return virtio_net_send(f, (uint16_t)(ETH_HLEN + IP_HLEN + plen));
}


static int is_on_link(const uint8_t *ip)
{
    for (int i = 0; i < 4; ++i) {

        if ((ip[i] & net_netmask[i]) != (net_ip[i] & net_netmask[i])) {
            return 0;
        }
    }

    return 1;
}


int ip_send(
    const uint8_t *dst_ip,
    uint8_t proto,
    const void *payload,
    uint16_t len
)
{
    const uint8_t *next_hop = is_on_link(dst_ip) ? dst_ip : net_gateway;

    uint8_t mac[6];

    if (!arp_resolve(next_hop, mac, 1500)) {
        kprint("IP: ARP failed for next hop ");
        kprint_ip(next_hop);
        kprint("\n");
        return -1;
    }

    return ip_send_to_mac(mac, dst_ip, proto, payload, len);
}


/* ---------------------------------------------------------------- */
/* ICMP                                                             */
/* ---------------------------------------------------------------- */

static void icmp_input(
    const uint8_t *src_mac,
    const uint8_t *src_ip,
    uint8_t ttl,
    const uint8_t *icmp,
    uint16_t len
)
{
    if (len < ICMP_HLEN) {
        return;
    }

    if (ip_checksum(icmp, len) != 0) {
        kprint("ICMP: bad checksum, dropped\n");
        return;
    }

    uint8_t type = icmp[0];

    if (type == ICMP_ECHO_REQUEST) {

        uint8_t reply[IP_MTU - IP_HLEN];

        if (len > sizeof(reply)) {
            return;
        }

        memcpy(reply, icmp, len);

        reply[0] = ICMP_ECHO_REPLY;
        wr16(reply + 2, 0);
        wr16(reply + 2, ip_checksum(reply, len));

        kprint("ICMP: echo request from ");
        kprint_ip(src_ip);
        kprint(", replying\n");

        ip_send_to_mac(src_mac, src_ip, IP_PROTO_ICMP, reply, len);

        return;
    }

    if (type == ICMP_ECHO_REPLY && rd16(icmp + 4) == PING_ID) {

        memcpy(ping_from, src_ip, 4);
        ping_seq = rd16(icmp + 6);
        ping_ttl = ttl;
        ping_len = len;
        ping_got = 1;
    }
}


/* ---------------------------------------------------------------- */
/* IPv4                                                             */
/* ---------------------------------------------------------------- */

void ip_input(const uint8_t *frame, uint16_t len)
{
    if (len < ETH_HLEN + IP_HLEN) {
        return;
    }

    const uint8_t *ip = frame + ETH_HLEN;

    if ((ip[0] >> 4) != 4) {
        return;
    }

    uint16_t ihl = (uint16_t)((ip[0] & 0x0F) * 4);

    if (ihl < IP_HLEN) {
        return;
    }

    uint16_t total = rd16(ip + 2);

    if (total < ihl || total > (uint16_t)(len - ETH_HLEN)) {
        return;
    }

    if (ip_checksum(ip, ihl) != 0) {
        return;
    }

    /* Sin soporte de fragmentacion: descartar MF u offset != 0. */
    if ((rd16(ip + 6) & 0x3FFF) != 0) {
        return;
    }

    if (memcmp(ip + 16, net_ip, 4) != 0) {
        return;
    }

    if (ip[9] == IP_PROTO_ICMP) {
        icmp_input(frame + 6, ip + 12, ip[8], ip + ihl,
                   (uint16_t)(total - ihl));
    }

    if (ip[9] == 17) {
        udp_input(ip + 12, ip + 16, ip + ihl,
                  (uint16_t)(total - ihl));
    }

    if (ip[9] == 6) {
        tcp_input(ip + 12, ip + 16, ip + ihl,
                  (uint16_t)(total - ihl));
    }
}


/* ---------------------------------------------------------------- */
/* Ping                                                             */
/* ---------------------------------------------------------------- */

static int icmp_ping_unlocked(const uint8_t *dst_ip, uint16_t seq, uint32_t timeout_ms)
{
    uint8_t req[ICMP_HLEN + PING_DATA_LEN];

    req[0] = ICMP_ECHO_REQUEST;
    req[1] = 0;
    wr16(req + 2, 0);
    wr16(req + 4, PING_ID);
    wr16(req + 6, seq);

    for (int i = 0; i < PING_DATA_LEN; ++i) {
        req[ICMP_HLEN + i] = (uint8_t)('a' + (i % 26));
    }

    wr16(req + 2, ip_checksum(req, sizeof(req)));

    ping_got = 0;

    if (ip_send(dst_ip, IP_PROTO_ICMP, req, sizeof(req)) != 0) {
        return -1;
    }

    uint64_t start = rdtsc();
    uint64_t limit = (uint64_t)timeout_ms * TSC_PER_MS;

    while ((rdtsc() - start) < limit) {

        net_wait_step();

        if (ping_got && ping_seq == seq) {

            uint64_t us = (rdtsc() - start) / TSC_PER_US;

            kprint("Reply from ");
            kprint_ip(ping_from);
            kprint(": bytes=");
            kprint_dec((uint32_t)(ping_len - ICMP_HLEN));
            kprint(" ttl=");
            kprint_dec(ping_ttl);
            kprint(" seq=");
            kprint_dec(seq);
            kprint(" time~");
            kprint_dec((uint32_t)us);
            kprint("us\n");

            return 1;
        }

        cpu_pause();
    }

    return 0;
}


void net_run_ping_test(void)
{
    kprint("\nNET: IPv4 + ICMP TEST\n");
    kprint("---------------------\n");

    if (!net_init()) {
        kprint("NET: init FAILED\n");
        return;
    }

    kprint("\nPING gateway ");
    kprint_ip(net_gateway);
    kprint("\n");

    const int count = 4;
    int received = 0;

    for (int seq = 1; seq <= count; ++seq) {

        int r = icmp_ping(net_gateway, (uint16_t)seq, 1000);

        if (r == 1) {
            ++received;
        } else if (r == 0) {
            kprint("Request timed out: seq=");
            kprint_dec((uint32_t)seq);
            kprint("\n");
        } else {
            kprint("Send failed: seq=");
            kprint_dec((uint32_t)seq);
            kprint("\n");
        }
    }

    kprint("Gateway: sent=");
    kprint_dec((uint32_t)count);
    kprint(" received=");
    kprint_dec((uint32_t)received);
    kprint("\n");

    /*
     * Informativo: el servidor LLM esta fuera de la red SLIRP. Si SLIRP
     * puede emitir ICMP hacia el host responde; si no, es TIMEOUT y
     * no indica fallo de MYOS (TCP si atravesara el NAT).
     */
    static const uint8_t llm[4] = { 192, 168, 1, 200 };

    kprint("\nPING LLM server ");
    kprint_ip(llm);
    kprint(" (informational)\n");

    int r = icmp_ping(llm, 1, 3000);

    if (r != 1) {
        kprint("No ICMP reply (normal con NAT de usuario)\n");
    }

    kprint("\n");

    if (received == count) {
        kprint("NET: IPv4 + ICMP layer OK\n");
    } else {
        kprint("NET: IPv4 + ICMP layer FAILED\n");
    }
}


int icmp_ping(const uint8_t *dst_ip, uint16_t seq, uint32_t timeout_ms)
{
    net_lock();
    int r = icmp_ping_unlocked(dst_ip, seq, timeout_ms);
    net_unlock();
    return r;
}
