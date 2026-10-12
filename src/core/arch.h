#ifndef MYOS_ARCH_H
#define MYOS_ARCH_H

#include <stdint.h>

#ifdef __aarch64__
#define MYOS_ARCH_NAME "aarch64"
#else
#define MYOS_ARCH_NAME "x86_64"
#endif

#define MYOS_ARCH_BITS 64

/* ====================================================================
 * PRIMITIVAS DE CPU Y BARRERAS DE MEMORIA
 * ==================================================================== */

static inline void arch_pause(void)
{
#ifdef __x86_64__
    __asm__ volatile ("pause");
#else
    __asm__ volatile ("yield");
#endif
}

static inline void arch_halt(void)
{
#ifdef __x86_64__
    __asm__ volatile ("hlt");
#else
    __asm__ volatile ("wfi");
#endif
}

static inline void arch_interrupts_enable(void)
{
#ifdef __x86_64__
    __asm__ volatile ("sti");
#else
    /* AArch64: Mantener IRQs enmascaradas hasta enlazar VBAR_EL1 y GIC */
    (void)0;
#endif
}

static inline void arch_interrupts_disable(void)
{
#ifdef __x86_64__
    __asm__ volatile ("cli");
#else
    __asm__ volatile ("msr daifset, #2");
#endif
}

static inline void arch_memory_barrier(void)
{
#ifdef __x86_64__
    __asm__ volatile ("mfence" ::: "memory");
#else
    __asm__ volatile ("dmb ish" ::: "memory");
#endif
}

static inline uint64_t arch_cycle_counter(void)
{
#ifdef __x86_64__
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    uint64_t val;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#endif
}

/* ====================================================================
 * ABSTRACCION HAL DE ENTRADA/SALIDA
 * ==================================================================== */

static inline void arch_outb(uint16_t port, uint8_t val)
{
#ifdef __x86_64__
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
#else
    (void)port; (void)val;
#endif
}

static inline uint8_t arch_inb(uint16_t port)
{
#ifdef __x86_64__
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
#else
    (void)port; return 0;
#endif
}

static inline void arch_outw(uint16_t port, uint16_t val)
{
#ifdef __x86_64__
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
#else
    (void)port; (void)val;
#endif
}

static inline uint16_t arch_inw(uint16_t port)
{
#ifdef __x86_64__
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
#else
    (void)port; return 0;
#endif
}

static inline void arch_outl(uint16_t port, uint32_t val)
{
#ifdef __x86_64__
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
#else
    (void)port; (void)val;
#endif
}

static inline uint32_t arch_inl(uint16_t port)
{
#ifdef __x86_64__
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
#else
    (void)port; return 0;
#endif
}

#endif
