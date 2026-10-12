#ifndef MYOS_MUTEX_H
#define MYOS_MUTEX_H

#include <stdint.h>
#include "thread.h"

struct kmutex {
    struct tcb   *owner;    /* Offset 0: 8 bytes */
    const char   *name;     /* Offset 8: 8 bytes */
    volatile int  locked;   /* Offset 16: 4 bytes */
    volatile int  depth;    /* Offset 20: 4 bytes -> total 24 B */
} __attribute__((aligned(8)));

void kmutex_init(struct kmutex *m, const char *name);
void kmutex_lock(struct kmutex *m);
int  kmutex_trylock(struct kmutex *m);
void kmutex_unlock(struct kmutex *m);
void kmutex_release_all_for_thread(struct tcb *t);
int  kmutex_test_self(void);

#endif
