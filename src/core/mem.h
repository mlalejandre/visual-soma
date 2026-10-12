#ifndef MYOS_MEM_H
#define MYOS_MEM_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* Inicializacion del Heap Dinamico VMM */
void  kheap_init(void);

/* Asignador y liberador alineados estrictamente a 16 bytes */
void *kmalloc(size_t size);
void  kfree(void *ptr);

/* Telemetria y estadisticas de memoria dinamica */
void  kheap_stats(size_t *used, size_t *free_bytes);
void  kheap_dump_stats(void);

/* Auto-test con prueba de expansion dinamica en vivo (>16 KiB) */
int   kheap_test_self(void);

#endif
