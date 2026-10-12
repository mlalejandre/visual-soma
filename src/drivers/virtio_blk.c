#include <stdint.h>
#include "console.h"
#include "io.h"
#include "pci.h"
#include "mem.h"
#include "mutex.h"
#include "virtio_blk.h"
#include "arch.h"

#define VIRTIO_VENDOR_ID       0x1AF4
#define VIRTIO_BLK_LEGACY_ID   0x1001

#define REG_DEVICE_FEATURES    0x00
#define REG_DRIVER_FEATURES    0x04
#define REG_QUEUE_ADDRESS      0x08
#define REG_QUEUE_SIZE         0x0C
#define REG_QUEUE_SELECT       0x0E
#define REG_QUEUE_NOTIFY       0x10
#define REG_STATUS             0x12

#define ST_ACKNOWLEDGE         0x01
#define ST_DRIVER              0x02
#define ST_DRIVER_OK           0x04
#define ST_FAILED              0x80

#define VIRTIO_BLK_T_IN        0
#define VIRTIO_BLK_T_OUT       1

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

#define VIRTIO_MMIO_BASE_START      0x0a000000ULL
#define VIRTIO_MMIO_STRIDE          0x200
#define VIRTIO_MMIO_MAX_DEVICES     32
#define VIRTIO_DEV_ID_BLOCK         2
#endif

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

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
    volatile struct virtq_desc  *desc;       /* offset 0 */
    volatile struct virtq_avail *avail;      /* offset 8 */
    volatile struct virtq_used  *used;       /* offset 16 */
    uint32_t size;                           /* offset 24 (4-byte aligned) */
    uint32_t avail_idx;                      /* offset 28 (4-byte aligned) */
    uint32_t last_used;                      /* offset 32 (4-byte aligned) */
    uint32_t pad;                            /* offset 36 -> total 40 bytes */
} __attribute__((aligned(16)));

struct virtio_blk_req {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} __attribute__((aligned(16)));

#ifndef __aarch64__
static uint16_t io_base = 0;
#endif
#ifdef __aarch64__
static uintptr_t mmio_base = 0;
#endif

static struct vq blkq __attribute__((aligned(16)));
static struct kmutex blk_mutex __attribute__((aligned(16)));
static int blk_ready = 0;
static int blk_broken = 0;

static uint8_t blk_memory[16384] __attribute__((aligned(4096)));
static volatile struct virtio_blk_req req __attribute__((aligned(16)));
static volatile uint8_t status_byte __attribute__((aligned(16)));
static volatile uint8_t buffer[512] __attribute__((aligned(16)));

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

int virtio_blk_init(void)
{
    kmutex_init(&blk_mutex, "virtio_blk_lock");
    kprint("\nVIRTIO-BLK INIT\n");
    kprint("---------------\n");

#ifdef __aarch64__
    mmio_base = 0;
    for (int i = 0; i < VIRTIO_MMIO_MAX_DEVICES; ++i) {
        uintptr_t base = VIRTIO_MMIO_BASE_START + (uint64_t)i * VIRTIO_MMIO_STRIDE;
        volatile uint32_t *p = (volatile uint32_t *)base;
        if (p[VIRTIO_MMIO_MAGIC_VALUE / 4] != 0x74726976) continue;
        if (p[VIRTIO_MMIO_DEVICE_ID / 4] == VIRTIO_DEV_ID_BLOCK) {
            mmio_base = base;
            kprint("Encontrado blk en slot ");
            kprint_dec((uint32_t)i);
            kprint("\n");
            break;
        }
    }
    if (!mmio_base) {
        kprint("virtio-blk-mmio no encontrado.\n");
        return 0;
    }

    mmio_write(VIRTIO_MMIO_STATUS, 0);
    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE);
    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE | ST_DRIVER);
    mmio_write(VIRTIO_MMIO_DRIVER_FEATURES, 0);
    mmio_write(VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
    mmio_write(VIRTIO_MMIO_QUEUE_SEL, 0);

    uint32_t max = mmio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0 || max > 256) {
        kprint("ERROR tamano cola\n");
        return 0;
    }

    uint16_t qsize = 8;
    if (qsize > max) qsize = (uint16_t)max;
    mmio_write(VIRTIO_MMIO_QUEUE_NUM, qsize);

    uint32_t addr = (uint32_t)(uintptr_t)blk_memory;
    for (uint32_t i = 0; i < sizeof(blk_memory); i++) blk_memory[i] = 0;
    arch_memory_barrier();

    blkq.desc  = (volatile struct virtq_desc *)(uintptr_t)addr;
    blkq.avail = (volatile struct virtq_avail *)(uintptr_t)(addr + 16 * qsize);
    uint32_t used_off = 4096;
    blkq.used  = (volatile struct virtq_used *)(uintptr_t)(addr + used_off);
    blkq.size  = qsize;
    blkq.avail_idx = 0;
    blkq.last_used = 0;
    blkq.pad = 0;

    mmio_write(VIRTIO_MMIO_QUEUE_ALIGN, 4096);
    mmio_write(VIRTIO_MMIO_QUEUE_PFN, addr / 4096);

    mmio_write(VIRTIO_MMIO_STATUS, ST_ACKNOWLEDGE | ST_DRIVER | ST_DRIVER_OK);
    uint32_t st = mmio_read(VIRTIO_MMIO_STATUS);

    if (st & ST_FAILED) {
        kprint("virtio-blk ERROR: FAILED status\n");
        return 0;
    }

    blk_ready = 1;
    kprint("virtio-blk ready (Disco persistente conectado)\n");
    return 1;

