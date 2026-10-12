#ifndef MYOS_RAMSTAT_H
#define MYOS_RAMSTAT_H

#include <stdint.h>

/* ramstat v2 */
struct ramstat_info {
    uint64_t total;        /* RAM total visible (bytes) */
    uint64_t used;         /* marcos usados segun el PMM */
    uint64_t free_b;       /* marcos libres */
    uint64_t footprint;    /* memoria comprometida (incluye zona baja) */
    uint64_t image;        /* imagen del kernel (txt+data+bss) */
    uint64_t heap_used;    /* kmalloc en uso */
    uint64_t heap_free;    /* heap libre ya mapeado */
    uint64_t reserved;     /* zona baja sumada aparte (0 si el PMM ya la cuenta) */
    uint64_t lower;        /* zona baja reservada (16 MiB) */
    uint64_t lower_image;  /* parte de la zona baja ocupada por la imagen */
    uint64_t lower_rest;   /* resto de la zona baja (reserva sin medir) */
    uint64_t heap_mapped;  /* heap_used + heap_free */
    uint64_t other;        /* pilas de hilos + tablas de paginas + otros */
    uint32_t pct10;        /* ocupacion en decimas de % */
};

void ramstat_get(struct ramstat_info *ri);
void ramstat_print(void);

/* Linea base para medir el coste de una aplicacion */
void ramstat_set_baseline(void);
void ramstat_clear_baseline(void);
int  ramstat_get_baseline(struct ramstat_info *ri); /* 1 si existe */

#endif
