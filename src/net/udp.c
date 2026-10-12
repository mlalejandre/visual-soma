#include <stdint.h>

#include "io.h"
#include "ip.h"
#include "mem.h"
#include "net.h"
#include "udp.h"


#define IP_PROTO_UDP      17
#define UDP_HLEN          8
#define UDP_MAX_PAYLOAD   1472

#define UDP_MAX_SOCKETS   4
#define UDP_RX_BUFFER     1024

/* Aproximacion bajo QEMU/TCG: ~1 tick de TSC por ns. */
extern uint64_t tsc_ticks_per_ms;
#define TSC_PER_MS        tsc_ticks_per_ms


struct udp_socket {
    uint8_t           used;
    uint16_t          port;
    volatile uint8_t  pending;
    uint8_t           src_ip[4];
    uint16_t          src_port;
    uint16_t          len;
    uint8_t           data[UDP_RX_BUFFER];
};

static struct udp_socket sockets[UDP_MAX_SOCKETS];


static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}


static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}


static uint32_t csum_add(uint32_t sum, const uint8_t *p, uint16_t len)
{
    while (len > 1) {
        sum += (uint32_t)(((uint32_t)p[0] << 8) | p[1]);
        p += 2;
        len = (uint16_t)(len - 2);
    }

    if (len == 1) {
        sum += (uint32_t)p[0] << 8;
    }

    return sum;
}


/*
 * Checksum UDP con pseudo-cabecera. Si el campo checksum del datagrama
 * esta a cero devuelve el valor a escribir; si contiene el checksum
 * recibido devuelve 0 cuando es valido.
 */
static uint16_t udp_checksum(
    const uint8_t *src_ip,
    const uint8_t *dst_ip,
    const uint8_t *udp,
    uint16_t udp_len
)
{
    uint8_t ph[12];

    memcpy(ph, src_ip, 4);
    memcpy(ph + 4, dst_ip, 4);
    ph[8] = 0;
    ph[9] = IP_PROTO_UDP;
    wr16(ph + 10, udp_len);

    uint32_t sum = csum_add(0, ph, sizeof(ph));
    sum = csum_add(sum, udp, udp_len);

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}


static struct udp_socket *find_socket(uint16_t port)
{
    for (int i = 0; i < UDP_MAX_SOCKETS; ++i) {

        if (sockets[i].used && sockets[i].port == port) {
            return &sockets[i];
        }
    }

    return 0;
}


int udp_bind(uint16_t port)
{
    if (port == 0 || find_socket(port)) {
        return -1;
    }

    for (int i = 0; i < UDP_MAX_SOCKETS; ++i) {

        if (!sockets[i].used) {
            sockets[i].used = 1;
            sockets[i].port = port;
            sockets[i].pending = 0;
            return 0;
        }
    }

    return -1;
}


void udp_unbind(uint16_t port)
{
    struct udp_socket *s = find_socket(port);

    if (s) {
        s->used = 0;
        s->pending = 0;
    }
}


int udp_sendto(
    const uint8_t *dst_ip,
    uint16_t dst_port,
    uint16_t src_port,
    const void *data,
    uint16_t len
)
{
    if (len > UDP_MAX_PAYLOAD) {
        return -1;
    }

    uint8_t pkt[UDP_HLEN + UDP_MAX_PAYLOAD];
    uint16_t total = (uint16_t)(UDP_HLEN + len);

    wr16(pkt, src_port);
    wr16(pkt + 2, dst_port);
    wr16(pkt + 4, total);
    wr16(pkt + 6, 0);

    memcpy(pkt + UDP_HLEN, data, len);

    uint16_t ck = udp_checksum(net_ip, dst_ip, pkt, total);

    if (ck == 0) {
        ck = 0xFFFF;
    }

    wr16(pkt + 6, ck);

    return ip_send(dst_ip, IP_PROTO_UDP, pkt, total);
}


void udp_input(
    const uint8_t *src_ip,
    const uint8_t *dst_ip,
    const uint8_t *udp,
    uint16_t len
)
{
    if (len < UDP_HLEN) {
        return;
    }

    uint16_t ulen = rd16(udp + 4);

    if (ulen < UDP_HLEN || ulen > len) {
        return;
    }

    /* Checksum 0 = el emisor no lo calculo (permitido en IPv4). */
    if (rd16(udp + 6) != 0 && udp_checksum(src_ip, dst_ip, udp, ulen) != 0) {
        return;
    }

    struct udp_socket *s = find_socket(rd16(udp + 2));

    if (!s || s->pending) {
        return;
    }

    uint16_t n = (uint16_t)(ulen - UDP_HLEN);

    if (n > UDP_RX_BUFFER) {
        n = UDP_RX_BUFFER;
    }

    memcpy(s->data, udp + UDP_HLEN, n);
    memcpy(s->src_ip, src_ip, 4);

    s->src_port = rd16(udp);
    s->len = n;
    s->pending = 1;
}


int udp_recvfrom(
    uint16_t port,
    void *buf,
    uint16_t max,
    uint8_t *src_ip,
    uint16_t *src_port,
    uint32_t timeout_ms
)
{
    struct udp_socket *s = find_socket(port);

    if (!s) {
        return -1;
    }

    uint64_t start = rdtsc();
    uint64_t limit = (uint64_t)timeout_ms * TSC_PER_MS;

    for (;;) {

        if (s->pending) {

            uint16_t n = s->len < max ? s->len : max;

            memcpy(buf, s->data, n);

            if (src_ip) {
                memcpy(src_ip, s->src_ip, 4);
            }

            if (src_port) {
                *src_port = s->src_port;
            }

            s->pending = 0;

            return n > 0 ? n : 1;
        }

        if ((rdtsc() - start) >= limit) {
            return 0;
        }

        net_wait_step();

        cpu_pause();
    }
}
