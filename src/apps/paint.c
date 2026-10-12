#include "paint.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "fs.h"
#include "mem.h"
#include "image.h"
#include "console.h"

#define PAINT_CW 80
#define PAINT_CH 60

enum paint_tool {
    TOOL_PENCIL = 0,
    TOOL_BRUSH,
    TOOL_ERASER,
    TOOL_BUCKET,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_RECT_FILL,
    TOOL_CIRCLE,
    TOOL_CIRCLE_FILL,
    TOOL_PICKER,
    TOOL_COUNT
};

static const uint32_t PALETTE[16] = {
    0x00000000, /* 0: Negro */
    0x00FFFFFF, /* 1: Blanco */
    0x0064748B, /* 2: Gris pizarra */
    0x00CBD5E1, /* 3: Gris claro */
    0x00EF4444, /* 4: Rojo */
    0x00991B1B, /* 5: Granate */
    0x00F97316, /* 6: Naranja */
    0x00F59E0B, /* 7: Ambar */
    0x00EAB308, /* 8: Amarillo */
    0x0010B981, /* 9: Verde */
    0x00065F46, /* 10: Verde bosque */
    0x0006B6D4, /* 11: Cian */
    0x003B82F6, /* 12: Azul */
    0x001E3A8A, /* 13: Azul marino */
    0x00A855F7, /* 14: Violeta */
    0x00EC4899  /* 15: Rosa */
};

static uint32_t *canvas_pixels = 0;
static enum paint_tool cur_tool = TOOL_PENCIL;
static uint32_t cur_color = 0x00000000;
static int cur_size = 1;

static char paint_path[64] = "/workspace/dibujo.bmp";
static char paint_status[64] = "Listo para pintar";

static int last_stroke_x = -1;
static int last_stroke_y = -1;
static int shape_start_x = -1;
static int shape_start_y = -1;
static int shape_preview_x = -1;
static int shape_preview_y = -1;
static int shape_dragging = 0;

static void str_copy_p(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (src && src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void paint_init(void)
{
    if (!canvas_pixels) {
        canvas_pixels = (uint32_t *)kmalloc(PAINT_CW * PAINT_CH * sizeof(uint32_t));
    }
    if (canvas_pixels) {
        for (uint32_t i = 0; i < PAINT_CW * PAINT_CH; ++i) {
            canvas_pixels[i] = 0x00FFFFFF;
        }
    }
    cur_tool = TOOL_PENCIL;
    cur_color = PALETTE[0];
    cur_size = 1;
    shape_dragging = 0;
    last_stroke_x = last_stroke_y = -1;
    str_copy_p(paint_path, "/workspace/dibujo.bmp", sizeof(paint_path));
    str_copy_p(paint_status, "Lienzo nuevo creado (80x60)", sizeof(paint_status));
}

static void plot_brush_point(int cx, int cy, uint32_t col, int sz)
{
    if (!canvas_pixels) return;
    if (sz <= 1) {
        if (cx >= 0 && cx < PAINT_CW && cy >= 0 && cy < PAINT_CH)
            canvas_pixels[cy * PAINT_CW + cx] = col;
        return;
    }
    int r = sz;
    for (int dy = -r + 1; dy < r; ++dy) {
        for (int dx = -r + 1; dx < r; ++dx) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < PAINT_CW && py >= 0 && py < PAINT_CH) {
                canvas_pixels[py * PAINT_CW + px] = col;
            }
        }
    }
}

