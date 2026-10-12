#include <stdint.h>
#include <stddef.h>
#include "ramstat.h"
#include "console.h"
#include "pmm.h"
#include "mem.h"

/* ramstat v2 */
extern char __text_start[];
extern char __bss_end[];

#define RAMSTAT_RESERVED_LOWER (16ULL * 1024ULL * 1024ULL)

static struct ramstat_info base_ri;
static int base_ok = 0;

void ramstat_get(struct ramstat_info *ri)
{
    size_t free_f = 0, used_f = 0, total_f = 0;
    size_t heap_used = 0, heap_free = 0;

    pmm_get_stats(&free_f, &used_f, &total_f);
    kheap_stats(&heap_used, &heap_free);

    ri->total  = (uint64_t)total_f * PMM_FRAME_SIZE;
    ri->used   = (uint64_t)used_f  * PMM_FRAME_SIZE;
    ri->free_b = (uint64_t)free_f  * PMM_FRAME_SIZE;

    ri->reserved  = (ri->used >= RAMSTAT_RESERVED_LOWER) ? 0 : RAMSTAT_RESERVED_LOWER;
    ri->footprint = ri->used + ri->reserved;

    ri->image     = (uint64_t)(uintptr_t)__bss_end - (uint64_t)(uintptr_t)__text_start;
    ri->heap_used = (uint64_t)heap_used;
    ri->heap_free = (uint64_t)heap_free;

    ri->lower       = RAMSTAT_RESERVED_LOWER;
    ri->lower_image = (ri->image < ri->lower) ? ri->image : ri->lower;
    ri->lower_rest  = ri->lower - ri->lower_image;

    ri->heap_mapped = ri->heap_used + ri->heap_free;
    uint64_t above  = (ri->footprint > ri->lower) ? (ri->footprint - ri->lower) : 0;
    ri->other       = (above > ri->heap_mapped) ? (above - ri->heap_mapped) : 0;

    ri->pct10 = ri->total ? (uint32_t)((ri->footprint * 1000ULL) / ri->total) : 0;
}

void ramstat_set_baseline(void)   { ramstat_get(&base_ri); base_ok = 1; }
void ramstat_clear_baseline(void) { base_ok = 0; }
int  ramstat_get_baseline(struct ramstat_info *ri)
{
    if (!base_ok) return 0;
    *ri = base_ri;
    return 1;
}

static void ram_line(const char *label, uint64_t bytes)
{
    uint32_t mib  = (uint32_t)(bytes >> 20);
    uint32_t frac = (uint32_t)(((bytes & 0xFFFFFULL) * 100ULL) >> 20);

    kprint(label);
    kprint_dec(mib);
    kprint(".");
    if (frac < 10) kprint("0");
    kprint_dec(frac);
    kprint(" MiB (");
    kprint_dec((uint32_t)(bytes >> 10));
    kprint(" KiB)\n");
}

static void ram_delta_line(const char *label, uint64_t now, uint64_t base)
{
    kprint(label);
    if (now >= base) { kprint("+"); kprint_dec((uint32_t)((now - base) >> 10)); }
    else             { kprint("-"); kprint_dec((uint32_t)((base - now) >> 10)); }
    kprint(" KiB\n");
}

void ramstat_print(void)
{
    struct ramstat_info ri, b;
    ramstat_get(&ri);

    kprint("\n--------------- CONSUMO DE RAM (SOMA OS) ---------------\n");
    ram_line("RAM total visible        : ", ri.total);
    ram_line("COMPROMETIDA (huella)    : ", ri.footprint);
    ram_line("  Zona baja reservada    : ", ri.lower);
    ram_line("    Imagen kernel        : ", ri.lower_image);
    ram_line("    Reserva sin medir    : ", ri.lower_rest);
    ram_line("  Heap mapeado           : ", ri.heap_mapped);
    ram_line("    En uso (kmalloc)     : ", ri.heap_used);
    ram_line("    Libre                : ", ri.heap_free);
    ram_line("  Pilas/tablas/otros     : ", ri.other);
    ram_line("LIBRE                    : ", ri.free_b);
    kprint("Ocupacion: ");
    kprint_dec(ri.pct10 / 10);
    kprint(".");
    kprint_dec(ri.pct10 % 10);
    kprint(" %\n");

    if (ramstat_get_baseline(&b)) {
        kprint("--- Delta desde la linea base ---\n");
        ram_delta_line("  Huella             : ", ri.footprint, b.footprint);
        ram_delta_line("  Heap en uso        : ", ri.heap_used, b.heap_used);
        ram_delta_line("  Pilas/tablas/otros : ", ri.other, b.other);
    }
    kprint("---------------------------------------------------------\n\n");
}
