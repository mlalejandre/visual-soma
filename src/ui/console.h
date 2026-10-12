#ifndef MYOS_CONSOLE_H
#define MYOS_CONSOLE_H

#include <stdint.h>

#define CMD_LINE_MAX 4096

void kputc(char c);
void kprint(const char *s);
void kprint_hex8(uint8_t value);
void kprint_hex16(uint16_t value);
void kprint_hex32(uint32_t value);
void kprint_dec(uint32_t value);
void kprint_mac(const uint8_t *mac);
void kprint_ip(const uint8_t *ip);

int  kgetc_ready(void);
char kgetc(void);
int  kgetline(char *buf, uint32_t max);
void console_clear(void);
extern volatile int console_muted;
void keyboard_irq_handler(void);
void keyboard_reset_state(void);

int  console_history_dump(char *out_buf, uint32_t max_out);
void console_history_load_from_vfs(void);
void console_history_sync_to_vfs(void);

#endif