static void paint_stroke_line(int x0, int y0, int x1, int y1, uint32_t col, int sz)
{
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        plot_brush_point(x0, y0, col, sz);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void paint_flood_fill(int start_x, int start_y, uint32_t fill_col)
{
    if (!canvas_pixels) return;
    if (start_x < 0 || start_x >= PAINT_CW || start_y < 0 || start_y >= PAINT_CH) return;

    uint32_t target_col = canvas_pixels[start_y * PAINT_CW + start_x];
    if (target_col == fill_col) return;

    uint16_t *q_x = (uint16_t *)kmalloc(PAINT_CW * PAINT_CH * sizeof(uint16_t));
    uint16_t *q_y = (uint16_t *)kmalloc(PAINT_CW * PAINT_CH * sizeof(uint16_t));
    if (!q_x || !q_y) {
        if (q_x) kfree(q_x);
        if (q_y) kfree(q_y);
        return;
    }

    int head = 0, tail = 0;
    q_x[tail] = (uint16_t)start_x;
    q_y[tail] = (uint16_t)start_y;
    tail++;

    canvas_pixels[start_y * PAINT_CW + start_x] = fill_col;

    while (head < tail) {
        int x = (int)q_x[head];
        int y = (int)q_y[head];
        head++;

        static const int dx[4] = { 0, 0, -1, 1 };
        static const int dy[4] = { -1, 1, 0, 0 };

        for (int d = 0; d < 4; ++d) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx >= 0 && nx < PAINT_CW && ny >= 0 && ny < PAINT_CH) {
                int idx = ny * PAINT_CW + nx;
                if (canvas_pixels[idx] == target_col) {
                    canvas_pixels[idx] = fill_col;
                    if (tail < PAINT_CW * PAINT_CH) {
                        q_x[tail] = (uint16_t)nx;
                        q_y[tail] = (uint16_t)ny;
                        tail++;
                    }
                }
            }
        }
    }

    kfree(q_x);
    kfree(q_y);
}

static void draw_canvas_rect(int x0, int y0, int x1, int y1, uint32_t col, int fill)
{
    int min_x = x0 < x1 ? x0 : x1, max_x = x0 > x1 ? x0 : x1;
    int min_y = y0 < y1 ? y0 : y1, max_y = y0 > y1 ? y0 : y1;
    if (min_x < 0) min_x = 0;
    if (max_x >= PAINT_CW) max_x = PAINT_CW - 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= PAINT_CH) max_y = PAINT_CH - 1;

    if (fill) {
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                canvas_pixels[y * PAINT_CW + x] = col;
            }
        }
    } else {
        for (int x = min_x; x <= max_x; ++x) {
            canvas_pixels[min_y * PAINT_CW + x] = col;
            canvas_pixels[max_y * PAINT_CW + x] = col;
        }
        for (int y = min_y; y <= max_y; ++y) {
            canvas_pixels[y * PAINT_CW + min_x] = col;
            canvas_pixels[y * PAINT_CW + max_x] = col;
        }
    }
}

static void draw_canvas_circle(int x0, int y0, int x1, int y1, uint32_t col, int fill)
{
    int dx = x1 - x0, dy = y1 - y0;
    int r2 = dx * dx + dy * dy;
    int r = 0; while (r * r <= r2) r++; r--;
    if (r <= 0) { plot_brush_point(x0, y0, col, 1); return; }

    for (int cy = -r; cy <= r; ++cy) {
        for (int cx = -r; cx <= r; ++cx) {
            int d = cx * cx + cy * cy;
            int px = x0 + cx, py = y0 + cy;
            if (px < 0 || px >= PAINT_CW || py < 0 || py >= PAINT_CH) continue;
            if (fill) {
                if (d <= r2) canvas_pixels[py * PAINT_CW + px] = col;
            } else {
                if (d <= r2 && d >= (r - 1) * (r - 1)) canvas_pixels[py * PAINT_CW + px] = col;
            }
        }
    }
}

