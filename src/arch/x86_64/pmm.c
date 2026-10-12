#include "pmm.h"
#include "console.h"
#include "mem.h"

/* Estructuras de la especificacion Multiboot 1 */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

/*
 * Bitmap de marcos fisicos de 4 KiB.
 * Capacidad: 512 MiB -> 131,072 marcos -> 16,384 bytes (16 KiB en BSS).
 * Bit = 1 -> Marco Ocupado / Reservado
 * Bit = 0 -> Marco Libre
 */
#define PMM_MAX_FRAMES   131072ULL
#define PMM_BITMAP_BYTES (PMM_MAX_FRAMES / 8ULL)

/* Blindaje: primeros 16 MiB reservados al hardware, kernel, virtqueues y heap inicial */
#define PMM_RESERVED_LOWER (16 * 1024 * 1024ULL)

static uint8_t pmm_bitmap[PMM_BITMAP_BYTES];
static size_t  pmm_total_frames = 0;
static size_t  pmm_used_frames = 0;

static inline void set_bit(size_t frame)
{
    pmm_bitmap[frame / 8] |= (uint8_t)(1U << (frame % 8));
}

static inline void clear_bit(size_t frame)
{
    pmm_bitmap[frame / 8] &= (uint8_t)~(1U << (frame % 8));
}

static inline int test_bit(size_t frame)
{
    return (pmm_bitmap[frame / 8] & (1U << (frame % 8))) != 0;
}

static void mark_region_free(uint64_t base, uint64_t length)
{
    uint64_t start_frame = (base + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    uint64_t end_frame   = (base + length) / PMM_FRAME_SIZE;

    if (end_frame > PMM_MAX_FRAMES) {
        end_frame = PMM_MAX_FRAMES;
    }

    for (uint64_t f = start_frame; f < end_frame; ++f) {
        if (test_bit(f)) {
            clear_bit(f);
            if (pmm_used_frames > 0) pmm_used_frames--;
        }
    }
}

static void mark_region_used(uint64_t base, uint64_t length)
{
    uint64_t start_frame = base / PMM_FRAME_SIZE;
    uint64_t end_frame   = (base + length + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;

    if (end_frame > PMM_MAX_FRAMES) {
        end_frame = PMM_MAX_FRAMES;
    }

    for (uint64_t f = start_frame; f < end_frame; ++f) {
        if (!test_bit(f)) {
            set_bit(f);
            pmm_used_frames++;
        }
    }
}

void pmm_init(uint32_t mb_magic, uint32_t mb_info_addr)
{
    /* 1. Por defecto, marcar toda la memoria como ocupada/reservada */
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));
    pmm_total_frames = 0; /* ramstat: se calcula con el techo real de RAM */
    pmm_used_frames  = PMM_MAX_FRAMES;

    int parsed_mmap = 0;

    /* 2. Si Multiboot 1 esta presente y reporta mmap valido (flag bit 6) */
    if (mb_magic == 0x2BADB002 && mb_info_addr != 0) {
        struct multiboot_info *mbi = (struct multiboot_info *)(uintptr_t)mb_info_addr;
        if (mbi->flags & (1 << 6)) {
            uintptr_t mmap_curr = (uintptr_t)mbi->mmap_addr;
            uintptr_t mmap_end  = mmap_curr + mbi->mmap_length;

            while (mmap_curr < mmap_end) {
                struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)mmap_curr;
                if (entry->type == 1) { /* Type 1 = RAM Utilizable */
                    mark_region_free(entry->addr, entry->len);
                    uint64_t top = entry->addr + entry->len;
                    size_t top_frame = (size_t)(top / PMM_FRAME_SIZE);
                    if (top_frame > pmm_total_frames) {
                        pmm_total_frames = (top_frame < PMM_MAX_FRAMES) ? top_frame : PMM_MAX_FRAMES;
                    }
                }
                mmap_curr += (entry->size + sizeof(uint32_t));
            }
            parsed_mmap = 1;
        }
    }

    /* Fallback si no hay mmap de GRUB: asumir 256 MiB estandar de QEMU */
    if (!parsed_mmap) {
        pmm_total_frames = (256 * 1024 * 1024ULL) / PMM_FRAME_SIZE;
        mark_region_free(0, 256 * 1024 * 1024ULL);
    }

    /* 3. Blindaje critico: los primeros 16 MiB quedan estrictamente reservados */
    mark_region_used(0, PMM_RESERVED_LOWER);

    /* ramstat: sin mapa valido, usar el maximo; luego recontar usados
     * solo dentro de la RAM real (los marcos por encima no existen). */
    if (pmm_total_frames == 0) pmm_total_frames = PMM_MAX_FRAMES;
    {
        size_t real_used = 0;
        for (size_t f = 0; f < pmm_total_frames; ++f) {
            if (test_bit(f)) real_used++;
        }
        pmm_used_frames = real_used;
    }

    kprint("PMM: Inicializado (Bitmap Allocator 4 KiB)\n");
    pmm_dump_stats();
}

