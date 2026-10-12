#include "arch/aarch64/rpi_fb.h"
volatile int console_muted = 0;
#include "arch.h"
#include "thread.h"
#include "console.h"
#include "shell.h"
#include "io.h"
#include "fs.h"

#define COM1 0x3F8

#ifdef __aarch64__
#define PL011_UARTDR ((volatile uint32_t *)0x09000000)
#define PL011_UARTFR ((volatile uint32_t *)0x09000018)
#else
static volatile uint16_t *const VGA_BUF = (uint16_t *)0xB8000;
static uint32_t vga_row = 0;
static uint32_t vga_col = 0;
static uint8_t  vga_attr = 0x07;
static int      ansi_state = 0;
static int      ansi_arg1 = 0;
static int      ansi_arg2 = 0;
static int      ansi_has_arg2 = 0;

/* Decodificador UTF-8 -> CP437 para renderizado en pantalla VGA */
static uint32_t vga_utf8_state = 0;
static uint32_t vga_utf8_cp = 0;

static uint8_t utf8_to_cp437(uint32_t cp)
{
    switch (cp) {
        case 0x00E1: return 0xA0; /* á */
        case 0x00E9: return 0x82; /* é */
        case 0x00ED: return 0xA1; /* í */
        case 0x00F3: return 0xA2; /* ó */
        case 0x00FA: return 0xA3; /* ú */
        case 0x00C1: return 'A';  /* Á */
        case 0x00C9: return 0x90; /* É */
        case 0x00CD: return 'I';  /* Í */
        case 0x00D3: return 'O';  /* Ó */
        case 0x00DA: return 'U';  /* Ú */
        case 0x00F1: return 0xA4; /* ñ */
        case 0x00D1: return 0xA5; /* Ñ */
        case 0x00BF: return 0xA8; /* ¿ */
        case 0x00A1: return 0xAD; /* ¡ */
        case 0x00FC: return 0x81; /* ü */
        case 0x00DC: return 0x9A; /* Ü */
        default:     return (cp < 128) ? (uint8_t)cp : '?';
    }
}

static void vga_update_cursor(void)
{
    uint16_t pos = (uint16_t)(vga_row * 80 + vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void)
{
    while (vga_row >= 25) {
        for (int i = 0; i < 24 * 80; ++i) {
            VGA_BUF[i] = VGA_BUF[i + 80];
        }
        for (int i = 24 * 80; i < 25 * 80; ++i) {
            VGA_BUF[i] = 0x0720;
        }
        vga_row--;
    }
}

static void vga_putc(char c)
{
    /* Manejo de secuencias multibyte UTF-8 */
    uint8_t uc = (uint8_t)c;
    if (uc >= 0x80) {
        if ((uc & 0xE0) == 0xC0) {
            vga_utf8_cp = uc & 0x1F;
            vga_utf8_state = 1;
            return;
        } else if ((uc & 0xF0) == 0xE0) {
            vga_utf8_cp = uc & 0x0F;
            vga_utf8_state = 2;
            return;
        } else if ((uc & 0xC0) == 0x80 && vga_utf8_state > 0) {
            vga_utf8_cp = (vga_utf8_cp << 6) | (uc & 0x3F);
            vga_utf8_state--;
            if (vga_utf8_state == 0) {
                c = (char)utf8_to_cp437(vga_utf8_cp);
            } else {
                return;
            }
        } else {
            vga_utf8_state = 0;
            return;
        }
    } else {
        vga_utf8_state = 0;
    }

    if (ansi_state == 0) {
        if ((uint8_t)c == 27) {
            ansi_state = 1;
            return;
        }
    } else if (ansi_state == 1) {
        if (c == '[') {
            ansi_state = 2;
            ansi_arg1 = 0;
            ansi_arg2 = 0;
            ansi_has_arg2 = 0;
            return;
        }
        ansi_state = 0;
    } else if (ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            if (!ansi_has_arg2) {
                ansi_arg1 = ansi_arg1 * 10 + (c - '0');
            } else {
                ansi_arg2 = ansi_arg2 * 10 + (c - '0');
            }
            return;
        } else if (c == ';') {
            ansi_has_arg2 = 1;
            return;
        } else if (c == 'm') {
            int bold = (ansi_arg1 == 1 || (ansi_has_arg2 && ansi_arg2 == 1));
            int code = (ansi_arg1 == 1 && ansi_has_arg2) ? ansi_arg2 : ansi_arg1;

            if (ansi_arg1 == 0 && !ansi_has_arg2) {
                vga_attr = 0x07;
            } else {
                uint8_t fg = 0x07;
                if (code == 30) fg = 0x00;
                else if (code == 31) fg = 0x04;
                else if (code == 32) fg = 0x02;
                else if (code == 33) fg = 0x06;
                else if (code == 34) fg = 0x01;
                else if (code == 35) fg = 0x05;
                else if (code == 36) fg = 0x03;
                else if (code == 37) fg = 0x07;

                if (bold) fg |= 0x08;
                vga_attr = fg;
            }
            ansi_state = 0;
            return;
        } else {
            ansi_state = 0;
            return;
        }
    }

    if (c == '\r') {
        vga_col = 0;
    } else if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            VGA_BUF[vga_row * 80 + vga_col] = (uint16_t)((vga_attr << 8) | ' ');
        }
    } else if ((uint8_t)c >= 32) {
        VGA_BUF[vga_row * 80 + vga_col] = (uint16_t)((vga_attr << 8) | (uint8_t)c);
        vga_col++;
        if (vga_col >= 80) {
            vga_col = 0;
            vga_row++;
        }
    }
    vga_scroll();
    vga_update_cursor();
}
#endif

