#include "imgview.h"
#include "image.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "console.h"
#include "fs.h"
#include "mem.h"

#define MAX_GALLERY_IMAGES 16

static struct image current_img;
static int img_loaded = 0;
static char current_img_path[64] = "/workspace/logo_soma.bmp";

static char gallery[MAX_GALLERY_IMAGES][64];
static int gallery_count = 0;
static int gallery_idx = 0;

static int zoom_pct = 100;
static char status_info[128] = "Visor de Imagenes listo";

static void str_copy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return (*a == *b);
}

static void scan_available_images(void)
{
    gallery_count = 0;
    static char list_buf[1024];
    int len = vfs_format_list_ext(1, 0, list_buf, sizeof(list_buf) - 1);
    if (len <= 0) return;
    list_buf[len] = '\0';

    const char *p = list_buf;
    if (p[0] == 'A' && p[1] == 'r') {
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
    }

    while (*p && gallery_count < MAX_GALLERY_IMAGES) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;

        char fname[64];
        uint32_t fp = 0;
        while (*p && *p != ',' && fp < sizeof(fname) - 1) {
            fname[fp++] = *p++;
        }
        fname[fp] = '\0';

        /* Filtrar archivos de imagen */
        uint32_t nl = 0; while (fname[nl]) nl++;
        if (nl > 4 && (fname[nl - 4] == '.' && (fname[nl - 3] == 'b' || fname[nl - 3] == 'B') &&
                      (fname[nl - 2] == 'm' || fname[nl - 2] == 'M') && (fname[nl - 1] == 'p' || fname[nl - 1] == 'P'))) {
            str_copy(gallery[gallery_count++], fname, 64);
        }
    }

    if (gallery_count == 0) {
        str_copy(gallery[0], "/workspace/logo_soma.bmp", 64);
        str_copy(gallery[1], "/workspace/chip_soma.bmp", 64);
        gallery_count = 2;
    }
}

void imgview_load_file(const char *path)
{
    if (img_loaded) {
        image_free(&current_img);
        img_loaded = 0;
    }

    str_copy(current_img_path, path, sizeof(current_img_path));
    /* SOMA_PARCHE_LIVE_V1: que '<' y '>' partan de la imagen abierta */
    for (int gi = 0; gi < gallery_count; ++gi) {
        if (str_eq(gallery[gi], current_img_path)) { gallery_idx = gi; break; }
    }
    static uint8_t raw_buf[24576];
    int bytes = vfs_read(current_img_path, (char *)raw_buf, sizeof(raw_buf) - 1);

    if (bytes <= 0) {
        if (str_eq(current_img_path, "/workspace/logo_soma.bmp")) {
            bytes = image_create_demo_bmp(raw_buf, sizeof(raw_buf), 80, 40);
            if (bytes > 0) vfs_write(current_img_path, (const char *)raw_buf, (uint32_t)bytes);
        } else if (str_eq(current_img_path, "/workspace/chip_soma.bmp")) {
            bytes = image_create_chip_bmp(raw_buf, sizeof(raw_buf), 64, 32);
            if (bytes > 0) vfs_write(current_img_path, (const char *)raw_buf, (uint32_t)bytes);
        }
    }

    if (bytes > 0 && image_decode(raw_buf, (size_t)bytes, &current_img) == 0) {
        img_loaded = 1;
        zoom_pct = 150; /* Zoom inicial cómodo */
        str_copy(status_info, "Imagen TrueColor cargada en memoria", sizeof(status_info));
    } else {
        img_loaded = 0;
        str_copy(status_info, "Error: no se pudo decodificar el archivo", sizeof(status_info));
    }
}

void imgview_init(void)
{
    scan_available_images();
    gallery_idx = 0;
    if (gallery_count > 0) {
        imgview_load_file(gallery[gallery_idx]);
    }
}

