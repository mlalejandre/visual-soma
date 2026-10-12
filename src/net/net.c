#include "arch.h"
#include <stdint.h>
#include "console.h"
#include "io.h"
#include "mem.h"
#include "net.h"
#include "ip.h"
#include "virtio_net.h"
#include "thread.h"
#include "mutex.h"

#define ETH_HLEN        14
#define ETHERTYPE_ARP   0x0806
#define ETHERTYPE_IPV4  0x0800

#define ARP_LEN         28
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2
#define ARP_CACHE_SIZE  8

extern uint64_t tsc_ticks_per_ms;
#define NET_TSC_PER_MS  tsc_ticks_per_ms

uint8_t net_mac[6];
uint8_t net_ip[4]      = { 10, 0, 2, 15 };
uint8_t net_gateway[4] = { 10, 0, 2, 2 };

struct arp_entry {
    uint8_t ip[4];
    uint8_t mac[6];
    uint8_t valid;
    uint8_t pad;
} __attribute__((aligned(4)));

static struct arp_entry arp_cache[ARP_CACHE_SIZE] __attribute__((aligned(8)));
static uint8_t arp_next;

static uint8_t rx_frame[2048] __attribute__((aligned(16)));
static volatile int net_rx_depth;
static struct kmutex net_big_lock;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static int arp_lookup(const uint8_t *ip, uint8_t *mac_out)
{
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0) {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

static int arp_learn(const uint8_t *ip, const uint8_t *mac)
{
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0) {
            if (memcmp(arp_cache[i].mac, mac, 6) == 0) return 0;
            memcpy(arp_cache[i].mac, mac, 6);
            return 1;
        }
    }
    struct arp_entry *e = &arp_cache[arp_next % ARP_CACHE_SIZE];
    arp_next++;
    memcpy(e->ip, ip, 4);
    memcpy(e->mac, mac, 6);
    e->valid = 1;
    return 1;
}

static void append_ch(char *dst, uint32_t max, uint32_t *pos, char c)
{
    if (*pos + 1 < max) { dst[(*pos)++] = c; dst[*pos] = '\0'; }
}

static void append_str_arp(char *dst, uint32_t max, uint32_t *pos, const char *s)
{
    while (*s && *pos + 1 < max) dst[(*pos)++] = *s++;
    dst[*pos] = '\0';
}

static void append_dec_u8(char *dst, uint32_t max, uint32_t *pos, uint8_t v)
{
    char tmp[4]; int n = 0;
    if (v == 0) { append_ch(dst, max, pos, '0'); return; }
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) append_ch(dst, max, pos, tmp[--n]);
}

static void append_hex_nibble(char *dst, uint32_t max, uint32_t *pos, uint8_t n)
{
    n &= 0x0F;
    append_ch(dst, max, pos, n < 10 ? (char)('0' + n) : (char)('A' + n - 10));
}

static void append_hex_u8(char *dst, uint32_t max, uint32_t *pos, uint8_t v)
{
    append_hex_nibble(dst, max, pos, v >> 4);
    append_hex_nibble(dst, max, pos, v);
}

int arp_format_cache(char *out, uint32_t max)
{
    uint32_t pos = 0;
    append_str_arp(out, max, &pos, "Cache ARP en vivo:\n");
    int count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (!arp_cache[i].valid) continue;
        count++;
        append_str_arp(out, max, &pos, "  ");
        for (int k = 0; k < 4; ++k) {
            append_dec_u8(out, max, &pos, arp_cache[i].ip[k]);
            if (k != 3) append_ch(out, max, &pos, '.');
        }
        append_str_arp(out, max, &pos, " -> ");
        for (int k = 0; k < 6; ++k) {
            append_hex_u8(out, max, &pos, arp_cache[i].mac[k]);
            if (k != 5) append_ch(out, max, &pos, ':');
        }
        append_ch(out, max, &pos, '\n');
    }
    if (count == 0) append_str_arp(out, max, &pos, "  (vacia)\n");
    return (int)pos;
}