void console_clear(void)
{
#ifdef __x86_64__
    for (uint32_t i = 0; i < 80 * 25; ++i) {
        VGA_BUF[i] = 0x0720;
    }
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
#else
    kprint("\033[2J\033[H");
#endif
}

void kputc(char c)
{
    if (console_muted) return;
#ifdef __x86_64__
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, (uint8_t)c);
    vga_putc(c);
#else
    while (*PL011_UARTFR & (1 << 5)) {}
    *PL011_UARTDR = (uint32_t)(uint8_t)c;
    rpi_fb_putc(c);
#endif
}

void kprint(const char *s)
{
    for (; *s; ++s) {
        kputc(*s);
    }
}

static void put_nibble(uint8_t v)
{
    v &= 0x0F;
    kputc(v < 10 ? (char)('0' + v) : (char)('A' + v - 10));
}

void kprint_hex8(uint8_t value)
{
    put_nibble((uint8_t)(value >> 4));
    put_nibble(value);
}

void kprint_hex16(uint16_t value)
{
    kprint_hex8((uint8_t)(value >> 8));
    kprint_hex8((uint8_t)value);
}

void kprint_hex32(uint32_t value)
{
    kprint_hex16((uint16_t)(value >> 16));
    kprint_hex16((uint16_t)value);
}

void kprint_dec(uint32_t value)
{
    char tmp[10];
    int n = 0;

    if (value == 0) {
        kputc('0');
        return;
    }

    while (value != 0) {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (n > 0) {
        kputc(tmp[--n]);
    }
}

void kprint_mac(const uint8_t *mac)
{
    for (int i = 0; i < 6; ++i) {
        kprint_hex8(mac[i]);
        if (i != 5) kputc(':');
    }
}

void kprint_ip(const uint8_t *ip)
{
    for (int i = 0; i < 4; ++i) {
        kprint_dec(ip[i]);
        if (i != 3) kputc('.');
    }
}

#ifdef __x86_64__
#define KBD_BUF_SIZE 128
static volatile char kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;
static int shift_active = 0;
static int caps_active = 0;
static int ctrl_active = 0;
static int e0_prefix = 0;
static int kbd_dead_key = 0; /* 0: ninguna, 1: agudo (´ / '), 2: dieresis (¨ / "), 3: tilde (~) */

static void kbd_push_byte(char ch)
{
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_buf[kbd_head] = ch;
        kbd_head = next;
    }
}

static void kbd_push_utf8_char(char base, int accent)
{
    /* accent: 1 = agudo (á,é,í,ó,ú), 2 = dieresis (ü,Ü), 3 = tilde (ñ,Ñ) */
    if (accent == 1) {
        switch (base) {
            case 'a': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xA1); return;
            case 'e': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xA9); return;
            case 'i': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xAD); return;
            case 'o': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xB3); return;
            case 'u': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xBA); return;
            case 'A': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x81); return;
            case 'E': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x89); return;
            case 'I': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x8D); return;
            case 'O': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x93); return;
            case 'U': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x9A); return;
            default:  kbd_push_byte((char)0x27); kbd_push_byte(base); return;
        }
    } else if (accent == 2) {
        switch (base) {
            case 'u': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xBC); return;
            case 'U': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x9C); return;
            default:  kbd_push_byte((char)0x22); kbd_push_byte(base); return;
        }
    } else if (accent == 3) {
        switch (base) {
            case 'n': kbd_push_byte((char)0xC3); kbd_push_byte((char)0xB1); return;
            case 'N': kbd_push_byte((char)0xC3); kbd_push_byte((char)0x91); return;
            default:  kbd_push_byte((char)0x7E); kbd_push_byte(base); return;
        }
    }
    kbd_push_byte(base);
}


