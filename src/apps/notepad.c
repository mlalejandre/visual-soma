#include "notepad.h"
#include "fb.h"
#include "gui_font.h"
#include "fs.h"
#include "console.h"
#include "mem.h"

#define NP_MAX_SIZE 16384
static char np_text[NP_MAX_SIZE];
static uint32_t np_len = 0;
static uint32_t np_cursor = 0;
static char np_path[64] = "/workspace/notas.txt";
static int np_scroll = 0;
static char np_status[64] = "Listo";
static int np_dirty = 0;

/* SOMA_PARCHE_LIVE_V1 */
static int np_follow = 0;               /* auto-scroll hacia el cursor */
static int np_live = 0;                 /* volcado en directo activo */
static const char *np_live_src = 0;
static uint32_t np_live_len = 0;
static uint32_t np_live_pos = 0;

static void str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void notepad_init(void) {
    np_follow = 0; np_live = 0;
    np_len = 0; np_cursor = 0; np_scroll = 0; np_dirty = 0;
    np_text[0] = '\0';
    str_copy(np_status, "Nuevo documento", sizeof(np_status));
}

void notepad_load_file(const char *path) {
    np_follow = 0; np_live = 0;
    str_copy(np_path, path, sizeof(np_path));
    int r = vfs_read(np_path, np_text, NP_MAX_SIZE - 1);
    if (r >= 0) {
        np_len = r;
        np_text[np_len] = '\0';
        np_cursor = np_len;
        np_scroll = 0;
        np_dirty = 0;
        str_copy(np_status, "Archivo cargado", sizeof(np_status));
    } else {
        np_len = 0; np_cursor = 0; np_text[0] = '\0'; np_dirty = 0;
        str_copy(np_status, "Archivo no encontrado. Creando nuevo.", sizeof(np_status));
    }
}

/* ---- SOMA_PARCHE_LIVE_V1: volcado en directo de archivos generados por SOMA ---- */
static int np_same_path(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

void notepad_live_begin(const char *path, const char *data, uint32_t len)
{
    uint32_t common = 0;

    if (!path) path = "/workspace/notas.txt";
    if (!data) { data = ""; len = 0; }
    if (len > NP_MAX_SIZE - 2) len = NP_MAX_SIZE - 2;

    if (np_same_path(np_path, path)) {
        /* Mismo archivo: conservar el prefijo comun y escribir solo la diferencia */
        while (common < np_len && common < len && np_text[common] == data[common]) common++;
    } else {
        np_scroll = 0;
    }

    str_copy(np_path, path, sizeof(np_path));
    np_len = common;
    np_text[np_len] = '\0';
    np_cursor = np_len;

    np_live_src = data;
    np_live_len = len;
    np_live_pos = common;
    np_live = 1;
    np_follow = 1;
    np_dirty = 0;
    str_copy(np_status, "SOMA escribiendo en vivo...", sizeof(np_status));
}

uint32_t notepad_live_step(uint32_t chunk)
{
    if (!np_live || !np_live_src) return 0;
    if (chunk == 0) chunk = 1;

    while (chunk > 0 && np_live_pos < np_live_len && np_len < NP_MAX_SIZE - 2) {
        char ch = np_live_src[np_live_pos++];
        chunk--;
        if (ch == '\r') continue;
        np_text[np_len++] = ch;
    }
    np_text[np_len] = '\0';
    np_cursor = np_len;
    np_follow = 1;

    if (np_len >= NP_MAX_SIZE - 2) np_live_pos = np_live_len;
    return np_live_len - np_live_pos;
}

void notepad_live_end(void)
{
    np_live = 0;
    np_live_src = 0;
    np_cursor = np_len;
    np_dirty = 0;
    str_copy(np_status, "Archivo generado por SOMA (en vivo)", sizeof(np_status));
}

void notepad_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor) {
    fb_fill_rect(win_x, win_y, win_w, win_h, 0x000F172A);
    fb_draw_rect(win_x, win_y, win_w, win_h, 0x00D946EF);

    fb_fill_rect(win_x, win_y, win_w, 36, 0x001E293B);
    fb_draw_rect(win_x, win_y, win_w, 36, 0x00334155);
    font_draw_string(win_x + 16, win_y + 14, "SOMA Notepad :: Editor de Texto", 0x00F8FAFC, 1);

    uint32_t btn_save_x = win_x + win_w - 110;
    fb_fill_rounded_rect(btn_save_x, win_y + 4, 96, 28, 4, np_dirty ? 0x000284C7 : 0x00334155);
    fb_draw_rounded_rect(btn_save_x, win_y + 4, 96, 28, 4, np_dirty ? 0x0038BDF8 : 0x00475569);
    font_draw_string(btn_save_x + 20, win_y + 14, "GUARDAR", 0x00FFFFFF, 1);

    font_draw_string(win_x + 300, win_y + 14, np_path, 0x00D946EF, 1);

    uint32_t text_x = win_x + 12;
    uint32_t text_y = win_y + 48;
    uint32_t text_w = win_w - 24;
    uint32_t text_h = win_h - 90;

    /* SOMA_PARCHE_LIVE_V1: auto-scroll para mantener visible el cursor */
    if (np_follow) {
        uint32_t cols = (text_w > 32) ? (text_w - 24 + 7) / 8 : 1;
        uint32_t col = 0, line = 0;
        for (uint32_t i = 0; i < np_cursor && i < np_len; ++i) {
            if (np_text[i] == '\n') { line++; col = 0; }
            else if (++col >= cols) { line++; col = 0; }
        }
        int cy = (int)(line * 16u);
        int view_h = (int)text_h - 24;
        if (view_h < 16) view_h = 16;
        if (cy < np_scroll) np_scroll = cy;
        else if (cy + 16 > np_scroll + view_h) np_scroll = cy + 16 - view_h;
        if (np_scroll < 0) np_scroll = 0;
    }

    fb_fill_rect(text_x, text_y, text_w, text_h, 0x00020617);
    fb_draw_rect(text_x, text_y, text_w, text_h, 0x00334155);

    fb_set_clip((int32_t)text_x, (int32_t)text_y, (int32_t)text_w, (int32_t)text_h);

    uint32_t cur_line = 0;
    uint32_t dr_y = text_y + 8 - np_scroll;
    uint32_t dr_x = text_x + 8;

    for (uint32_t i = 0; i <= np_len; ++i) {
        if (i == np_cursor && show_cursor) {
            fb_fill_rect(dr_x, dr_y, 8, 14, 0x00D946EF);
        }
        if (i == np_len) break;
        char c = np_text[i];
        if (c == '\n') {
            dr_y += 16;
            dr_x = text_x + 8;
            cur_line++;
        } else {
            if (dr_y + 16 >= text_y && dr_y < text_y + text_h) {
                char ch[2] = {c, '\0'};
                font_draw_string(dr_x, dr_y, ch, 0x00E2E8F0, 1);
            }
            dr_x += 8;
            if (dr_x >= text_x + text_w - 16) {
                dr_y += 16;
                dr_x = text_x + 8;
                cur_line++;
            }
        }
    }
    fb_reset_clip();

    uint32_t st_y = win_y + win_h - 36;
    fb_fill_rect(win_x, st_y, win_w, 36, 0x001E293B);
    fb_draw_rect(win_x, st_y, win_w, 36, 0x00334155);
    font_draw_string(win_x + 16, st_y + 14, np_status, 0x0034D399, 1);

    char pos_str[64]; uint32_t pp = 0;
    const char *lbl = "Tamano: "; while (*lbl) pos_str[pp++] = *lbl++;
    uint32_t v = np_len; char tb[10]; int tn = 0;
    if (v == 0) pos_str[pp++] = '0';
    else { while (v) { tb[tn++] = '0' + (v % 10); v /= 10; } while (tn) pos_str[pp++] = tb[--tn]; }
    const char *lbl2 = " B"; while (*lbl2) pos_str[pp++] = *lbl2++;
    pos_str[pp] = '\0';
    font_draw_string(win_x + win_w - 140, st_y + 14, pos_str, 0x0094A3B8, 1);
}