static int paint_save_bmp(const char *path)
{
    if (!canvas_pixels) return -1;
    uint32_t stride = ((PAINT_CW * 3 + 3) / 4) * 4;
    uint32_t total_size = 54 + PAINT_CH * stride;

    uint8_t *bmp = (uint8_t *)kmalloc(total_size);
    if (!bmp) return -1;
    memset(bmp, 0, total_size);

    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[2] = (uint8_t)(total_size & 0xFF);
    bmp[3] = (uint8_t)((total_size >> 8) & 0xFF);
    bmp[4] = (uint8_t)((total_size >> 16) & 0xFF);
    bmp[5] = (uint8_t)((total_size >> 24) & 0xFF);
    bmp[10] = 54;
    bmp[14] = 40;
    bmp[18] = (uint8_t)(PAINT_CW & 0xFF);
    bmp[19] = (uint8_t)((PAINT_CW >> 8) & 0xFF);
    bmp[22] = (uint8_t)(PAINT_CH & 0xFF);
    bmp[23] = (uint8_t)((PAINT_CH >> 8) & 0xFF);
    bmp[26] = 1;
    bmp[28] = 24;
    uint32_t img_bytes = PAINT_CH * stride;
    bmp[34] = (uint8_t)(img_bytes & 0xFF);
    bmp[35] = (uint8_t)((img_bytes >> 8) & 0xFF);

    uint8_t *pix = bmp + 54;
    for (uint32_t y = 0; y < PAINT_CH; ++y) {
        uint32_t src_y = PAINT_CH - 1 - y;
        uint8_t *row = pix + y * stride;
        for (uint32_t x = 0; x < PAINT_CW; ++x) {
            uint32_t c = canvas_pixels[src_y * PAINT_CW + x];
            row[x * 3 + 0] = (uint8_t)(c & 0xFF);
            row[x * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
            row[x * 3 + 2] = (uint8_t)((c >> 16) & 0xFF);
        }
    }

    int r = vfs_write(path, (const char *)bmp, total_size);
    kfree(bmp);
    return r >= 0 ? 0 : -1;
}

static int paint_load_bmp(const char *path)
{
    paint_init();
    uint8_t *raw = (uint8_t *)kmalloc(FS_DATA_MAX);
    if (!raw) return -1;
    int r = vfs_read(path, (char *)raw, FS_DATA_MAX - 1);
    if (r <= 54 || raw[0] != 'B' || raw[1] != 'M') {
        kfree(raw);
        return -1;
    }
    struct image img;
    if (image_decode(raw, (size_t)r, &img) != 0) {
        kfree(raw);
        return -1;
    }
    for (uint32_t i = 0; i < PAINT_CW * PAINT_CH; ++i) canvas_pixels[i] = 0x00FFFFFF;

    uint32_t copy_w = img.width < PAINT_CW ? img.width : PAINT_CW;
    uint32_t copy_h = img.height < PAINT_CH ? img.height : PAINT_CH;
    for (uint32_t y = 0; y < copy_h; ++y) {
        for (uint32_t x = 0; x < copy_w; ++x) {
            canvas_pixels[y * PAINT_CW + x] = img.pixels[y * img.width + x] & 0x00FFFFFF;
        }
    }
    image_free(&img);
    kfree(raw);
    return 0;
}

void paint_open_file(const char *path)
{
    if (path && *path) {
        str_copy_p(paint_path, path, sizeof(paint_path));
        if (paint_load_bmp(paint_path) == 0) {
            str_copy_p(paint_status, "Archivo BMP cargado", sizeof(paint_status));
        } else {
            str_copy_p(paint_status, "Archivo no encontrado. Nuevo lienzo listo", sizeof(paint_status));
        }
    }
}

static int get_canvas_geom(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h,
                           int *out_x, int *out_y, int *out_scale)
{
    int sidebar_w = 144;
    int top_bar_h = 40;
    int status_h  = 26;
    int avail_w = (int)win_w - sidebar_w - 24;
    int avail_h = (int)win_h - top_bar_h - status_h - 16;
    if (avail_w < 80 || avail_h < 60) return 0;

    int scale = avail_w / PAINT_CW;
    if (avail_h / PAINT_CH < scale) scale = avail_h / PAINT_CH;
    if (scale < 2) scale = 2;
    if (scale > 5) scale = 5;

    int cw = PAINT_CW * scale;
    int ch = PAINT_CH * scale;
    int cx = (int)win_x + sidebar_w + 12 + (avail_w - cw) / 2;
    int cy = (int)win_y + top_bar_h + 8 + (avail_h - ch) / 2;

    *out_x = cx;
    *out_y = cy;
    *out_scale = scale;
    return 1;
}

void paint_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor)
{
    (void)show_cursor;
    if (!canvas_pixels) paint_init();

    /* Fondo principal y barra de menú */
    fb_fill_rect(win_x, win_y, win_w, win_h, 0x000F172A);
    fb_fill_rect(win_x, win_y, win_w, 38, 0x001E293B);
    fb_draw_rect(win_x, win_y, win_w, 38, 0x00334155);

    /* Botones de menú superior */
    static const char *const top_btns[] = { "NUEVO", "GUARDAR", "ABRIR", "LIMPIAR" };
    int btn_x = (int)win_x + 10;
    for (int b = 0; b < 4; ++b) {
        int bw = 64;
        fb_fill_rounded_rect(btn_x, (int)win_y + 6, bw, 26, 4, 0x00334155);
        fb_draw_rounded_rect(btn_x, (int)win_y + 6, bw, 26, 4, 0x0064748B);
        font_draw_string((uint32_t)(btn_x + 8), win_y + 14, top_btns[b], 0x00F8FAFC, 1);
        btn_x += bw + 8;
    }

    /* Ruta del archivo */
    font_draw_string((uint32_t)btn_x + 8, win_y + 14, "Ruta:", 0x0094A3B8, 1);
    font_draw_string((uint32_t)btn_x + 54, win_y + 14, paint_path, 0x00F59E0B, 1);

    /* Barra lateral izquierda: Herramientas, Tamaños y Paleta */
    int side_x = (int)win_x + 8;
    int side_y = (int)win_y + 44;
    int side_w = 138;
    int side_h = (int)win_h - 44 - 28;
    fb_fill_rounded_rect(side_x, side_y, side_w, side_h, 6, 0x00161F30);
    fb_draw_rounded_rect(side_x, side_y, side_w, side_h, 6, 0x00334155);

    font_draw_string((uint32_t)side_x + 10, (uint32_t)side_y + 8, "HERRAMIENTAS", 0x0038BDF8, 1);

    static const char *const tool_names[TOOL_COUNT] = {
        "Lapiz", "Pincel",
        "Goma",  "Cubo",
        "Linea", "Pipeta",
        "Rect",  "RectR",
        "Circ",  "CircR"
    };

    for (int t = 0; t < TOOL_COUNT; ++t) {
        int col = t % 2;
        int row = t / 2;
        int tx = side_x + 10 + col * 60;
        int ty = side_y + 24 + row * 26;
        int is_act = (cur_tool == (enum paint_tool)t);
        fb_fill_rounded_rect(tx, ty, 54, 22, 3, is_act ? 0x000284C7 : 0x001E293B);
        fb_draw_rounded_rect(tx, ty, 54, 22, 3, is_act ? 0x0038BDF8 : 0x00475569);
        font_draw_string((uint32_t)tx + 6, (uint32_t)ty + 6, tool_names[t],
                         is_act ? 0x00FFFFFF : 0x0094A3B8, 1);
    }

    /* Selector de grosor */
    int sz_y = side_y + 160;
    font_draw_string((uint32_t)side_x + 10, (uint32_t)sz_y, "GROSOR", 0x0038BDF8, 1);
    for (int s = 1; s <= 4; ++s) {
        int sx = side_x + 10 + (s - 1) * 30;
        int is_act = (cur_size == s);
        fb_fill_rounded_rect(sx, sz_y + 14, 26, 20, 3, is_act ? 0x00F59E0B : 0x001E293B);
        char lbl[2] = { (char)('0' + s), '\0' };
        font_draw_string((uint32_t)sx + 9, (uint32_t)sz_y + 18, lbl,
                         is_act ? 0x00000000 : 0x00CBD5E1, 1);
    }

    /* Muestra de color actual */
    int pal_y = sz_y + 44;
    font_draw_string((uint32_t)side_x + 10, (uint32_t)pal_y, "PALETA", 0x0038BDF8, 1);
    fb_fill_rounded_rect(side_x + 68, pal_y - 2, 56, 18, 3, cur_color);
    fb_draw_rounded_rect(side_x + 68, pal_y - 2, 56, 18, 3, 0x00F8FAFC);

    for (int c = 0; c < 16; ++c) {
        int col = c % 4;
        int row = c / 4;
        int px = side_x + 10 + col * 29;
        int py = pal_y + 18 + row * 24;
        fb_fill_rounded_rect(px, py, 24, 20, 3, PALETTE[c]);
        int is_sel = (cur_color == PALETTE[c]);
        fb_draw_rounded_rect(px, py, 24, 20, 3, is_sel ? 0x00FFFFFF : 0x00475569);
    }

    /* Lienzo central */
    int cx = 0, cy = 0, scale = 4;
    if (get_canvas_geom(win_x, win_y, win_w, win_h, &cx, &cy, &scale)) {
        int cw = PAINT_CW * scale;
        int ch = PAINT_CH * scale;

        /* Marco y sombra del lienzo */
        fb_fill_rect((uint32_t)cx + 6, (uint32_t)cy + 6, (uint32_t)cw, (uint32_t)ch, 0x00050810);
        fb_draw_rect((uint32_t)cx - 2, (uint32_t)cy - 2, (uint32_t)cw + 4, (uint32_t)ch + 4, 0x00F59E0B);

        /* Volcar píxeles escalados del lienzo */
        for (int y = 0; y < PAINT_CH; ++y) {
            for (int x = 0; x < PAINT_CW; ++x) {
                uint32_t pcol = canvas_pixels[y * PAINT_CW + x];
                fb_fill_rect((uint32_t)(cx + x * scale),
                             (uint32_t)(cy + y * scale),
                             (uint32_t)scale, (uint32_t)scale, pcol);
            }
        }

        /* Previsualización en pantalla al arrastrar figuras (Línea / Rect / Círculo) */
        if (shape_dragging && shape_start_x >= 0 && shape_preview_x >= 0) {
            uint32_t prev_col = cur_color;
            if (cur_tool == TOOL_LINE) {
                int sx0 = cx + shape_start_x * scale + scale / 2;
                int sy0 = cy + shape_start_y * scale + scale / 2;
                int sx1 = cx + shape_preview_x * scale + scale / 2;
                int sy1 = cy + shape_preview_y * scale + scale / 2;
                fb_draw_line(sx0, sy0, sx1, sy1, prev_col);
            } else if (cur_tool == TOOL_RECT || cur_tool == TOOL_RECT_FILL) {
                int min_x = shape_start_x < shape_preview_x ? shape_start_x : shape_preview_x;
                int max_x = shape_start_x > shape_preview_x ? shape_start_x : shape_preview_x;
                int min_y = shape_start_y < shape_preview_y ? shape_start_y : shape_preview_y;
                int max_y = shape_start_y > shape_preview_y ? shape_start_y : shape_preview_y;
                int rx = cx + min_x * scale;
                int ry = cy + min_y * scale;
                int rw = (max_x - min_x + 1) * scale;
                int rh = (max_y - min_y + 1) * scale;
                if (cur_tool == TOOL_RECT_FILL)
                    fb_fill_rect((uint32_t)rx, (uint32_t)ry, (uint32_t)rw, (uint32_t)rh, prev_col);
                else
                    fb_draw_rect((uint32_t)rx, (uint32_t)ry, (uint32_t)rw, (uint32_t)rh, prev_col);
            } else if (cur_tool == TOOL_CIRCLE || cur_tool == TOOL_CIRCLE_FILL) {
                int sx0 = cx + shape_start_x * scale + scale / 2;
                int sy0 = cy + shape_start_y * scale + scale / 2;
                int dx = (shape_preview_x - shape_start_x) * scale;
                int dy = (shape_preview_y - shape_start_y) * scale;
                int r = 0; while (r * r <= dx * dx + dy * dy) r++; r--;
                if (cur_tool == TOOL_CIRCLE_FILL)
                    fb_fill_circle(sx0, sy0, r, prev_col);
                else
                    fb_draw_circle(sx0, sy0, r, prev_col);
            }
        }
    }

    /* Barra de estado inferior */
    uint32_t st_y = win_y + win_h - 26;
    fb_fill_rect(win_x, st_y, win_w, 26, 0x001E293B);
    fb_draw_rect(win_x, st_y, win_w, 26, 0x00334155);

    font_draw_string(win_x + 12, st_y + 8, paint_status, 0x0010B981, 1);

    char info_buf[64];
    uint32_t ip = 0;
    const char *ilbl = "Lienzo: 80x60 (14.4 KiB BMP)";
    while (*ilbl) info_buf[ip++] = *ilbl++;
    info_buf[ip] = '\0';
    font_draw_string(win_x + win_w - 240, st_y + 8, info_buf, 0x0094A3B8, 1);
}