uintptr_t pmm_alloc_frame(void)
{
    /* Comenzamos la busqueda por encima de los 16 MiB reservados */
    size_t start_f = PMM_RESERVED_LOWER / PMM_FRAME_SIZE;

    for (size_t f = start_f; f < pmm_total_frames; ++f) {
        if (!test_bit(f)) {
            set_bit(f);
            pmm_used_frames++;
            return (uintptr_t)(f * PMM_FRAME_SIZE);
        }
    }

    return 0; /* Out of Physical Memory */
}

void pmm_free_frame(uintptr_t phys_addr)
{
    if (phys_addr < PMM_RESERVED_LOWER) {
        return; /* Prohibido liberar memoria reservada del sistema */
    }

    size_t f = (size_t)(phys_addr / PMM_FRAME_SIZE);
    if (f < pmm_total_frames) {
        if (test_bit(f)) {
            clear_bit(f);
            if (pmm_used_frames > 0) pmm_used_frames--;
        }
    }
}

void pmm_get_stats(size_t *free_frames, size_t *used_frames, size_t *total_frames)
{
    if (total_frames) *total_frames = pmm_total_frames;
    if (used_frames)  *used_frames  = pmm_used_frames;
    if (free_frames)  *free_frames  = (pmm_total_frames > pmm_used_frames)
                                      ? (pmm_total_frames - pmm_used_frames) : 0;
}

void pmm_dump_stats(void)
{
    size_t f_free = 0, f_used = 0, f_total = 0;
    pmm_get_stats(&f_free, &f_used, &f_total);

    kprint("PMM RAM: Total ");
    kprint_dec((uint32_t)((f_total * PMM_FRAME_SIZE) / (1024 * 1024)));
    kprint(" MiB (");
    kprint_dec((uint32_t)f_total);
    kprint(" frames) | Usados: ");
    kprint_dec((uint32_t)f_used);
    kprint(" | Libres: ");
    kprint_dec((uint32_t)f_free);
    kprint(" (");
    kprint_dec((uint32_t)((f_free * PMM_FRAME_SIZE) / (1024 * 1024)));
    kprint(" MiB disponibles)\n");
}

int pmm_test_self(void)
{
    kprint("\n[PMM AUTO-TEST] Verificando asignador de marcos fisicos...\n");
    size_t free_before = 0, used_before = 0, total_before = 0;
    pmm_get_stats(&free_before, &used_before, &total_before);

    uintptr_t frame1 = pmm_alloc_frame();
    uintptr_t frame2 = pmm_alloc_frame();
    uintptr_t frame3 = pmm_alloc_frame();

    if (!frame1 || !frame2 || !frame3) {
        kprint("  FALLO: pmm_alloc_frame retorno 0 (OOM prematuro)\n");
        return 0;
    }

    /* Comprobar alineacion estricta a 4096 bytes */
    if ((frame1 & 0xFFF) != 0 || (frame2 & 0xFFF) != 0 || (frame3 & 0xFFF) != 0) {
        kprint("  FALLO: frames no alineados a 4 KiB\n");
        return 0;
    }

    /* Comprobar que estan por encima de los 16 MiB reservados */
    if (frame1 < PMM_RESERVED_LOWER || frame2 < PMM_RESERVED_LOWER || frame3 < PMM_RESERVED_LOWER) {
        kprint("  FALLO: frame asignado invade la zona reservada de 16 MiB\n");
        return 0;
    }

    /* Comprobar que son distintos */
    if (frame1 == frame2 || frame2 == frame3 || frame1 == frame3) {
        kprint("  FALLO: frames duplicados detectados\n");
        return 0;
    }

    kprint("  Asignados 3 frames fisicos OK: 0x");
    kprint_hex32((uint32_t)frame1);
    kprint(", 0x");
    kprint_hex32((uint32_t)frame2);
    kprint(", 0x");
    kprint_hex32((uint32_t)frame3);
    kprint("\n");

    /* Liberar */
    pmm_free_frame(frame2);
    pmm_free_frame(frame1);
    pmm_free_frame(frame3);

    size_t free_after = 0, used_after = 0, total_after = 0;
    pmm_get_stats(&free_after, &used_after, &total_after);

    if (free_after != free_before || used_after != used_before) {
        kprint("  FALLO: fuga o inconsistencia en conteo de frames tras free\n");
        return 0;
    }

    kprint("  Liberacion y recontabilizacion verficada OK (0 fugas)\n");
    kprint("[PMM AUTO-TEST] SUPERADO CON EXITO.\n\n");
    return 1;
}
