#include "fm.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "console.h"
#include "fs.h"
#include "mem.h"

#define MAX_FM_FILES 48

struct fm_item {
    char name[64];
    uint32_t size;
    uint8_t is_dir;
    uint8_t is_img;
    uint8_t is_html;
    uint8_t is_txt;
};

static struct fm_item file_list[MAX_FM_FILES];
static int file_count = 0;
static int selected_idx = -1;

static char current_folder[64] = "/workspace";
static char preview_text[256] = "";

static void str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0; while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; } dst[i] = '\0';
}
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; } return (*a == *b);
}

/* SOMA_PARCHE_LIVE_V1: comparacion de extension sin distinguir mayusculas */
static int fm_ends_with_ci(const char *s, const char *suf)
{
    uint32_t sl = 0, xl = 0;
    while (s[sl]) sl++;
    while (suf[xl]) xl++;
    if (xl > sl) return 0;
    for (uint32_t i = 0; i < xl; ++i) {
        char a = s[sl - xl + i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

void fm_refresh(void) {
    file_count = 0; selected_idx = -1; preview_text[0] = '\0';
    static char buf[2048];
    int len = vfs_format_list_ext(1, current_folder, buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = '\0';

    const char *p = buf;
    if (p[0] == 'A' && p[1] == 'r') { while (*p && *p != ':') p++; if (*p == ':') p++; }

    while (*p && file_count < MAX_FM_FILES) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char fname[64]; uint32_t fp = 0;
        while (*p && *p != ',' && fp < sizeof(fname) - 1) fname[fp++] = *p++;
        fname[fp] = '\0';
        str_copy(file_list[file_count].name, fname, 64);

        static char tmp[64]; int sz = vfs_read(fname, tmp, sizeof(tmp) - 1);
        file_list[file_count].size = (sz >= 0) ? (uint32_t)sz : 0;

        uint32_t nl = 0; while (fname[nl]) nl++;
        file_list[file_count].is_img = fm_ends_with_ci(fname, ".bmp");
        file_list[file_count].is_html = (fm_ends_with_ci(fname, ".html") || fm_ends_with_ci(fname, ".htm"));
        file_list[file_count].is_txt = (nl > 4 && fname[nl - 4] == '.' && (fname[nl - 3] == 't' || fname[nl - 3] == 's')) ||
                                       (nl > 2 && fname[nl - 2] == '.' && (fname[nl - 1] == 'c' || fname[nl - 1] == 'h'));
        file_list[file_count].is_dir = 0;
        file_count++;
    }
}

void fm_init(void) { str_copy(current_folder, "/workspace", sizeof(current_folder)); fm_refresh(); }

void fm_open_folder(const char *folder) {
    if (folder && *folder) {
        str_copy(current_folder, folder, sizeof(current_folder));
    }
    fm_refresh();
}

void fm_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor) {
    (void)show_cursor;
    fb_fill_rect(win_x, win_y, win_w, win_h, 0x000F172A);
    fb_draw_rect(win_x, win_y, win_w, win_h, 0x0038BDF8);

    fb_fill_rect(win_x, win_y, win_w, 36, 0x001E293B);
    fb_draw_rect(win_x, win_y, win_w, 36, 0x00334155);
    font_draw_string(win_x + 16, win_y + 14, "SOMA File Manager :: Explorador de Archivos", 0x00F8FAFC, 1);

    uint32_t nav_y = win_y + 42, bar_h = 36;
    fb_fill_rect(win_x + 14, nav_y, win_w - 28, bar_h, 0x00020617);
    fb_draw_rect(win_x + 14, nav_y, win_w - 28, bar_h, 0x001E293B);
    font_draw_string(win_x + 24, nav_y + 14, "Ruta:", COLOR_CYAN, 1);
    font_draw_string(win_x + 74, nav_y + 14, current_folder, 0x00F8FAFC, 1);

    uint32_t b_ref_x = win_x + win_w - 120;
    fb_fill_rounded_rect(b_ref_x, nav_y + 4, 96, 28, 4, 0x001E293B);
    fb_draw_rounded_rect(b_ref_x, nav_y + 4, 96, 28, 4, 0x0010B981);
    font_draw_string(b_ref_x + 10, nav_y + 14, "Actualizar", 0x0010B981, 1);

    uint32_t side_w = 180, main_y = win_y + 86, main_h = win_h - 180;
    fb_fill_rect(win_x + 14, main_y, side_w, main_h, 0x00090D15);
    fb_draw_rect(win_x + 14, main_y, side_w, main_h, 0x001E293B);
    font_draw_string(win_x + 24, main_y + 14, "LUGARES", 0x0094A3B8, 1);

    const char *places[] = { "/workspace", "/mis_archivos", "/etc", "/proc", "/" };
    const char *labels[] = { "Workspace", "Mis Archivos", "Config (etc)", "Kernel (proc)", "Raiz (/)" };
    for (int i = 0; i < 5; ++i) {
        uint32_t py = main_y + 42 + (uint32_t)(i * 32);
        int is_cur = str_eq(current_folder, places[i]);
        if (is_cur) fb_fill_rect(win_x + 16, py - 6, side_w - 4, 26, 0x001E293B);
        font_draw_string(win_x + 24, py, labels[i], is_cur ? COLOR_CYAN : 0x00E2E8F0, 1);
    }

    uint32_t list_x = win_x + 14 + side_w + 10, list_w = win_w - 28 - side_w - 10;
    fb_fill_rect(list_x, main_y, list_w, main_h, 0x00030712);
    fb_draw_rect(list_x, main_y, list_w, main_h, 0x001F2937);

    fb_fill_rect(list_x + 2, main_y + 2, list_w - 4, 28, 0x000F172A);
    font_draw_string(list_x + 14, main_y + 12, "TIPO", 0x0094A3B8, 1);
    font_draw_string(list_x + 70, main_y + 12, "NOMBRE DEL ARCHIVO", 0x0094A3B8, 1);
    font_draw_string(list_x + list_w - 120, main_y + 12, "TAMANO", 0x0094A3B8, 1);

    fb_set_clip((int32_t)list_x, (int32_t)(main_y + 32), (int32_t)list_w, (int32_t)(main_h - 34));
    uint32_t row_y = main_y + 36;
    for (int i = 0; i < file_count; ++i) {
        if (i == selected_idx) fb_fill_rect(list_x + 4, row_y - 2, list_w - 8, 22, 0x001E293B);
        if (file_list[i].is_img) font_draw_string(list_x + 14, row_y + 4, "[IMG]", 0x00F59E0B, 1);
        else if (file_list[i].is_html) font_draw_string(list_x + 14, row_y + 4, "[WEB]", 0x0010B981, 1);
        else if (file_list[i].is_txt) font_draw_string(list_x + 14, row_y + 4, "[TXT]", 0x00D946EF, 1);
        else font_draw_string(list_x + 14, row_y + 4, "[DOC]", COLOR_CYAN, 1);

        font_draw_string(list_x + 70, row_y + 4, file_list[i].name, 0x00F8FAFC, 1);
        char sz_str[24]; uint32_t sp = 0, v = file_list[i].size; char tb[10]; int tn = 0;
        if (v == 0) sz_str[sp++] = '0'; else { while (v) { tb[tn++] = '0' + v % 10; v /= 10; } while (tn) sz_str[sp++] = tb[--tn]; }
        const char *b_lbl = " B"; while (*b_lbl) sz_str[sp++] = *b_lbl++; sz_str[sp] = '\0';
        font_draw_string(list_x + list_w - 120, row_y + 4, sz_str, 0x0094A3B8, 1);
        row_y += 24;
    }
    fb_reset_clip();

    uint32_t prev_y = win_y + win_h - 84;
    fb_fill_rect(win_x + 14, prev_y, win_w - 28, 44, 0x000F172A);
    fb_draw_rect(win_x + 14, prev_y, win_w - 28, 44, 0x001E293B);

    if (selected_idx >= 0 && selected_idx < file_count) {
        font_draw_string(win_x + 24, prev_y + 8, "Vista previa:", COLOR_CYAN, 1);
        font_draw_string(win_x + 140, prev_y + 8, file_list[selected_idx].name, 0x00F59E0B, 1);
        font_draw_string(win_x + 24, prev_y + 24, preview_text, 0x0094A3B8, 1);

        uint32_t btn_act_x = win_x + win_w - 140;
        fb_fill_rounded_rect(btn_act_x, prev_y + 8, 110, 28, 4, 0x000284C7);
        fb_draw_rounded_rect(btn_act_x, prev_y + 8, 110, 28, 4, 0x0038BDF8);
        font_draw_string(btn_act_x + 14, prev_y + 16, "ABRIR ARCHIVO", 0x00FFFFFF, 1);
    } else {
        font_draw_string(win_x + 24, prev_y + 16, "Selecciona un archivo.", 0x0064748B, 1);
    }

    uint32_t st_y = win_y + win_h - 36;
    fb_fill_rect(win_x, st_y, win_w, 36, 0x001E293B); fb_draw_rect(win_x, st_y, win_w, 36, 0x00334155);
    char st_buf[64]; uint32_t stp = 0; const char *st_lbl = "Archivos: "; while (*st_lbl) st_buf[stp++] = *st_lbl++;
    uint32_t vv = (uint32_t)file_count; char tbb[10]; int tnn = 0;
    if (vv == 0) st_buf[stp++] = '0'; else { while (vv) { tbb[tnn++] = '0' + vv % 10; vv /= 10; } while (tnn) st_buf[stp++] = tbb[--tnn]; }
    st_buf[stp] = '\0';
    font_draw_string(win_x + 16, st_y + 14, st_buf, 0x0010B981, 1);
}

/* SOMA_PARCHE_LIVE_V1: abre el archivo seleccionado en su herramienta.
 * 2 = Visor de Imagenes, 3 = Navegador, 4 = Notepad. */
static int fm_open_selected(char *out_path, uint32_t max_out)
{
    if (selected_idx < 0 || selected_idx >= file_count) return 0;
    str_copy(out_path, file_list[selected_idx].name, max_out);
    if (file_list[selected_idx].is_img) return 2;
    if (file_list[selected_idx].is_html) return 3;
    return 4;
}

int fm_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, char *out_path, uint32_t max_out) {
    uint32_t nav_y = win_y + 42, b_ref_x = win_x + win_w - 120;
    if (point_in_rect(mx, my, b_ref_x, nav_y + 4, 96, 28)) { fm_refresh(); return 0; }

    uint32_t side_w = 180, main_y = win_y + 86;
    const char *places[] = { "/workspace", "/mis_archivos", "/etc", "/proc", "/" };
    for (int i = 0; i < 5; ++i) {
        uint32_t py = main_y + 42 + (uint32_t)(i * 32);
        if (point_in_rect(mx, my, win_x + 16, py - 6, side_w - 4, 26)) {
            str_copy(current_folder, places[i], sizeof(current_folder)); fm_refresh(); return 0;
        }
    }

    uint32_t list_x = win_x + 14 + side_w + 10, list_w = win_w - 28 - side_w - 10, main_h = win_h - 180;
    if (point_in_rect(mx, my, list_x, main_y + 32, list_w, main_h - 34)) {
        int32_t rel_y = my - (int32_t)(main_y + 36);
        int clicked_row = (rel_y < 0) ? -1 : (int)(rel_y / 24);
        if (clicked_row >= 0 && clicked_row < file_count) {
            selected_idx = clicked_row;
            static char prev_buf[128]; int r = vfs_read(file_list[selected_idx].name, prev_buf, sizeof(prev_buf) - 1);
            if (r > 0) {
                prev_buf[r] = '\0'; for (int k = 0; k < r; ++k) if (prev_buf[k] == '\n' || prev_buf[k] == '\r') prev_buf[k] = ' ';
                str_copy(preview_text, prev_buf, sizeof(preview_text));
            } else str_copy(preview_text, "(Archivo sin contenido)", sizeof(preview_text));
            return fm_open_selected(out_path, max_out); /* SOMA_PARCHE_LIVE_V1 */
        }
    }

    uint32_t prev_y = win_y + win_h - 84, btn_act_x = win_x + win_w - 140;
    if (selected_idx >= 0 && selected_idx < file_count && point_in_rect(mx, my, btn_act_x, prev_y + 8, 110, 28)) {
        str_copy(out_path, file_list[selected_idx].name, max_out);
        if (file_list[selected_idx].is_img) return 2;
        if (file_list[selected_idx].is_html) return 3;
        if (file_list[selected_idx].is_txt) return 4;
        return 4; // Por defecto intentar abrir todo como texto
    }
    return 0;
}
void fm_handle_key(char c) {
    if (c == 16 && selected_idx > 0) selected_idx--;
    else if (c == 14 && selected_idx < file_count - 1) selected_idx++;
}