int paint_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    /* Clic en botones de acción superior */
    if (my >= (int32_t)win_y + 6 && my <= (int32_t)win_y + 32) {
        int btn_x = (int)win_x + 10;
        /* NUEVO */
        if (mx >= btn_x && mx <= btn_x + 64) {
            paint_init();
            return 0;
        }
        btn_x += 64 + 8;
        /* GUARDAR */
        if (mx >= btn_x && mx <= btn_x + 64) {
            if (paint_save_bmp(paint_path) == 0)
                str_copy_p(paint_status, "Guardado con exito en /workspace/dibujo.bmp", sizeof(paint_status));
            else
                str_copy_p(paint_status, "Error al guardar BMP en disco", sizeof(paint_status));
            return 0;
        }
        btn_x += 64 + 8;
        /* ABRIR */
        if (mx >= btn_x && mx <= btn_x + 64) {
            if (paint_load_bmp(paint_path) == 0)
                str_copy_p(paint_status, "Archivo BMP cargado", sizeof(paint_status));
            else
                str_copy_p(paint_status, "No se pudo abrir el archivo BMP", sizeof(paint_status));
            return 0;
        }
        btn_x += 64 + 8;
        /* LIMPIAR */
        if (mx >= btn_x && mx <= btn_x + 64) {
            for (uint32_t i = 0; i < PAINT_CW * PAINT_CH; ++i) canvas_pixels[i] = 0x00FFFFFF;
            str_copy_p(paint_status, "Lienzo limpiado", sizeof(paint_status));
            return 0;
        }
    }

    /* Clic en barra de herramientas */
    int side_x = (int)win_x + 8;
    int side_y = (int)win_y + 44;

    for (int t = 0; t < TOOL_COUNT; ++t) {
        int col = t % 2;
        int row = t / 2;
        int tx = side_x + 10 + col * 60;
        int ty = side_y + 24 + row * 26;
        if (mx >= tx && mx <= tx + 54 && my >= ty && my <= ty + 22) {
            cur_tool = (enum paint_tool)t;
            shape_dragging = 0;
            return 0;
        }
    }

    /* Clic en selector de grosor */
    int sz_y = side_y + 160;
    for (int s = 1; s <= 4; ++s) {
        int sx = side_x + 10 + (s - 1) * 30;
        if (mx >= sx && mx <= sx + 26 && my >= sz_y + 14 && my <= sz_y + 34) {
            cur_size = s;
            return 0;
        }
    }

    /* Clic en paleta de colores */
    int pal_y = sz_y + 44;
    for (int c = 0; c < 16; ++c) {
        int col = c % 4;
        int row = c / 4;
        int px = side_x + 10 + col * 29;
        int py = pal_y + 18 + row * 24;
        if (mx >= px && mx <= px + 24 && my >= py && my <= py + 20) {
            cur_color = PALETTE[c];
            return 0;
        }
    }

    /* Clic en el lienzo */
    int cx = 0, cy = 0, scale = 4;
    if (get_canvas_geom(win_x, win_y, win_w, win_h, &cx, &cy, &scale)) {
        int can_x = (mx - cx) / scale;
        int can_y = (my - cy) / scale;
        if (can_x >= 0 && can_x < PAINT_CW && can_y >= 0 && can_y < PAINT_CH) {
            if (cur_tool == TOOL_BUCKET) {
                paint_flood_fill(can_x, can_y, cur_color);
            } else if (cur_tool == TOOL_PICKER) {
                cur_color = canvas_pixels[can_y * PAINT_CW + can_x];
            } else if (cur_tool == TOOL_PENCIL || cur_tool == TOOL_BRUSH || cur_tool == TOOL_ERASER) {
                uint32_t col = (cur_tool == TOOL_ERASER) ? 0x00FFFFFF : cur_color;
                int sz = (cur_tool == TOOL_PENCIL) ? 1 : cur_size;
                plot_brush_point(can_x, can_y, col, sz);
                last_stroke_x = can_x;
                last_stroke_y = can_y;
            } else {
                shape_start_x = can_x;
                shape_start_y = can_y;
                shape_preview_x = can_x;
                shape_preview_y = can_y;
                shape_dragging = 1;
            }
        }
    }

    return 0;
}

