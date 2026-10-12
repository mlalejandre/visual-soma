#include <stdint.h>
#include "console.h"
#include "io.h"
#include "pci.h"
#include "virtio_net.h"
#include "arch.h"
#include "net.h"

#define VIRTIO_VENDOR_ID       0x1AF4
#define VIRTIO_NET_LEGACY_ID   0x1000

#define REG_DEVICE_FEATURES    0x00
#define REG_DRIVER_FEATURES    0x04
#define REG_QUEUE_ADDRESS      0x08
#define REG_QUEUE_SIZE         0x0C
#define REG_QUEUE_SELECT       0x0E
#define REG_QUEUE_NOTIFY       0x10
#define REG_STATUS             0x12
#define REG_DEVICE_CONFIG      0x14

#define ST_ACKNOWLEDGE         0x01
#define ST_DRIVER              0x02
#define ST_DRIVER_OK           0x04
#define ST_FAILED              0x80

#define DESC_F_WRITE           2
#define PAGE_SIZE              4096
#define QUEUE_MEM_BYTES        12288
#define QUEUE_MAX_SIZE         256
#define NET_HDR_SIZE           10
#define RX_BUFFER_COUNT        8
#define RX_BUFFER_SIZE         2048
#define TX_BUFFER_SIZE         2048
#define ETH_MIN_FRAME          60
#define TX_TIMEOUT_TICKS       1000000000ULL

#ifdef __aarch64__
#define VIRTIO_MMIO_MAGIC_VALUE     0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c
#define VIRTIO_MMIO_QUEUE_PFN       0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_CONFIG          0x100

#define VIRTIO_MMIO_BASE_START      0x0a000000ULL
#define VIRTIO_MMIO_STRIDE          0x200
#define VIRTIO_MMIO_MAX_DEVICES     32
#define VIRTIO_DEV_ID_NET           1
#endif

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

_Static_assert(sizeof(struct virtq_desc) == 16, "virtq_desc must be 16 bytes");

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
};

struct vq {
    volatile struct virtq_desc  *desc;
    volatile struct virtq_avail *avail;
    volatile struct virtq_used  *used;
    uint32_t size;
    uint32_t avail_idx;
    uint32_t last_used;
    uint32_t pad;
} __attribute__((aligned(16)));

#ifdef __x86_64__
__attribute__((section(".virtio_queue"), aligned(PAGE_SIZE)))
static uint8_t virtq0_memory[QUEUE_MEM_BYTES];
#else
static uint8_t virtq0_memory[QUEUE_MEM_BYTES] __attribute__((aligned(PAGE_SIZE)));
#endif
static uint8_t virtq1_memory[QUEUE_MEM_BYTES] __attribute__((aligned(PAGE_SIZE)));

static uint8_t rx_buffers[RX_BUFFER_COUNT][RX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffer[TX_BUFFER_SIZE] __attribute__((aligned(16)));

#ifndef __aarch64__
static uint16_t io_base;
#endif
#ifdef __aarch64__
static uintptr_t mmio_base = 0;
#endif
static int      ready;
static uint8_t  mac_addr[6];
static struct vq rxq __attribute__((aligned(16)));
static struct vq txq __attribute__((aligned(16)));

static uint32_t stat_tx_packets;
static uint32_t stat_rx_packets;
static uint64_t stat_tx_bytes;
static uint64_t stat_rx_bytes;

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

#ifdef __aarch64__
static inline uint32_t mmio_read(uint32_t reg)
{
    volatile uint32_t *p = (volatile uint32_t *)(mmio_base + reg);
    return *p;
}
static inline void mmio_write(uint32_t reg, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)(mmio_base + reg);
    *p = val;
    arch_memory_barrier();
}
#endif