int notepad_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h) {
    (void)win_h;
    uint32_t btn_save_x = win_x + win_w - 110;
    if (mx >= (int32_t)btn_save_x && mx <= (int32_t)(btn_save_x + 96) &&
        my >= (int32_t)(win_y + 4) && my <= (int32_t)(win_y + 32)) {
        if (vfs_write(np_path, np_text, np_len) >= 0) {
            str_copy(np_status, "Guardado con exito", sizeof(np_status));
            np_dirty = 0;
        } else {
            str_copy(np_status, "Error al guardar", sizeof(np_status));
        }
        return 0;
    }

    uint32_t text_y = win_y + 48;
    if (mx > (int32_t)win_x && my > (int32_t)text_y) {
        np_follow = 0;
        if (my < (int32_t)(text_y + 40) && np_scroll > 0) np_scroll -= 32;
        if (my > (int32_t)(win_y + win_h - 80)) np_scroll += 32;
        if (np_scroll < 0) np_scroll = 0;
    }
    return 0;
}

void notepad_handle_key(char c) {
    np_follow = 1;
    if (c == 2) {
        if (np_cursor > 0) np_cursor--;
    } else if (c == 6) {
        if (np_cursor < np_len) np_cursor++;
    } else if (c == '\b' || c == 127) {
        if (np_cursor > 0) {
            for (uint32_t i = np_cursor - 1; i < np_len; ++i) np_text[i] = np_text[i + 1];
            np_len--; np_cursor--; np_dirty = 1;
            str_copy(np_status, "Editando...", sizeof(np_status));
        }
    } else if (c == '\r' || c == '\n') {
        if (np_len < NP_MAX_SIZE - 2) {
            for (uint32_t i = np_len; i > np_cursor; --i) np_text[i] = np_text[i - 1];
            np_text[np_cursor++] = '\n'; np_len++; np_text[np_len] = '\0'; np_dirty = 1;
            str_copy(np_status, "Editando...", sizeof(np_status));
        }
    } else if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
        if (np_len < NP_MAX_SIZE - 2) {
            for (uint32_t i = np_len; i > np_cursor; --i) np_text[i] = np_text[i - 1];
            np_text[np_cursor++] = c; np_len++; np_text[np_len] = '\0'; np_dirty = 1;
            str_copy(np_status, "Editando...", sizeof(np_status));
        }
    }
}