static void arp_send(uint16_t op, const uint8_t *eth_dst, const uint8_t *target_mac, const uint8_t *target_ip)
{
    uint8_t f[ETH_HLEN + ARP_LEN + 18];
    memset(f, 0, sizeof(f));
    memcpy(f, eth_dst, 6);
    memcpy(f + 6, net_mac, 6);
    wr16(f + 12, ETHERTYPE_ARP);
    uint8_t *a = f + ETH_HLEN;
    wr16(a, 1);
    wr16(a + 2, ETHERTYPE_IPV4);
    a[4] = 6; a[5] = 4;
    wr16(a + 6, op);
    memcpy(a + 8,  net_mac, 6);
    memcpy(a + 14, net_ip, 4);
    memcpy(a + 18, target_mac, 6);
    memcpy(a + 24, target_ip, 4);
    virtio_net_send(f, sizeof(f));
}

static void handle_arp(const uint8_t *a, uint16_t len)
{
    if (len < ARP_LEN) return;
    if (rd16(a) != 1 || rd16(a + 2) != ETHERTYPE_IPV4 || a[4] != 6 || a[5] != 4) return;
    uint16_t op = rd16(a + 6);
    const uint8_t *sha = a + 8;
    const uint8_t *spa = a + 14;
    const uint8_t *tpa = a + 24;
    static const uint8_t zero_ip[4] = { 0, 0, 0, 0 };
    if (memcmp(spa, zero_ip, 4) != 0) arp_learn(spa, sha);
    if (op == ARP_OP_REQUEST && memcmp(tpa, net_ip, 4) == 0) {
        arp_send(ARP_OP_REPLY, sha, sha, spa);
    }
}

static void handle_frame(const uint8_t *f, uint16_t len)
{
    if (len < ETH_HLEN) return;
    uint16_t type = rd16(f + 12);
    if (type == ETHERTYPE_ARP) handle_arp(f + ETH_HLEN, (uint16_t)(len - ETH_HLEN));
    if (type == ETHERTYPE_IPV4) ip_input(f, len);
}

int net_init(void)
{
    /* PARCHE 047: idempotente. Re-ejecutar virtio_net_init() re-inicializa las colas
     * y deja el dispositivo en FAILED. */
    static int net_inited = 0;
    if (net_inited) return 1;
    if (!virtio_net_init()) return 0;
    net_inited = 1;
    memcpy(net_mac, virtio_net_mac(), 6);
    kprint("\nNET: MAC "); kprint_mac(net_mac);
    kprint("  IP "); kprint_ip(net_ip);
    kprint("  GW "); kprint_ip(net_gateway); kprint("\n");
    return 1;
}

int net_poll(void)
{
    uint16_t len = 0;
    if (!virtio_net_poll(rx_frame, sizeof(rx_frame), &len)) return 0;
    net_rx_depth++;
    handle_frame(rx_frame, len);
    net_rx_depth--;
    return 1;
}

void net_wait_step(void)
{
    net_lock();
    net_poll();
    net_unlock();
    if (net_rx_depth == 0) thread_yield();
}

static int net_lock_ready;
void net_lock(void)
{
    if (!net_lock_ready) { kmutex_init(&net_big_lock, "net_lock"); net_lock_ready = 1; }
    kmutex_lock(&net_big_lock);
}
void net_unlock(void) { kmutex_unlock(&net_big_lock); }

int arp_resolve(const uint8_t *ip, uint8_t *mac_out, uint32_t timeout_ms)
{
    static const uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const uint8_t zero_mac[6]  = { 0, 0, 0, 0, 0, 0 };
    if (arp_lookup(ip, mac_out)) return 1;

    const int attempts = 3;
    uint64_t per_attempt = (uint64_t)timeout_ms * (NET_TSC_PER_MS > 0 ? NET_TSC_PER_MS : 62500ULL) / attempts;

    for (int n = 0; n < attempts; ++n) {
        arp_send(ARP_OP_REQUEST, broadcast, zero_mac, ip);
        uint64_t start = arch_cycle_counter();
        while ((arch_cycle_counter() - start) < per_attempt) {
            net_wait_step();
            if (arp_lookup(ip, mac_out)) return 1;
            arch_pause();
        }
    }
    return 0;
}
