#ifndef MYOS_SYSINFO_H
#define MYOS_SYSINFO_H

#include <stdint.h>

struct sysinfo {
    uint64_t cr0;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t rsp;
    uint64_t tx_bytes;
    uint64_t rx_bytes;

    const uint8_t *mac;
    const uint8_t *ip;
    const uint8_t *gw;

    uint32_t tx_packets;
    uint32_t rx_packets;
} __attribute__((aligned(8)));

extern uint64_t tsc_ticks_per_ms;
extern uint64_t tsc_freq_mhz;
uint64_t timer_calibrate_tsc(void);

void sysinfo_get(struct sysinfo *info);
void sysinfo_print_mem(void);
void sysinfo_print_stats(void);
int  sysinfo_format_telemetry(char *out_buf, uint32_t max);
int  sysinfo_format_mem(char *out_buf, uint32_t max);

#endif