#ifndef __aarch64__
static int find_virtio_net_pci(uint32_t *bar0_out)
{
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t function = 0; function < 8; ++function) {
                if (pci_vendor_id((uint8_t)bus, slot, function) != VIRTIO_VENDOR_ID)
                    continue;
                if (pci_device_id((uint8_t)bus, slot, function) != VIRTIO_NET_LEGACY_ID)
                    continue;
                *bar0_out = pci_bar0((uint8_t)bus, slot, function);
                return 1;
            }
        }
    }
    return 0;
}

static int vq_setup_pci(uint16_t index, uint8_t *memory, struct vq *q)
{
    outw(io_base + REG_QUEUE_SELECT, index);
    uint16_t max = inw(io_base + REG_QUEUE_SIZE);
    if (max == 0 || max > QUEUE_MAX_SIZE) return 0;
    if (inl(io_base + REG_QUEUE_ADDRESS) != 0) return 0;
    uint32_t addr = (uint32_t)(uintptr_t)memory;
    if ((addr & (PAGE_SIZE - 1)) != 0 || addr >= 0x40000000U) return 0;
    for (uint32_t i = 0; i < QUEUE_MEM_BYTES; ++i) memory[i] = 0;

    uint32_t desc_bytes  = 16U * max;
    uint32_t avail_bytes = 6U + 2U * max;
    uint32_t used_offset = align_up(desc_bytes + avail_bytes, PAGE_SIZE);

    q->desc  = (volatile struct virtq_desc *)(uintptr_t)addr;
    q->avail = (volatile struct virtq_avail *)(uintptr_t)(addr + desc_bytes);
    q->used  = (volatile struct virtq_used *)(uintptr_t)(addr + used_offset);
    q->size  = max;
    q->avail_idx = 0;
    q->last_used = 0;
    q->pad = 0;

    outl(io_base + REG_QUEUE_ADDRESS, addr / PAGE_SIZE);
    return 1;
}
#endif

#ifdef __aarch64__
static int vq_setup_mmio_v1(uint16_t index, uint8_t *memory, struct vq *q, uint16_t qsize)
{
    mmio_write(VIRTIO_MMIO_QUEUE_SEL, index);
    uint32_t max = mmio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0 || max > QUEUE_MAX_SIZE) return 0;
    if (qsize > max) qsize = (uint16_t)max;
    mmio_write(VIRTIO_MMIO_QUEUE_NUM, qsize);

    uint32_t addr = (uint32_t)(uintptr_t)memory;
    for (uint32_t i = 0; i < QUEUE_MEM_BYTES; ++i) memory[i] = 0;

    uint32_t desc_bytes  = 16U * qsize;
    uint32_t avail_bytes = 6U + 2U * qsize;
    uint32_t used_offset = align_up(desc_bytes + avail_bytes, PAGE_SIZE);

    q->desc  = (volatile struct virtq_desc *)(uintptr_t)addr;
    q->avail = (volatile struct virtq_avail *)(uintptr_t)(addr + desc_bytes);
    q->used  = (volatile struct virtq_used *)(uintptr_t)(addr + used_offset);
    q->size  = qsize;
    q->avail_idx = 0;
    q->last_used = 0;
    q->pad = 0;

    mmio_write(VIRTIO_MMIO_QUEUE_ALIGN, 4096);
    mmio_write(VIRTIO_MMIO_QUEUE_PFN, addr / 4096);
    return 1;
}
#endif

static void rx_post_buffer(uint16_t id)
{
    rxq.avail->ring[rxq.avail_idx % rxq.size] = id;
    arch_memory_barrier();
    rxq.avail_idx++;
    rxq.avail->idx = (uint16_t)rxq.avail_idx;
    arch_memory_barrier();
}

static int fail_device(const char *message)
{
    kprint("virtio-net ERROR: ");
    kprint(message);
    kprint("\n");
#ifdef __aarch64__
    if (mmio_base) mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE | ST_DRIVER | ST_FAILED);
#else
    outb(io_base + REG_STATUS, ST_ACKNOWLEDGE | ST_DRIVER | ST_FAILED);
#endif
    return 0;
}