void paint_handle_mouse(int32_t mx, int32_t my, int is_down, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    int cx = 0, cy = 0, scale = 4;
    if (!get_canvas_geom(win_x, win_y, win_w, win_h, &cx, &cy, &scale)) return;

    int can_x = (mx - cx) / scale;
    int can_y = (my - cy) / scale;

    if (!is_down) {
        if (shape_dragging && shape_start_x >= 0) {
            if (can_x < 0) can_x = 0;
            if (can_x >= PAINT_CW) can_x = PAINT_CW - 1;
            if (can_y < 0) can_y = 0;
            if (can_y >= PAINT_CH) can_y = PAINT_CH - 1;

            if (cur_tool == TOOL_LINE) {
                paint_stroke_line(shape_start_x, shape_start_y, can_x, can_y, cur_color, cur_size);
            } else if (cur_tool == TOOL_RECT) {
                draw_canvas_rect(shape_start_x, shape_start_y, can_x, can_y, cur_color, 0);
            } else if (cur_tool == TOOL_RECT_FILL) {
                draw_canvas_rect(shape_start_x, shape_start_y, can_x, can_y, cur_color, 1);
            } else if (cur_tool == TOOL_CIRCLE) {
                draw_canvas_circle(shape_start_x, shape_start_y, can_x, can_y, cur_color, 0);
            } else if (cur_tool == TOOL_CIRCLE_FILL) {
                draw_canvas_circle(shape_start_x, shape_start_y, can_x, can_y, cur_color, 1);
            }
        }
        shape_dragging = 0;
        last_stroke_x = last_stroke_y = -1;
        shape_start_x = shape_start_y = -1;
        return;
    }

    /* Arrastre activo */
    if (can_x >= 0 && can_x < PAINT_CW && can_y >= 0 && can_y < PAINT_CH) {
        if (cur_tool == TOOL_PENCIL || cur_tool == TOOL_BRUSH || cur_tool == TOOL_ERASER) {
            uint32_t col = (cur_tool == TOOL_ERASER) ? 0x00FFFFFF : cur_color;
            int sz = (cur_tool == TOOL_PENCIL) ? 1 : cur_size;
            if (last_stroke_x >= 0 && last_stroke_y >= 0) {
                paint_stroke_line(last_stroke_x, last_stroke_y, can_x, can_y, col, sz);
            } else {
                plot_brush_point(can_x, can_y, col, sz);
            }
            last_stroke_x = can_x;
            last_stroke_y = can_y;
        } else if (shape_dragging) {
            shape_preview_x = can_x;
            shape_preview_y = can_y;
        }
    }
}