void imgview_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor)
{
    (void)show_cursor;

    /* Sombra y ventana */
    fb_fill_rect(win_x + 8, win_y + 8, win_w, win_h, 0x00050810);
    fb_fill_rect(win_x, win_y, win_w, win_h, COLOR_CARD_BG);
    fb_draw_rect(win_x, win_y, win_w, win_h, 0x0010B981); /* Borde esmeralda */

    /* Barra de título */
    fb_fill_rect(win_x, win_y, win_w, 36, 0x000B0F19);
    fb_draw_rect(win_x, win_y, win_w, 36, 0x00334155);

    /* Botón cerrar [X] */
    uint32_t btn_close_x = win_x + 12;
    uint32_t btn_close_y = win_y + 10;
    fb_fill_rect(btn_close_x, btn_close_y, 16, 16, 0x00EF4444);
    fb_draw_rect(btn_close_x, btn_close_y, 16, 16, 0x00FFFFFF);

    font_draw_string(win_x + 76, win_y + 14, "SOMA Image Viewer :: Visor de Imagenes Bare-Metal", 0x00F8FAFC, 1);

    /* Barra de herramientas */
    uint32_t tool_y = win_y + 42;
    uint32_t bar_h = 36;
    fb_fill_rect(win_x + 14, tool_y, win_w - 28, bar_h, 0x000F172A);
    fb_draw_rect(win_x + 14, tool_y, win_w - 28, bar_h, 0x001E293B);

    /* Botones: [ < ] [ > ] [ + ] [ - ] [ 100% ] */
    uint32_t bx = win_x + 20;
    fb_fill_rounded_rect(bx, tool_y + 4, 32, 28, 4, 0x001E293B);
    fb_draw_rounded_rect(bx, tool_y + 4, 32, 28, 4, COLOR_CYAN);
    font_draw_string(bx + 11, tool_y + 14, "<", 0x00FFFFFF, 1);

    bx += 38;
    fb_fill_rounded_rect(bx, tool_y + 4, 32, 28, 4, 0x001E293B);
    fb_draw_rounded_rect(bx, tool_y + 4, 32, 28, 4, COLOR_CYAN);
    font_draw_string(bx + 11, tool_y + 14, ">", 0x00FFFFFF, 1);

    bx += 46;
    fb_fill_rounded_rect(bx, tool_y + 4, 32, 28, 4, 0x001E293B);
    fb_draw_rounded_rect(bx, tool_y + 4, 32, 28, 4, 0x0010B981);
    font_draw_string(bx + 11, tool_y + 14, "+", 0x0010B981, 1);

    bx += 38;
    fb_fill_rounded_rect(bx, tool_y + 4, 32, 28, 4, 0x001E293B);
    fb_draw_rounded_rect(bx, tool_y + 4, 32, 28, 4, 0x00F59E0B);
    font_draw_string(bx + 11, tool_y + 14, "-", 0x00F59E0B, 1);

    bx += 38;
    fb_fill_rounded_rect(bx, tool_y + 4, 48, 28, 4, 0x001E293B);
    fb_draw_rounded_rect(bx, tool_y + 4, 48, 28, 4, 0x0094A3B8);
    font_draw_string(bx + 8, tool_y + 14, "100%", 0x00E2E8F0, 1);

    /* Nombre de archivo actual en barra */
    font_draw_string(win_x + 250, tool_y + 14, current_img_path, 0x00F59E0B, 1);

    /* Viewport de imagen */
    uint32_t view_x = win_x + 14;
    uint32_t view_y = win_y + 86;
    uint32_t view_w = win_w - 28;
    uint32_t view_h = win_h - 132;

    /* Fondo oscuro con patrón de contraste */
    fb_fill_rect(view_x, view_y, view_w, view_h, 0x00030712);
    fb_draw_rect(view_x, view_y, view_w, view_h, 0x001F2937);

    fb_set_clip((int32_t)view_x, (int32_t)view_y, (int32_t)view_w, (int32_t)view_h);

    if (img_loaded && current_img.pixels) {
        int32_t disp_w = (int32_t)((current_img.width * (uint32_t)zoom_pct) / 100);
        int32_t disp_h = (int32_t)((current_img.height * (uint32_t)zoom_pct) / 100);

        int32_t center_x = (int32_t)view_x + (int32_t)(view_w - (uint32_t)disp_w) / 2;
        int32_t center_y = (int32_t)view_y + (int32_t)(view_h - (uint32_t)disp_h) / 2;

        /* Marco de la imagen */
        fb_draw_rect((uint32_t)(center_x - 2), (uint32_t)(center_y - 2), (uint32_t)(disp_w + 4), (uint32_t)(disp_h + 4), 0x0038BDF8);

        image_render_alpha(&current_img, center_x, center_y, disp_w, disp_h);
    } else {
        font_draw_string(view_x + 40, view_y + view_h / 2, "No hay imagen cargada.", 0x00EF4444, 2);
    }

    fb_reset_clip();

    /* Barra de estado */
    uint32_t st_y = win_y + win_h - 36;
    fb_fill_rect(win_x, st_y, win_w, 36, 0x000B0F19);
    fb_draw_rect(win_x, st_y, win_w, 36, 0x001E293B);

    font_draw_string(win_x + 16, st_y + 14, status_info, 0x0010B981, 1);

    if (img_loaded) {
        char metrics[64];
        uint32_t mp = 0;
        const char *m_lbl = "Res: "; while (*m_lbl) metrics[mp++] = *m_lbl++;
        uint32_t v = current_img.width; char tb[10]; int tn = 0; while (v) { tb[tn++] = '0' + v % 10; v /= 10; } while (tn) metrics[mp++] = tb[--tn];
        metrics[mp++] = 'x';
        v = current_img.height; tn = 0; while (v) { tb[tn++] = '0' + v % 10; v /= 10; } while (tn) metrics[mp++] = tb[--tn];
        const char *z_lbl = " | Zoom: "; while (*z_lbl) metrics[mp++] = *z_lbl++;
        v = (uint32_t)zoom_pct; tn = 0; while (v) { tb[tn++] = '0' + v % 10; v /= 10; } while (tn) metrics[mp++] = tb[--tn];
        metrics[mp++] = '%'; metrics[mp] = '\0';
        font_draw_string(win_x + win_w - 220, st_y + 14, metrics, 0x0094A3B8, 1);
    }
}