int virtio_net_init(void)
{
    kprint("\nVIRTIO-NET INIT\n");
    kprint("---------------\n");

#ifdef __aarch64__
    mmio_base = 0;
    for (int i = 0; i < VIRTIO_MMIO_MAX_DEVICES; ++i) {
        uintptr_t base = VIRTIO_MMIO_BASE_START + (uint64_t)i * VIRTIO_MMIO_STRIDE;
        volatile uint32_t *p = (volatile uint32_t *)base;
        if (p[VIRTIO_MMIO_MAGIC_VALUE / 4] != 0x74726976) continue;
        if (p[VIRTIO_MMIO_DEVICE_ID / 4] == VIRTIO_DEV_ID_NET) {
            mmio_base = base;
            kprint("Encontrado net en slot ");
            kprint_dec((uint32_t)i);
            kprint("\n");
            break;
        }
    }
    if (!mmio_base) {
        kprint("virtio-net-mmio no encontrado.\n");
        return 0;
    }

    mmio_write(VIRTIO_MMIO_STATUS, 0);
    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE);
    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE | ST_DRIVER);
    mmio_write(VIRTIO_MMIO_DRIVER_FEATURES, 0);
    mmio_write(VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);

    if (!vq_setup_mmio_v1(0, virtq0_memory, &rxq, 8)) return fail_device("RX queue failed");
    if (!vq_setup_mmio_v1(1, virtq1_memory, &txq, 8)) return fail_device("TX queue failed");

    for (uint16_t i = 0; i < RX_BUFFER_COUNT && i < rxq.size; ++i) {
        rxq.desc[i].addr  = (uint64_t)(uintptr_t)rx_buffers[i];
        rxq.desc[i].len   = RX_BUFFER_SIZE;
        rxq.desc[i].flags = DESC_F_WRITE;
        rxq.desc[i].next  = 0;
        rx_post_buffer(i);
    }

    txq.desc[0].addr  = (uint64_t)(uintptr_t)tx_buffer;
    txq.desc[0].flags = 0;
    txq.desc[0].next  = 0;

    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE | ST_DRIVER | ST_DRIVER_OK);
    uint32_t status = mmio_read(VIRTIO_MMIO_STATUS);
    if (status & ST_FAILED) return fail_device("FAILED");

    volatile uint8_t *cfg = (volatile uint8_t *)(mmio_base + VIRTIO_MMIO_CONFIG);
    for (int i = 0; i < 6; ++i) mac_addr[i] = cfg[i];

    kprint("MAC: ");
    kprint_mac(mac_addr);
    kprint("\n");

    mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    ready = 1;
    kprint("virtio-net ready\n");
    return 1;

#else
    uint32_t bar0 = 0;
    if (!find_virtio_net_pci(&bar0)) return 0;
    if ((bar0 & 1) == 0) return 0;
    io_base = (uint16_t)(bar0 & 0xFFFC);
    outb(io_base + REG_STATUS, 0);
    outb(io_base + REG_STATUS, ST_ACKNOWLEDGE);
    outb(io_base + REG_STATUS, ST_ACKNOWLEDGE | ST_DRIVER);
    outl(io_base + REG_DRIVER_FEATURES, 0);

    if (!vq_setup_pci(0, virtq0_memory, &rxq)) return fail_device("RX queue setup failed");
    if (!vq_setup_pci(1, virtq1_memory, &txq)) return fail_device("TX queue setup failed");

    for (uint16_t i = 0; i < RX_BUFFER_COUNT; ++i) {
        rxq.desc[i].addr  = (uint64_t)(uintptr_t)rx_buffers[i];
        rxq.desc[i].len   = RX_BUFFER_SIZE;
        rxq.desc[i].flags = DESC_F_WRITE;
        rxq.desc[i].next  = 0;
        rx_post_buffer(i);
    }
    txq.desc[0].addr  = (uint64_t)(uintptr_t)tx_buffer;
    txq.desc[0].flags = 0;
    txq.desc[0].next  = 0;

    outb(io_base + REG_STATUS, ST_ACKNOWLEDGE | ST_DRIVER | ST_DRIVER_OK);
    uint8_t status = inb(io_base + REG_STATUS);
    if (status & ST_FAILED) return fail_device("device reported FAILED");

    for (int i = 0; i < 6; ++i) {
        mac_addr[i] = inb((uint16_t)(io_base + REG_DEVICE_CONFIG + i));
    }
    outw(io_base + REG_QUEUE_NOTIFY, 0);
    ready = 1;
    kprint("virtio-net ready\n");
    return 1;
