#include "nano.h"
#include "console.h"
#include "fs.h"
#include "io.h"
#include <stdint.h>

#define NANO_MAX_SIZE 4096
static char nano_buf[NANO_MAX_SIZE];
#ifdef __aarch64__
static uint16_t arm_vga_scratch[80 * 25];
static volatile uint16_t *const VGA_MEM = arm_vga_scratch;
#else
static volatile uint16_t *const VGA_MEM = (uint16_t *)0xB8000;
#endif

static void nano_draw_bar(uint32_t row, const char *text, uint8_t attr)
{
    uint32_t col = 0;
    while (text[col] && col < 80) {
        VGA_MEM[row * 80 + col] = (uint16_t)((attr << 8) | (uint8_t)text[col]);
        col++;
    }
    while (col < 80) {
        VGA_MEM[row * 80 + col] = (uint16_t)((attr << 8) | ' ');
        col++;
    }
}

void run_nano(const char *filename)
{
    if (!filename || filename[0] == '\0') {
        kprint("Uso: nano <archivo>\n");
        return;
    }

    int r = vfs_read(filename, nano_buf, sizeof(nano_buf) - 1);
    uint32_t buf_len = (r > 0) ? (uint32_t)r : 0;
    nano_buf[buf_len] = '\0';

    uint32_t cursor_pos = 0;
    uint32_t top_line = 0;
    int dirty = 0;
    char status_msg[64];
    status_msg[0] = '\0';

    console_clear();

    for (;;) {
        uint32_t cur_line = 0;
        uint32_t cur_col = 0;
        for (uint32_t i = 0; i < cursor_pos; ++i) {
            if (nano_buf[i] == '\n') {
                cur_line++;
                cur_col = 0;
            } else {
                cur_col++;
            }
        }

        if (cur_line < top_line) top_line = cur_line;
        if (cur_line >= top_line + 22) top_line = cur_line - 21;

        char hdr[80]; uint32_t hp = 0;
        const char *h1 = " [ SOMA nano 0.2 ]  Archivo: "; while (*h1) hdr[hp++] = *h1++;
        const char *h2 = filename; while (*h2 && hp < 50) hdr[hp++] = *h2++;
        const char *h3 = dirty ? "  [Modificado]" : "  [Limpio]"; while (*h3 && hp < 79) hdr[hp++] = *h3++;
        hdr[hp] = '\0';
        nano_draw_bar(0, hdr, 0x70);

        uint32_t scan_l = 0;
        uint32_t p = 0;
        while (p < buf_len && scan_l < top_line) {
            if (nano_buf[p++] == '\n') scan_l++;
        }

        for (uint32_t row = 1; row <= 22; ++row) {
            uint32_t col = 0;
            if (p < buf_len && scan_l >= top_line) {
                while (p < buf_len && nano_buf[p] != '\n') {
                    if (col < 80) {
                        VGA_MEM[row * 80 + col] = (uint16_t)(0x0700 | (uint8_t)nano_buf[p]);
                        col++;
                    }
                    p++;
                }
                if (p < buf_len && nano_buf[p] == '\n') p++;
                scan_l++;
            }
            while (col < 80) {
                VGA_MEM[row * 80 + col] = 0x0720;
                col++;
            }
        }

        char st[80]; uint32_t sp = 0;
        if (status_msg[0]) {
            const char *s = status_msg; while (*s && sp < 79) st[sp++] = *s++;
        } else {
            const char *s = "Linea: "; while (*s) st[sp++] = *s++;
            uint32_t val = cur_line + 1; char tb[10]; int tn=0; while(val){tb[tn++]='0'+(val%10); val/=10;} while(tn) st[sp++]=tb[--tn];
            s = ", Col: "; while (*s) st[sp++] = *s++;
            val = cur_col + 1; tn=0; while(val){tb[tn++]='0'+(val%10); val/=10;} while(tn) st[sp++]=tb[--tn];
            s = " | Tamano: "; while (*s) st[sp++] = *s++;
            val = buf_len; if(val==0) st[sp++]='0'; else { tn=0; while(val){tb[tn++]='0'+(val%10); val/=10;} while(tn) st[sp++]=tb[--tn]; }
            s = " B"; while (*s) st[sp++] = *s++;
        }
        st[sp] = '\0';
        nano_draw_bar(23, st, 0x0F);

        nano_draw_bar(24, " ^S Guardar    ^X Salir    ^P/^N Arriba/Abajo    ^B/^F Izq/Der ", 0x70);

        uint32_t scr_row = 1 + (cur_line - top_line);
        uint32_t scr_col = (cur_col < 80) ? cur_col : 79;
        uint16_t cpos = (uint16_t)(scr_row * 80 + scr_col);
        outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(cpos & 0xFF));
        outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((cpos >> 8) & 0xFF));

        char ch = kgetc();
        status_msg[0] = '\0';

        if (ch == 24) { /* Ctrl+X */
            break;
        } else if (ch == 19) { /* Ctrl+S */
            if (vfs_write(filename, nano_buf, buf_len) >= 0) {
                dirty = 0;
                const char *ok = "[Guardado con exito en RamFS / virtio-blk]";
                uint32_t k = 0; while (*ok) status_msg[k++] = *ok++; status_msg[k] = '\0';
            } else {
                const char *err = "[Error de I/O al guardar archivo]";
                uint32_t k = 0; while (*err) status_msg[k++] = *err++; status_msg[k] = '\0';
            }
            continue;
        } else if (ch == 2) { /* Ctrl+B */
            if (cursor_pos > 0) cursor_pos--;
        } else if (ch == 6) { /* Ctrl+F */
            if (cursor_pos < buf_len) cursor_pos++;
        } else if (ch == 16) { /* Ctrl+P */
            if (cur_line > 0) {
                uint32_t target_col = cur_col;
                while (cursor_pos > 0 && nano_buf[cursor_pos - 1] != '\n') cursor_pos--;
                if (cursor_pos > 0) cursor_pos--;
                uint32_t line_start = cursor_pos;
                while (line_start > 0 && nano_buf[line_start - 1] != '\n') line_start--;
                uint32_t line_len = cursor_pos - line_start;
                cursor_pos = line_start + ((target_col < line_len) ? target_col : line_len);
            }
        } else if (ch == 14) { /* Ctrl+N */
            uint32_t target_col = cur_col;
            uint32_t next_line = cursor_pos;
            while (next_line < buf_len && nano_buf[next_line] != '\n') next_line++;
            if (next_line < buf_len) {
                next_line++;
                uint32_t line_len = 0;
                while (next_line + line_len < buf_len && nano_buf[next_line + line_len] != '\n') line_len++;
                cursor_pos = next_line + ((target_col < line_len) ? target_col : line_len);
            }
        } else if (ch == '\b' || ch == 127) {
            if (cursor_pos > 0) {
                for (uint32_t i = cursor_pos - 1; i < buf_len; ++i) {
                    nano_buf[i] = nano_buf[i + 1];
                }
                buf_len--;
                cursor_pos--;
                dirty = 1;
            }
        } else if (ch == '\r' || ch == '\n') {
            if (buf_len < NANO_MAX_SIZE - 2) {
                for (uint32_t i = buf_len + 1; i > cursor_pos; --i) {
                    nano_buf[i] = nano_buf[i - 1];
                }
                nano_buf[cursor_pos] = '\n';
                buf_len++;
                cursor_pos++;
                nano_buf[buf_len] = '\0';
                dirty = 1;
            }
        } else if ((uint8_t)ch >= 32 && (uint8_t)ch < 127) {
            if (buf_len < NANO_MAX_SIZE - 2) {
                for (uint32_t i = buf_len + 1; i > cursor_pos; --i) {
                    nano_buf[i] = nano_buf[i - 1];
                }
                nano_buf[cursor_pos] = ch;
                buf_len++;
                cursor_pos++;
                nano_buf[buf_len] = '\0';
                dirty = 1;
            }
        }
    }

    console_clear();
    kprint("[nano: sesion finalizada para '"); kprint(filename); kprint("']\n");
}