int imgview_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    (void)win_w; (void)win_h;

    /* Botón cerrar [X] */
    if (point_in_rect(mx, my, win_x + 8, win_y + 6, 26, 26)) {
        return 1;
    }

    uint32_t tool_y = win_y + 42;
    uint32_t bx = win_x + 20;

    /* Botón [ < ] */
    if (point_in_rect(mx, my, bx, tool_y + 4, 32, 28)) {
        if (gallery_count > 0) {
            gallery_idx = (gallery_idx - 1 + gallery_count) % gallery_count;
            imgview_load_file(gallery[gallery_idx]);
        }
        return 0;
    }

    /* Botón [ > ] */
    bx += 38;
    if (point_in_rect(mx, my, bx, tool_y + 4, 32, 28)) {
        if (gallery_count > 0) {
            gallery_idx = (gallery_idx + 1) % gallery_count;
            imgview_load_file(gallery[gallery_idx]);
        }
        return 0;
    }

    /* Botón [ + ] */
    bx += 46;
    if (point_in_rect(mx, my, bx, tool_y + 4, 32, 28)) {
        if (zoom_pct < 400) zoom_pct += 25;
        return 0;
    }

    /* Botón [ - ] */
    bx += 38;
    if (point_in_rect(mx, my, bx, tool_y + 4, 32, 28)) {
        if (zoom_pct > 50) zoom_pct -= 25;
        return 0;
    }

    /* Botón [ 100% ] */
    bx += 38;
    if (point_in_rect(mx, my, bx, tool_y + 4, 48, 28)) {
        zoom_pct = 100;
        return 0;
    }

    return 0;
}

void imgview_handle_key(char c)
{
    if (c == '+' || c == '=') {
        if (zoom_pct < 400) zoom_pct += 25;
    } else if (c == '-' || c == '_') {
        if (zoom_pct > 50) zoom_pct -= 25;
    } else if (c == '0') {
        zoom_pct = 100;
    } else if (c == 6 || c == ' ') { /* Siguiente */
        if (gallery_count > 0) {
            gallery_idx = (gallery_idx + 1) % gallery_count;
            imgview_load_file(gallery[gallery_idx]);
        }
    } else if (c == 2) { /* Anterior */
        if (gallery_count > 0) {
            gallery_idx = (gallery_idx - 1 + gallery_count) % gallery_count;
            imgview_load_file(gallery[gallery_idx]);
        }
    }
}