static const char kbd_us_lower[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\r',
    0, /* Ctrl */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, /* LShift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, /* RShift */
    '*', 0, /* Alt */ ' ', 0 /* CapsLock */
};

static const char kbd_us_upper[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\r',
    0, /* Ctrl */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, /* LShift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, /* RShift */
    '*', 0, /* Alt */ ' ', 0 /* CapsLock */
};

void keyboard_irq_handler(void)
{
    uint8_t status = inb(0x64);
    if (status & 0x20) {
        inb(0x60); /* Descartar byte espurio del ratón para no desincronizar */
        return;
    }
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) {
        e0_prefix = 1;
        return;
    }
    if (e0_prefix) {
        e0_prefix = 0;
        char k = 0;
        if (sc == 0x48) k = 16;
        else if (sc == 0x50) k = 14;
        else if (sc == 0x4B) k = 2;
        else if (sc == 0x4D) k = 6;
        else if (sc == 0x47) k = 1;
        else if (sc == 0x4F) k = 5;
        if (k != 0) {
            uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
            if (next != kbd_tail) {
                kbd_buf[kbd_head] = k;
                kbd_head = next;
            }
        }
        return;
    }
    if (sc == 0x1D) {
        ctrl_active = 1;
        return;
    }
    if (sc == 0x9D) {
        ctrl_active = 0;
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        shift_active = 1;
        return;
    }
    if (sc == 0xAA || sc == 0xB6) {
        shift_active = 0;
        return;
    }
    if (sc == 0x3A) {
        caps_active = !caps_active;
        return;
    }
    if (sc & 0x80) {
        return;
    }
    if (sc < sizeof(kbd_us_lower)) {
        /* Soporte de teclado español estándar & dead-keys */
        if (!ctrl_active) {
            /* Tecla Ñ / ñ (scancode 0x27) */
            if (sc == 0x27) {
                kbd_dead_key = 0;
                if (shift_active ^ caps_active) {
                    kbd_push_byte((char)0xC3); kbd_push_byte((char)0x91); /* Ñ */
                } else {
                    kbd_push_byte((char)0xC3); kbd_push_byte((char)0xB1); /* ñ */
                }
                return;
            }

            /* Tecla Acento agudo / Diéresis (scancode 0x1A: junto a la P) */
            if (sc == 0x1A) {
                if (shift_active) kbd_dead_key = 2; /* Diéresis ¨ */
                else              kbd_dead_key = 1; /* Acento agudo ´ */
                return;
            }

            /* Signos ¡ y ¿ (scancode 0x0D en teclado ES) */
            if (sc == 0x0D) {
                kbd_dead_key = 0;
                if (shift_active) {
                    kbd_push_byte((char)0xC2); kbd_push_byte((char)0xBF); /* ¿ */
                } else {
                    kbd_push_byte((char)0xC2); kbd_push_byte((char)0xA1); /* ¡ */
                }
                return;
            }

            /* Shift + , -> ;  y  Shift + . -> : en layout ES */
            if (shift_active && sc == 0x33) { kbd_dead_key = 0; kbd_push_byte(';'); return; }
            if (shift_active && sc == 0x34) { kbd_dead_key = 0; kbd_push_byte(':'); return; }
        }

        char ch = shift_active ? kbd_us_upper[sc] : kbd_us_lower[sc];
        if (ctrl_active) {
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 1);
            else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 1);
            kbd_dead_key = 0;
            if (ch != 0) kbd_push_byte(ch);
            return;
        }

        if (caps_active) {
            if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
            else if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
        }

        if (ch != 0) {
            /* Dead-keys alternativas (layout US-International: ' y ~) */
            if (sc == 0x28 && !shift_active && kbd_dead_key == 0) {
                kbd_dead_key = 1;
                return;
            }
            if (sc == 0x29 && shift_active && kbd_dead_key == 0) {
                kbd_dead_key = 3;
                return;
            }

            if (kbd_dead_key != 0) {
                int dk = kbd_dead_key;
                kbd_dead_key = 0;
                if (ch == ' ') {
                    if (dk == 1)      kbd_push_byte((char)0x27);
                    else if (dk == 2) kbd_push_byte((char)0x22);
                    else if (dk == 3) kbd_push_byte((char)0x7E);
                } else {
                    kbd_push_utf8_char(ch, dk);
                }
            } else {
                kbd_push_byte(ch);
            }
        }
    }
}
#endif