#endif
}

const uint8_t *virtio_net_mac(void) { return mac_addr; }

int virtio_net_send(const void *frame, uint16_t len)
{
    net_lock();
    if (!ready || len == 0 || len > (TX_BUFFER_SIZE - NET_HDR_SIZE)) { net_unlock(); return -1; }
    uint16_t frame_len = len < ETH_MIN_FRAME ? ETH_MIN_FRAME : len;
    const uint8_t *src = (const uint8_t *)frame;
    volatile uint8_t *dst = tx_buffer;
    for (uint16_t i = 0; i < NET_HDR_SIZE; ++i) dst[i] = 0;
    for (uint16_t i = 0; i < len; ++i) dst[NET_HDR_SIZE + i] = src[i];
    for (uint16_t i = len; i < frame_len; ++i) dst[NET_HDR_SIZE + i] = 0;

    txq.desc[0].len = (uint32_t)(NET_HDR_SIZE + frame_len);
    txq.avail->ring[txq.avail_idx % txq.size] = 0;
    arch_memory_barrier();
    txq.avail_idx++;
    txq.avail->idx = (uint16_t)txq.avail_idx;
    arch_memory_barrier();

#ifdef __aarch64__
    mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 1);
#else
    outw(io_base + REG_QUEUE_NOTIFY, 1);
#endif

    uint64_t start = arch_cycle_counter();
    while (txq.used->idx != (uint16_t)txq.avail_idx) {
        if ((arch_cycle_counter() - start) > TX_TIMEOUT_TICKS) {
            kprint("virtio-net: TX timeout\n");
            net_unlock();
            return -1;
        }
        arch_pause();
    }
    arch_memory_barrier();
    stat_tx_packets++;
    stat_tx_bytes += frame_len;
    net_unlock();
    return 0;
}

int virtio_net_poll(void *out, uint16_t max, uint16_t *len_out)
{
    if (!ready) return 0;
    if (rxq.used->idx == (uint16_t)rxq.last_used) return 0;

    arch_memory_barrier();
    volatile struct virtq_used_elem *elem = &rxq.used->ring[rxq.last_used % rxq.size];
    uint32_t id  = elem->id;
    uint32_t len = elem->len;
    rxq.last_used++;

    int received = 0;
    if (id < RX_BUFFER_COUNT && len > NET_HDR_SIZE) {
        uint32_t frame_len = len - NET_HDR_SIZE;
        if (frame_len > max) frame_len = max;
        const volatile uint8_t *src = rx_buffers[id] + NET_HDR_SIZE;
        uint8_t *dst = (uint8_t *)out;
        for (uint32_t i = 0; i < frame_len; ++i) dst[i] = src[i];
        *len_out = (uint16_t)frame_len;
        received = 1;
        stat_rx_packets++;
        stat_rx_bytes += frame_len;
    }

    if (id < RX_BUFFER_COUNT) {
        rx_post_buffer((uint16_t)id);
#ifdef __aarch64__
        mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
#else
        outw(io_base + REG_QUEUE_NOTIFY, 0);
#endif
    }
    return received;
}

void virtio_net_stats(uint32_t *tx_pkts, uint32_t *rx_pkts, uint64_t *tx_b, uint64_t *rx_b)
{
    if (tx_pkts) *tx_pkts = stat_tx_packets;
    if (rx_pkts) *rx_pkts = stat_rx_packets;
    if (tx_b)    *tx_b    = stat_tx_bytes;
    if (rx_b)    *rx_b    = stat_rx_bytes;
}