void paint_handle_key(char c)
{
    if (c == 's' || c == 'S' || c == 19) {
        if (paint_save_bmp(paint_path) == 0)
            str_copy_p(paint_status, "Guardado con exito", sizeof(paint_status));
        else
            str_copy_p(paint_status, "Error al guardar", sizeof(paint_status));
    } else if (c == 'c' || c == 'C') {
        if (canvas_pixels) {
            for (uint32_t i = 0; i < PAINT_CW * PAINT_CH; ++i) canvas_pixels[i] = 0x00FFFFFF;
            str_copy_p(paint_status, "Lienzo limpiado", sizeof(paint_status));
        }
    } else if (c == '+' || c == '=') {
        if (cur_size < 4) cur_size++;
    } else if (c == '-' || c == '_') {
        if (cur_size > 1) cur_size--;
    } else if (c == '1') cur_tool = TOOL_PENCIL;
    else if (c == '2') cur_tool = TOOL_BRUSH;
    else if (c == '3') cur_tool = TOOL_ERASER;
    else if (c == '4') cur_tool = TOOL_BUCKET;
    else if (c == '5') cur_tool = TOOL_LINE;
    else if (c == '6') cur_tool = TOOL_RECT;
    else if (c == '7') cur_tool = TOOL_CIRCLE;
    else if (c == '8') cur_tool = TOOL_PICKER;
}