void keyboard_reset_state(void)
{
#ifdef __x86_64__
    shift_active = 0;
    caps_active = 0;
    ctrl_active = 0;
    e0_prefix = 0;
    kbd_head = 0;
    kbd_tail = 0;
    kbd_dead_key = 0;
#endif
}

int kgetc_ready(void)
{
#ifdef __x86_64__
    if (kbd_head != kbd_tail) {
        return 1;
    }
    return (inb(COM1 + 5) & 0x01) != 0;
#else
    return !(*PL011_UARTFR & (1 << 4));
#endif
}

char kgetc(void)
{
    while (!kgetc_ready()) {
        thread_yield();
        arch_pause();
    }
#ifdef __x86_64__
    if (kbd_head != kbd_tail) {
        char ch = kbd_buf[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        return ch;
    }
    return (char)inb(COM1);
#else
    return (char)(*PL011_UARTDR & 0xFF);
#endif
}

#define CMD_HISTORY_MAX 16
/* CMD_LINE_MAX definido en console.h (4096) */

static char cmd_history[CMD_HISTORY_MAX][CMD_LINE_MAX];
static int history_count = 0;
static int history_tail = 0;

static void history_add(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') return;

    if (history_count > 0) {
        int last_idx = (history_tail - 1 + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
        int same = 1;
        for (int i = 0; cmd[i] || cmd_history[last_idx][i]; ++i) {
            if (cmd[i] != cmd_history[last_idx][i]) { same = 0; break; }
        }
        if (same) return;
    }

    int idx = history_tail;
    uint32_t i = 0;
    while (cmd[i] && i < CMD_LINE_MAX - 1) {
        cmd_history[idx][i] = cmd[i];
        i++;
    }
    cmd_history[idx][i] = '\0';

    history_tail = (history_tail + 1) % CMD_HISTORY_MAX;
    if (history_count < CMD_HISTORY_MAX) history_count++;
    console_history_sync_to_vfs();
}

#define KEY_ACT_UP    1001
#define KEY_ACT_DOWN  1002
#define KEY_ACT_LEFT  1003
#define KEY_ACT_RIGHT 1004
#define KEY_ACT_HOME  1005
#define KEY_ACT_END   1006
#define KEY_ACT_TAB   '\t'
#define KEY_ACT_ENTER '\n'
#define KEY_ACT_BS    '\b'

static int read_key_action(void)
{
    char c = kgetc();
    if (c == 16) return KEY_ACT_UP;
    if (c == 14) return KEY_ACT_DOWN;
    if (c == 2)  return KEY_ACT_LEFT;
    if (c == 6)  return KEY_ACT_RIGHT;
    if (c == 1)  return KEY_ACT_HOME;
    if (c == 5)  return KEY_ACT_END;

    if (c == 27) {
        if (kgetc_ready()) {
            char c2 = kgetc();
            if (c2 == '[') {
                char c3 = kgetc();
                if (c3 == 'A') return KEY_ACT_UP;
                if (c3 == 'B') return KEY_ACT_DOWN;
                if (c3 == 'C') return KEY_ACT_RIGHT;
                if (c3 == 'D') return KEY_ACT_LEFT;
                if (c3 == 'H') return KEY_ACT_HOME;
                if (c3 == 'F') return KEY_ACT_END;
                if (c3 == '1' || c3 == '4') {
                    if (kgetc_ready()) {
                        char c4 = kgetc();
                        if (c4 == '~') {
                            if (c3 == '1') return KEY_ACT_HOME;
                            if (c3 == '4') return KEY_ACT_END;
                        }
                    }
                }
            }
        }
        return 27;
    }

    if (c == '\r') return KEY_ACT_ENTER;
    if (c == 0x7F || c == 8) return KEY_ACT_BS;

    return (int)(uint8_t)c;
}

static uint32_t utf8_vis_len(const char *s, uint32_t byte_len)
{
    uint32_t vis = 0;
    for (uint32_t i = 0; i < byte_len; ++i) {
        if (((uint8_t)s[i] & 0xC0) != 0x80) {
            vis++;
        }
    }
    return vis;
}

static void redraw_prompt_line(const char *line, uint32_t len, uint32_t cursor, uint32_t prev_len)
{
    kputc('\r');
    kprint("\033[1;36msoma\033[1;32m>\033[0m ");

    for (uint32_t i = 0; i < len; ++i) {
        kputc(line[i]);
    }

    uint32_t vis_len = utf8_vis_len(line, len);
    uint32_t vis_prev = utf8_vis_len(line, prev_len);
    uint32_t max_v = vis_len;
    if (vis_prev > vis_len) {
        for (uint32_t i = vis_len; i < vis_prev; ++i) kputc(' ');
        max_v = vis_prev;
    }

    uint32_t vis_cursor = utf8_vis_len(line, cursor);
    uint32_t steps_back = (max_v > vis_cursor) ? (max_v - vis_cursor) : 0;
    if (steps_back > 0) {
#ifdef __x86_64__
        if (vga_col >= steps_back) vga_col -= steps_back;
        else vga_col = 0;
        vga_update_cursor();
#endif
        for (uint32_t i = 0; i < steps_back; ++i) {
            kputc('\b');
        }
    }
}

int kgetline(char *buf, uint32_t max)
{
    if (!buf || max == 0) return 0;

    char line[CMD_LINE_MAX];
    char temp_line[CMD_LINE_MAX];
    uint32_t len = 0;
    uint32_t cursor = 0;
    uint32_t prev_len = 0;
    int hist_idx = -1;

    line[0] = '\0';
    temp_line[0] = '\0';

    for (;;) {
        int act = read_key_action();

        if (act == KEY_ACT_ENTER) {
            kputc('\r');
            kputc('\n');
            line[len] = '\0';
            history_add(line);

            uint32_t copy_n = (len < max - 1) ? len : max - 1;
            for (uint32_t i = 0; i < copy_n; ++i) buf[i] = line[i];
            buf[copy_n] = '\0';
            return (int)copy_n;
        }

        if (act == KEY_ACT_BS) {
            if (cursor > 0) {
                prev_len = len;
                uint32_t del_bytes = 1;
                /* Manejo de borrado atómico para caracteres UTF-8 multibyte */
                if (((uint8_t)line[cursor - 1] & 0xC0) == 0x80) {
                    while (cursor > del_bytes && ((uint8_t)line[cursor - del_bytes] & 0xC0) == 0x80) {
                        del_bytes++;
                    }
                }
                for (uint32_t i = cursor - del_bytes; i + del_bytes <= len; ++i) {
                    line[i] = line[i + del_bytes];
                }
                cursor -= del_bytes;
                len -= del_bytes;
                line[len] = '\0';
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_LEFT) {
            if (cursor > 0) {
                cursor--;
                while (cursor > 0 && ((uint8_t)line[cursor] & 0xC0) == 0x80) cursor--;
                prev_len = len;
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_RIGHT) {
            if (cursor < len) {
                cursor++;
                while (cursor < len && ((uint8_t)line[cursor] & 0xC0) == 0x80) cursor++;
                prev_len = len;
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_HOME) {
            if (cursor > 0) {
                cursor = 0;
                prev_len = len;
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_END) {
            if (cursor < len) {
                cursor = len;
                prev_len = len;
                redraw_prompt_line(line, len, cursor, prev_len);
            }
            continue;
        }

        if (act == KEY_ACT_UP) {
            if (history_count == 0) continue;
            prev_len = len;

            if (hist_idx == -1) {
                for (uint32_t i = 0; i <= len; ++i) temp_line[i] = line[i];
                hist_idx = history_count - 1;
            } else if (hist_idx > 0) {
                hist_idx--;
            }

            int start_idx = (history_tail - history_count + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
            int target_idx = (start_idx + hist_idx) % CMD_HISTORY_MAX;
            len = 0;
            while (cmd_history[target_idx][len] && len < sizeof(line) - 1) {
                line[len] = cmd_history[target_idx][len];
                len++;
            }
            line[len] = '\0';
            cursor = len;
            redraw_prompt_line(line, len, cursor, prev_len);
            continue;
        }

        if (act == KEY_ACT_DOWN) {
            if (hist_idx == -1) continue;
            prev_len = len;

            if (hist_idx < history_count - 1) {
                hist_idx++;
                int start_idx = (history_tail - history_count + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
                int target_idx = (start_idx + hist_idx) % CMD_HISTORY_MAX;
                len = 0;
                while (cmd_history[target_idx][len] && len < sizeof(line) - 1) {
                    line[len] = cmd_history[target_idx][len];
                    len++;
                }
                line[len] = '\0';
                cursor = len;
            } else {
                hist_idx = -1;
                len = 0;
                while (temp_line[len] && len < sizeof(line) - 1) {
                    line[len] = temp_line[len];
                    len++;
                }
                line[len] = '\0';
                cursor = len;
            }
            redraw_prompt_line(line, len, cursor, prev_len);
            continue;
        }

        if (act == KEY_ACT_TAB) {
            int has_space = 0;
            for (uint32_t i = 0; i < cursor; ++i) {
                if (line[i] == ' ') { has_space = 1; break; }
            }
            if (!has_space) {
                const char *matches[16];
                int match_count = shell_autocomplete(line, cursor, matches, 16);

                if (match_count == 1) {
                    prev_len = len;
                    const char *m = matches[0];
                    len = 0;
                    while (m[len] && len < sizeof(line) - 2) {
                        line[len] = m[len];
                        len++;
                    }
                    line[len++] = ' ';
                    line[len] = '\0';
                    cursor = len;
                    redraw_prompt_line(line, len, cursor, prev_len);
                } else if (match_count > 1) {
                    kprint("\n");
                    for (int m = 0; m < match_count; ++m) {
                        kprint("  ");
                        kprint(matches[m]);
                    }
                    kprint("\n");
                    prev_len = len;
                    redraw_prompt_line(line, len, cursor, prev_len);
                }
            }
            continue;
        }

        /* Admisión de caracteres ASCII y bytes UTF-8 (>= 32) */
        if ((uint8_t)act >= 32) {
            if (len < sizeof(line) - 2) {
                if (cursor == len) {
                    line[len] = (char)act;
                    len++;
                    cursor++;
                    line[len] = '\0';
                    kputc((char)act);
                } else {
                    prev_len = len;
                    for (uint32_t i = len + 1; i > cursor; --i) {
                        line[i] = line[i - 1];
                    }
                    line[cursor] = (char)act;
                    len++;
                    cursor++;
                    line[len] = '\0';
                    redraw_prompt_line(line, len, cursor, prev_len);
                }
            }
        }
    }
}

int console_history_dump(char *out_buf, uint32_t max_out)
{
    uint32_t pos = 0;
    kprint("\nHISTORIAL DE COMANDOS:\n----------------------\n");
    for (int i = 0; i < history_count; ++i) {
        int idx = (history_tail - history_count + i + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
        kprint("  ");
        kprint_dec((uint32_t)(i + 1));
        kprint("  ");
        kprint(cmd_history[idx]);
        kprint("\n");

        if (out_buf && pos < max_out - 64) {
            char tb[10]; int tn = 0; uint32_t v = (uint32_t)(i + 1);
            while (v) { tb[tn++] = (char)('0' + v % 10); v /= 10; }
            while (tn > 0) out_buf[pos++] = tb[--tn];
            out_buf[pos++] = ' '; out_buf[pos++] = ' ';
            const char *p = cmd_history[idx];
            while (*p && pos < max_out - 2) out_buf[pos++] = *p++;
            out_buf[pos++] = '\n';
        }
    }
    if (out_buf) out_buf[pos] = '\0';
    kprint("----------------------\n");
    return 1;
}

void console_history_sync_to_vfs(void)
{
    static char hist_buf[2048];
    uint32_t pos = 0;
    for (int i = 0; i < history_count; ++i) {
        int idx = (history_tail - history_count + i + CMD_HISTORY_MAX) % CMD_HISTORY_MAX;
        const char *p = cmd_history[idx];
        while (*p && pos < sizeof(hist_buf) - 2) hist_buf[pos++] = *p++;
        hist_buf[pos++] = '\n';
    }
    if (pos > 0) {
        vfs_write("/etc/history.txt", hist_buf, pos);
    }
}

void console_history_load_from_vfs(void)
{
    static char load_buf[2048];
    int r = vfs_read("/etc/history.txt", load_buf, sizeof(load_buf) - 1);
    if (r <= 0) return;
    load_buf[r] = '\0';

    const char *p = load_buf;
    while (*p) {
        while (*p == '\r' || *p == '\n') p++;
        if (*p == '\0') break;

        char line[CMD_LINE_MAX];
        uint32_t lp = 0;
        while (*p && *p != '\n' && *p != '\r' && lp < sizeof(line) - 1) {
            line[lp++] = *p++;
        }
        line[lp] = '\0';
        if (lp > 0) {
            history_add(line);
        }
    }
}