#else
    uint32_t bar0 = 0;
    int found = 0;
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t function = 0; function < 8; ++function) {
                if (pci_vendor_id(bus, slot, function) == VIRTIO_VENDOR_ID &&
                    pci_device_id(bus, slot, function) == VIRTIO_BLK_LEGACY_ID) {
                    bar0 = pci_bar0(bus, slot, function);
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (found) break;
    }
    if (!found || !(bar0 & 1)) {
        kprint("virtio-blk no encontrado o BAR0 invalido.\n");
        return 0;
    }
    io_base = (uint16_t)(bar0 & 0xFFFC);
    kprint("I/O base: 0x"); kprint_hex16(io_base); kprint("\n");
    outb(io_base + REG_STATUS, 0);
    outb(io_base + REG_STATUS, ST_ACKNOWLEDGE | ST_DRIVER);
    outl(io_base + REG_DRIVER_FEATURES, 0);
    outw(io_base + REG_QUEUE_SELECT, 0);
    uint16_t max = inw(io_base + REG_QUEUE_SIZE);
    kprint("Queue 0 max size: "); kprint_dec(max); kprint("\n");
    if (max == 0 || max > 256) return 0;
    uint32_t addr = (uint32_t)(uintptr_t)blk_memory;
    for (uint32_t i = 0; i < sizeof(blk_memory); i++) blk_memory[i] = 0;
    blkq.desc = (void*)(uintptr_t)addr;
    blkq.avail = (void*)(uintptr_t)(addr + 16 * max);
    blkq.used = (void*)(uintptr_t)((addr + 16 * max + 6 + 2 * max + 4095) & ~4095);
    blkq.size = max;
    blkq.avail_idx = 0;
    blkq.last_used = 0;
    blkq.pad = 0;
    outl(io_base + REG_QUEUE_ADDRESS, addr / 4096);
    outb(io_base + REG_STATUS, ST_ACKNOWLEDGE | ST_DRIVER | ST_DRIVER_OK);
    uint8_t status = inb(io_base + REG_STATUS);
    if (status & ST_FAILED) {
        kprint("virtio-blk ERROR: FAILED status.\n");
        return 0;
    }
    blk_ready = 1;
    kprint("virtio-blk ready (Disco duro conectado)\n");
    return 1;
#endif
}

static int virtio_blk_op(uint64_t sector, void *buf, int write)
{
    if (!blk_ready || blk_broken) return -1;
    kmutex_lock(&blk_mutex);
    req.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req.ioprio = 0;
    req.sector = sector;
    status_byte = 0xFF;
    if (write) memcpy((void*)buffer, buf, 512);
    
    blkq.desc[0].addr = (uint64_t)(uintptr_t)&req;
    blkq.desc[0].len = 16;
    blkq.desc[0].flags = 1;
    blkq.desc[0].next = 1;
    
    blkq.desc[1].addr = (uint64_t)(uintptr_t)buffer;
    blkq.desc[1].len = 512;
    blkq.desc[1].flags = 1 | (write ? 0 : 2);
    blkq.desc[1].next = 2;
    
    blkq.desc[2].addr = (uint64_t)(uintptr_t)&status_byte;
    blkq.desc[2].len = 1;
    blkq.desc[2].flags = 2;
    blkq.desc[2].next = 0;
    
    blkq.avail->ring[blkq.avail_idx % blkq.size] = 0;
    arch_memory_barrier();
    blkq.avail_idx++;
    blkq.avail->idx = (uint16_t)blkq.avail_idx;
    arch_memory_barrier();
    
#ifdef __aarch64__
    mmio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
#else
    outw(io_base + REG_QUEUE_NOTIFY, 0);
#endif
    
    uint64_t start = arch_cycle_counter();
    extern uint64_t tsc_freq_mhz;
    uint64_t limit_ticks = (tsc_freq_mhz > 0 ? tsc_freq_mhz * 1000000ULL : 62500000ULL) * 2;
    
    while (blkq.used->idx == (uint16_t)blkq.last_used) {
        if ((arch_cycle_counter() - start) > limit_ticks) {
            blk_broken = 1;
            kprint("\nvirtio-blk ERROR: timeout en sector ");
            kprint_dec((uint32_t)sector);
            kprint("\n");
            kmutex_unlock(&blk_mutex);
            return -1;
        }
        arch_pause();
    }
    
    blkq.last_used++;
    arch_memory_barrier();
    
    if (status_byte != 0) {
        kprint("\nvirtio-blk ERROR: status_byte=");
        kprint_hex8(status_byte);
        kprint("\n");
        kmutex_unlock(&blk_mutex);
        return -1;
    }
    
    if (!write) memcpy(buf, (void*)buffer, 512);
    kmutex_unlock(&blk_mutex);
    return 0;
}

int virtio_blk_read(uint64_t sector, void *buf)
{
    return virtio_blk_op(sector, buf, 0);
}

int virtio_blk_write(uint64_t sector, const void *buf)
{
    return virtio_blk_op(sector, (void *)buf, 1);
}
