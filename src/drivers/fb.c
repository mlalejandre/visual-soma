#include "fb.h"
#include "console.h"
#include "pci.h"
#include "vmm.h"
#include "mem.h"
#include "io.h"

#define BGA_INDEX_PORT      0x01CE
#define BGA_DATA_PORT       0x01CF

#define BGA_REG_ID          0
#define BGA_REG_XRES        1
#define BGA_REG_YRES        2
#define BGA_REG_BPP         3
#define BGA_REG_ENABLE      4
#define BGA_REG_BANK        5
#define BGA_REG_X_OFFSET    8
#define BGA_REG_Y_OFFSET    9

#define BGA_DISABLED        0x00
#define BGA_ENABLED         0x01
#define BGA_LFB_ENABLED     0x40

#define VGA_MISC_WRITE      0x03C2
#define VGA_SEQ_INDEX       0x03C4
#define VGA_SEQ_DATA        0x03C5
#define VGA_CRTC_INDEX      0x03D4
#define VGA_CRTC_DATA       0x03D5
#define VGA_GC_INDEX        0x03CE
#define VGA_GC_DATA         0x03CF
#define VGA_AC_INDEX        0x03C0
#define VGA_INSTAT_READ     0x03DA

static volatile uint32_t *fb_vram = (volatile uint32_t *)FB_VIRT_ADDR;
static uint32_t *fb_backbuffer = 0;
static uint32_t fb_phys_base = 0;
static int fb_mapped = 0;
static int fb_active = 0;

static uint32_t fb_cur_w = FB_RES_FHD_W;
static uint32_t fb_cur_h = FB_RES_FHD_H;

/* Región de Recorte (Scissor Clipping) */
static int32_t clip_x0 = 0;
static int32_t clip_y0 = 0;
static int32_t clip_x1 = FB_RES_FHD_W;
static int32_t clip_y1 = FB_RES_FHD_H;

/* Lista de rectángulos de daño */
#define FB_MAX_DIRTY_RECTS 16
struct fb_dirty_rect {
    int32_t x0, y0, x1, y1;
};
static struct fb_dirty_rect dirty_rects[FB_MAX_DIRTY_RECTS];
static int dirty_count = 0;

/* Estado del cursor superpuesto (Overlay) */
static const uint32_t *cursor_overlay_img = 0;
static int32_t cursor_overlay_w = 0;
static int32_t cursor_overlay_h = 0;
static int32_t cursor_pos_x = -1;
static int32_t cursor_pos_y = -1;
static int     cursor_visible = 0;

static int32_t cursor_drawn_x = -1;
static int32_t cursor_drawn_y = -1;
static int     cursor_drawn_visible = 0;

uint32_t fb_get_width(void)  { return fb_cur_w; }
uint32_t fb_get_height(void) { return fb_cur_h; }

static inline uint32_t *fb_target(void)
{
    return fb_backbuffer ? fb_backbuffer : (uint32_t *)fb_vram;
}

volatile uint32_t *fb_get_buffer(void)
{
    return (volatile uint32_t *)fb_target();
}

static void bga_write(uint16_t reg, uint16_t val)
{
#ifdef __x86_64__
    outw(BGA_INDEX_PORT, reg);
    outw(BGA_DATA_PORT, val);
#else
    (void)reg; (void)val;
#endif
}

static uint16_t bga_read(uint16_t reg)
{
#ifdef __x86_64__
    outw(BGA_INDEX_PORT, reg);
    return inw(BGA_DATA_PORT);
#else
    (void)reg; return 0;
#endif
}

static uint32_t pci_find_vga_lfb(void)
{
#ifdef __x86_64__
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint16_t v = pci_vendor_id((uint8_t)bus, slot, func);
                uint16_t d = pci_device_id((uint8_t)bus, slot, func);
                if (v == 0x1234 && d == 0x1111) {
                    return pci_bar0((uint8_t)bus, slot, func) & 0xFFFFFFF0;
                }
            }
        }
    }
#endif
    return 0xFD000000;
}

int fb_init(void)
{
    if (fb_mapped) return 0;
    fb_phys_base = pci_find_vga_lfb();

    for (uint64_t i = 0; i < 4096; ++i) {
        uint64_t va = FB_VIRT_ADDR + i * 4096;
        uint64_t pa = (uint64_t)fb_phys_base + i * 4096;
        if (vmm_map_page(va, pa, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NX) != 0) {
            return -1;
        }
    }
    fb_mapped = 1;
    return 0;
}

void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0) {
        clip_x0 = clip_x1 = clip_y0 = clip_y1 = 0;
        return;
    }
    clip_x0 = (x < 0) ? 0 : x;
    clip_y0 = (y < 0) ? 0 : y;
    clip_x1 = (x + w > (int32_t)fb_cur_w) ? (int32_t)fb_cur_w : (x + w);
    clip_y1 = (y + h > (int32_t)fb_cur_h) ? (int32_t)fb_cur_h : (y + h);
}

void fb_get_clip(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    if (x0) *x0 = clip_x0;
    if (y0) *y0 = clip_y0;
    if (x1) *x1 = clip_x1;
    if (y1) *y1 = clip_y1;
}

void fb_reset_clip(void)
{
    clip_x0 = 0;
    clip_y0 = 0;
    clip_x1 = (int32_t)fb_cur_w;
    clip_y1 = (int32_t)fb_cur_h;
}

static inline int clip_rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    if (*w <= 0 || *h <= 0) return 0;
    int32_t x0 = *x;
    int32_t y0 = *y;
    int32_t x1 = x0 + *w;
    int32_t y1 = y0 + *h;

    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;

    if (x0 >= x1 || y0 >= y1) return 0;

    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0) return;
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t x1 = x + w;
    int32_t y1 = y + h;
    if (x1 > (int32_t)fb_cur_w) x1 = (int32_t)fb_cur_w;
    if (y1 > (int32_t)fb_cur_h) y1 = (int32_t)fb_cur_h;
    if (x0 >= x1 || y0 >= y1) return;

    /* Intentar fusionar con rectángulos existentes que solapen */
    for (int i = 0; i < dirty_count; ++i) {
        if (x0 <= dirty_rects[i].x1 && x1 >= dirty_rects[i].x0 &&
            y0 <= dirty_rects[i].y1 && y1 >= dirty_rects[i].y0) {
            if (x0 < dirty_rects[i].x0) dirty_rects[i].x0 = x0;
            if (y0 < dirty_rects[i].y0) dirty_rects[i].y0 = y0;
            if (x1 > dirty_rects[i].x1) dirty_rects[i].x1 = x1;
            if (y1 > dirty_rects[i].y1) dirty_rects[i].y1 = y1;
            return;
        }
    }

    if (dirty_count < FB_MAX_DIRTY_RECTS) {
        dirty_rects[dirty_count].x0 = x0;
        dirty_rects[dirty_count].y0 = y0;
        dirty_rects[dirty_count].x1 = x1;
        dirty_rects[dirty_count].y1 = y1;
        dirty_count++;
    } else {
        /* Búfer lleno: fusionar en el rectángulo 0 abarcando todo */
        if (x0 < dirty_rects[0].x0) dirty_rects[0].x0 = x0;
        if (y0 < dirty_rects[0].y0) dirty_rects[0].y0 = y0;
        if (x1 > dirty_rects[0].x1) dirty_rects[0].x1 = x1;
        if (y1 > dirty_rects[0].y1) dirty_rects[0].y1 = y1;
    }
}

/* Transferencias en ráfaga alineadas de 64 bits */
static inline void fast_fill_span(uint32_t *dst, uint32_t color, int32_t count)
{
    if (count <= 0) return;
    uint64_t c2 = ((uint64_t)color << 32) | (uint64_t)color;

    if (((uintptr_t)dst & 7) != 0) {
        *dst++ = color;
        count--;
    }

    size_t qwords = (size_t)count / 2;
    if (qwords > 0) {
#if defined(__x86_64__)
        __asm__ volatile (
            "cld\n\t"
            "rep stosq"
            : "+D"(dst), "+c"(qwords)
            : "a"(c2)
            : "memory"
        );
#else
        uint64_t *d64 = (uint64_t *)dst;
        while (qwords >= 4) {
            d64[0] = c2; d64[1] = c2; d64[2] = c2; d64[3] = c2;
            d64 += 4; qwords -= 4;
        }
        while (qwords--) *d64++ = c2;
        dst = (uint32_t *)d64;
#endif
    }
    if (count & 1) {
        *dst = color;
    }
}

static inline void fast_copy_span(volatile uint32_t *dst, const uint32_t *src, int32_t count)
{
    if (count <= 0) return;

    if (((uintptr_t)dst & 7) == ((uintptr_t)src & 7)) {
        if (((uintptr_t)dst & 7) != 0) {
            *dst++ = *src++;
            count--;
        }
        size_t qwords = (size_t)count / 2;
        if (qwords > 0) {
#if defined(__x86_64__)
            __asm__ volatile (
                "cld\n\t"
                "rep movsq"
                : "+D"(dst), "+S"(src), "+c"(qwords)
                :
                : "memory"
            );
#else
            volatile uint64_t *d64 = (volatile uint64_t *)dst;
            const uint64_t *s64 = (const uint64_t *)src;
            while (qwords >= 4) {
                d64[0] = s64[0]; d64[1] = s64[1]; d64[2] = s64[2]; d64[3] = s64[3];
                d64 += 4; s64 += 4; qwords -= 4;
            }
            while (qwords--) *d64++ = *s64++;
            dst = (volatile uint32_t *)d64;
            src = (const uint32_t *)s64;
#endif
        }
        if (count & 1) {
            *dst = *src;
        }
    } else {
        while (count--) *dst++ = *src++;
    }
}

/* Presentación a VRAM con superposición de cursor */
void fb_present(void)
{
    if (!fb_mapped) return;

    if (fb_backbuffer) {
        /* Si el cursor cambió de posición o visibilidad, restaurar el fondo viejo en VRAM */
        if (cursor_drawn_visible && (cursor_drawn_x != cursor_pos_x || cursor_drawn_y != cursor_pos_y || !cursor_visible)) {
            fb_damage(cursor_drawn_x, cursor_drawn_y, cursor_overlay_w, cursor_overlay_h);
        }

        /* 1. Volcar sólo los rectángulos sucios */
        if (dirty_count > 0) {
            for (int i = 0; i < dirty_count; ++i) {
                int32_t x0 = dirty_rects[i].x0;
                int32_t y0 = dirty_rects[i].y0;
                int32_t w  = dirty_rects[i].x1 - x0;
                int32_t h  = dirty_rects[i].y1 - y0;

                for (int32_t r = 0; r < h; ++r) {
                    uint32_t off = (uint32_t)((y0 + r) * fb_cur_w + x0);
                    fast_copy_span(fb_vram + off, fb_backbuffer + off, w);
                }
            }
            dirty_count = 0;
        }

        /* 2. Superponer el cursor en VRAM sin tocar el backbuffer */
        if (cursor_visible && cursor_overlay_img && cursor_overlay_w > 0 && cursor_overlay_h > 0) {
            for (int32_t r = 0; r < cursor_overlay_h; ++r) {
                int32_t vy = cursor_pos_y + r;
                if (vy < 0 || vy >= (int32_t)fb_cur_h) continue;

                for (int32_t c = 0; c < cursor_overlay_w; ++c) {
                    int32_t vx = cursor_pos_x + c;
                    if (vx < 0 || vx >= (int32_t)fb_cur_w) continue;

                    uint32_t fg = cursor_overlay_img[r * cursor_overlay_w + c];
                    if ((fg >> 24) != 0) {
                        uint32_t off = (uint32_t)(vy * fb_cur_w + vx);
                        uint32_t bg = fb_backbuffer[off];
                        fb_vram[off] = fb_blend_color(bg, fg);
                    }
                }
            }
            cursor_drawn_x = cursor_pos_x;
            cursor_drawn_y = cursor_pos_y;
            cursor_drawn_visible = 1;
        } else {
            cursor_drawn_visible = 0;
        }
    }
}

void fb_present_full(void)
{
    fb_damage(0, 0, (int32_t)fb_cur_w, (int32_t)fb_cur_h);
    fb_present();
}

void fb_cursor_set_image(const uint32_t *argb, int32_t w, int32_t h)
{
    cursor_overlay_img = argb;
    cursor_overlay_w = w;
    cursor_overlay_h = h;
}

void fb_cursor_show(int32_t x, int32_t y)
{
    cursor_pos_x = x;
    cursor_pos_y = y;
    cursor_visible = 1;
}

void fb_cursor_hide(void)
{
    cursor_visible = 0;
}

int fb_set_mode(uint32_t width, uint32_t height)
{
#ifdef __x86_64__
    uint16_t id = bga_read(BGA_REG_ID);
    if ((id & 0xFFF0) != 0xB0C0) return -1;
    if (!fb_mapped) { if (fb_init() != 0) return -2; }

    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    bga_write(BGA_REG_XRES, (uint16_t)width);
    bga_write(BGA_REG_YRES, (uint16_t)height);
    bga_write(BGA_REG_BPP, 32);
    bga_write(BGA_REG_BANK, 0);
    bga_write(BGA_REG_X_OFFSET, 0);
    bga_write(BGA_REG_Y_OFFSET, 0);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

    fb_cur_w = width;
    fb_cur_h = height;
    fb_reset_clip();

    size_t bytes = (size_t)width * height * sizeof(uint32_t);
    if (!fb_backbuffer) {
        fb_backbuffer = (uint32_t *)kmalloc(bytes);
    }
    if (fb_backbuffer) {
        memset((void *)fb_backbuffer, 0, bytes);
    }

    fb_active = 1;
    dirty_count = 0;
    cursor_drawn_visible = 0;
    fb_clear(COLOR_DEEP_NAVY);
    fb_present_full();
    return 0;
#else
    (void)width; (void)height; return -1;
#endif
}

int fb_set_mode_hd(void)
{
    return fb_set_mode(FB_RES_FHD_W, FB_RES_FHD_H);
}

#ifdef __x86_64__
static const uint8_t vga_mode3_seq[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
static const uint8_t vga_mode3_crtc[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
};
static const uint8_t vga_mode3_gc[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };
static const uint8_t vga_mode3_ac[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static void vga_hardware_restore_mode3(void)
{
    outb(VGA_MISC_WRITE, 0x67);
    outb(VGA_SEQ_INDEX, 0x00); outb(VGA_SEQ_DATA, 0x01);
    for (uint8_t i = 1; i < 5; ++i) {
        outb(VGA_SEQ_INDEX, i); outb(VGA_SEQ_DATA, vga_mode3_seq[i]);
    }
    outb(VGA_SEQ_INDEX, 0x00); outb(VGA_SEQ_DATA, 0x03);

    outb(VGA_CRTC_INDEX, 0x11);
    uint8_t crtc11 = inb(VGA_CRTC_DATA);
    outb(VGA_CRTC_DATA, crtc11 & 0x7F);

    for (uint8_t i = 0; i < 25; ++i) {
        outb(VGA_CRTC_INDEX, i); outb(VGA_CRTC_DATA, vga_mode3_crtc[i]);
    }
    for (uint8_t i = 0; i < 9; ++i) {
        outb(VGA_GC_INDEX, i); outb(VGA_GC_DATA, vga_mode3_gc[i]);
    }
    inb(VGA_INSTAT_READ);
    for (uint8_t i = 0; i < 21; ++i) {
        outb(VGA_AC_INDEX, i); outb(VGA_AC_INDEX, vga_mode3_ac[i]);
    }
    outb(VGA_AC_INDEX, 0x20);

    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x04);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, 0x06);
    outb(VGA_GC_INDEX, 0x04); outb(VGA_GC_DATA, 0x02);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x06); outb(VGA_GC_DATA, 0x04);

    volatile uint8_t *font_dest = (volatile uint8_t *)0xA0000;
    for (int ch = 0; ch < 96; ++ch) {
        const uint8_t *glyph = font_get_glyph(ch + 32);
        for (int row = 0; row < 16; ++row) {
            font_dest[(ch + 32) * 32 + row] = glyph[row / 2];
        }
    }

    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x03);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, 0x02);
    outb(VGA_GC_INDEX, 0x04); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x10);
    outb(VGA_GC_INDEX, 0x06); outb(VGA_GC_DATA, 0x0E);
}
#endif

void fb_restore_text_mode(void)
{
#ifdef __x86_64__
    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    vga_hardware_restore_mode3();
    fb_active = 0;
    console_clear();
#endif
}

int fb_is_active(void) { return fb_active; }

/* ====================================================================
 * PRIMITIVAS 2D DE ALTO RENDIMIENTO
 * ==================================================================== */

void fb_clear(uint32_t color)
{
    if (!fb_active) return;
    uint32_t *target = fb_target();
    fast_fill_span(target, color, (int32_t)(fb_cur_w * fb_cur_h));
    fb_damage(0, 0, (int32_t)fb_cur_w, (int32_t)fb_cur_h);
}

void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!fb_active) return;
    if ((int32_t)x < clip_x0 || (int32_t)x >= clip_x1 ||
        (int32_t)y < clip_y0 || (int32_t)y >= clip_y1) return;

    fb_target()[y * fb_cur_w + x] = color;
    fb_damage((int32_t)x, (int32_t)y, 1, 1);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!fb_active) return;
    int32_t cx = (int32_t)x, cy = (int32_t)y, cw = (int32_t)w, ch = (int32_t)h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    for (int32_t r = 0; r < ch; ++r) {
        uint32_t *row = target + (cy + r) * fb_cur_w + cx;
        fast_fill_span(row, color, cw);
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (!fb_active || w == 0 || h == 0) return;
    fb_fill_rect(x, y, w, 2, color);
    fb_fill_rect(x, y + h - 2, w, 2, color);
    fb_fill_rect(x, y, 2, h, color);
    fb_fill_rect(x + w - 2, y, 2, h, color);
}

/* ====================================================================
 * COMPOSICIÓN ALFA
 * ==================================================================== */

uint32_t fb_blend_color(uint32_t bg, uint32_t fg)
{
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 0) return bg;
    if (a == 255) return fg | 0xFF000000;

    uint32_t bg_r = (bg >> 16) & 0xFF;
    uint32_t bg_g = (bg >> 8)  & 0xFF;
    uint32_t bg_b = bg & 0xFF;

    uint32_t fg_r = (fg >> 16) & 0xFF;
    uint32_t fg_g = (fg >> 8)  & 0xFF;
    uint32_t fg_b = fg & 0xFF;

    uint32_t inv_a = 255 - a;

    /* Fórmula entera exacta de división por 255 */
    uint32_t r = ((fg_r * a + bg_r * inv_a + 1) + ((fg_r * a + bg_r * inv_a + 1) >> 8)) >> 8;
    uint32_t g = ((fg_g * a + bg_g * inv_a + 1) + ((fg_g * a + bg_g * inv_a + 1) >> 8)) >> 8;
    uint32_t b = ((fg_b * a + bg_b * inv_a + 1) + ((fg_b * a + bg_b * inv_a + 1) >> 8)) >> 8;

    return (r << 16) | (g << 8) | b | 0xFF000000;
}

void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    if (!fb_active) return;
    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    for (int32_t r = 0; r < ch; ++r) {
        uint32_t *row = target + (cy + r) * fb_cur_w + cx;
        for (int32_t c = 0; c < cw; ++c) {
            row[c] = fb_blend_color(row[c], color);
        }
    }
}

/* ====================================================================
 * GEOMETRÍA: BORDES REDONDEADOS, CÍRCULOS Y DEGRADADOS
 * ==================================================================== */

void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
    if (!fb_active || w <= 0 || h <= 0) return;
    if (r <= 0) { fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;
    fb_damage(cx, cy, cw, ch);

    uint32_t *target = fb_target();

    /* 1. Centro y barras verticales */
    for (int32_t row = r; row < h - r; ++row) {
        int32_t py = y + row;
        if (py >= clip_y0 && py < clip_y1) {
            int32_t px = x, span_w = w;
            int32_t dummy_y = py, dummy_h = 1;
            if (clip_rect(&px, &dummy_y, &span_w, &dummy_h)) {
                fast_fill_span(target + py * fb_cur_w + px, color, span_w);
            }
        }
    }

    /* 2. Barras medias superiores e inferiores */
    for (int32_t row = 0; row < r; ++row) {
        int32_t py_top = y + row;
        int32_t py_bot = y + h - 1 - row;
        int32_t px = x + r, span_w = w - 2 * r;
        int32_t dummy_h = 1;

        if (py_top >= clip_y0 && py_top < clip_y1) {
            int32_t kx = px, kw = span_w, ky = py_top;
            if (clip_rect(&kx, &ky, &kw, &dummy_h)) {
                fast_fill_span(target + py_top * fb_cur_w + kx, color, kw);
            }
        }
        if (py_bot >= clip_y0 && py_bot < clip_y1) {
            int32_t kx = px, kw = span_w, ky = py_bot;
            if (clip_rect(&kx, &ky, &kw, &dummy_h)) {
                fast_fill_span(target + py_bot * fb_cur_w + kx, color, kw);
            }
        }
    }

    /* 3. Cuadrantes redondeados calculados por distancia cuadrática */
    int32_t r2 = r * r;
    for (int32_t dy = 0; dy < r; ++dy) {
        int32_t cur_y = r - 1 - dy;
        int32_t dx = 0;
        while ((r - 1 - dx) * (r - 1 - dx) + cur_y * cur_y > r2 && dx < r) dx++;

        int32_t span = r - dx;
        if (span > 0) {
            int32_t py_top = y + dy;
            int32_t py_bot = y + h - 1 - dy;
            int32_t dummy_h = 1;

            if (py_top >= clip_y0 && py_top < clip_y1) {
                int32_t lx = x + dx, lw = span, ly = py_top;
                if (clip_rect(&lx, &ly, &lw, &dummy_h)) fast_fill_span(target + py_top * fb_cur_w + lx, color, lw);
                int32_t rx = x + w - r, rw = span, ry = py_top;
                if (clip_rect(&rx, &ry, &rw, &dummy_h)) fast_fill_span(target + py_top * fb_cur_w + rx, color, rw);
            }
            if (py_bot >= clip_y0 && py_bot < clip_y1) {
                int32_t lx = x + dx, lw = span, ly = py_bot;
                if (clip_rect(&lx, &ly, &lw, &dummy_h)) fast_fill_span(target + py_bot * fb_cur_w + lx, color, lw);
                int32_t rx = x + w - r, rw = span, ry = py_bot;
                if (clip_rect(&rx, &ry, &rw, &dummy_h)) fast_fill_span(target + py_bot * fb_cur_w + rx, color, rw);
            }
        }
    }
}

void fb_draw_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color)
{
    if (!fb_active || w <= 0 || h <= 0) return;
    if (r <= 0) { fb_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color); return; }

    fb_fill_rect((uint32_t)(x + r), (uint32_t)y, (uint32_t)(w - 2 * r), 2, color);
    fb_fill_rect((uint32_t)(x + r), (uint32_t)(y + h - 2), (uint32_t)(w - 2 * r), 2, color);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + r), 2, (uint32_t)(h - 2 * r), color);
    fb_fill_rect((uint32_t)(x + w - 2), (uint32_t)(y + r), 2, (uint32_t)(h - 2 * r), color);
}

void fb_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
{
    if (!fb_active) return;
    int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    while (1) {
        fb_draw_pixel((uint32_t)x0, (uint32_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void fb_fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color)
{
    if (!fb_active || r <= 0) return;
    int32_t min_x = cx - r, min_y = cy - r, max_w = r * 2, max_h = r * 2;
    if (!clip_rect(&min_x, &min_y, &max_w, &max_h)) return;

    fb_damage(min_x, min_y, max_w, max_h);
    uint32_t *target = fb_target();
    int32_t r2 = r * r;

    for (int32_t dy = -r; dy <= r; ++dy) {
        int32_t py = cy + dy;
        if (py < clip_y0 || py >= clip_y1) continue;

        int32_t span = 0;
        while (span * span + dy * dy <= r2) span++;
        if (span > 0) {
            int32_t lx = cx - span, lw = span * 2;
            int32_t dummy_y = py, dummy_h = 1;
            if (clip_rect(&lx, &dummy_y, &lw, &dummy_h)) {
                fast_fill_span(target + py * fb_cur_w + lx, color, lw);
            }
        }
    }
}

void fb_draw_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color)
{
    if (!fb_active || r <= 0) return;
    int32_t x = 0, y = r;
    int32_t d = 3 - 2 * r;
    while (x <= y) {
        fb_draw_pixel((uint32_t)(cx + x), (uint32_t)(cy + y), color);
        fb_draw_pixel((uint32_t)(cx - x), (uint32_t)(cy + y), color);
        fb_draw_pixel((uint32_t)(cx + x), (uint32_t)(cy - y), color);
        fb_draw_pixel((uint32_t)(cx - x), (uint32_t)(cy - y), color);
        fb_draw_pixel((uint32_t)(cx + y), (uint32_t)(cy + x), color);
        fb_draw_pixel((uint32_t)(cx - y), (uint32_t)(cy + x), color);
        fb_draw_pixel((uint32_t)(cx + y), (uint32_t)(cy - x), color);
        fb_draw_pixel((uint32_t)(cx - y), (uint32_t)(cy - x), color);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

void fb_gradient_v(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c_top, uint32_t c_bottom)
{
    if (!fb_active || w <= 0 || h <= 0) return;
    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    int32_t r0 = (int32_t)((c_top >> 16) & 0xFF);
    int32_t g0 = (int32_t)((c_top >> 8) & 0xFF);
    int32_t b0 = (int32_t)(c_top & 0xFF);

    int32_t r1 = (int32_t)((c_bottom >> 16) & 0xFF);
    int32_t g1 = (int32_t)((c_bottom >> 8) & 0xFF);
    int32_t b1 = (int32_t)(c_bottom & 0xFF);

    int32_t denom = (h > 1) ? (h - 1) : 1;

    for (int32_t row = 0; row < ch; ++row) {
        int32_t orig_y = (cy - y) + row;
        uint32_t r = (uint32_t)(r0 + ((r1 - r0) * orig_y) / denom);
        uint32_t g = (uint32_t)(g0 + ((g1 - g0) * orig_y) / denom);
        uint32_t b = (uint32_t)(b0 + ((b1 - b0) * orig_y) / denom);
        uint32_t col = (r << 16) | (g << 8) | b | 0xFF000000;

        uint32_t *dst_row = target + (cy + row) * fb_cur_w + cx;
        fast_fill_span(dst_row, col, cw);
    }
}

/* ====================================================================
 * MAPAS DE BITS, FOTOGRAFÍAS Y VÍDEO
 * ==================================================================== */

void fb_blit(int32_t dx, int32_t dy, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{
    if (!fb_active || !src || sw <= 0 || sh <= 0) return;
    int32_t cx = dx, cy = dy, cw = sw, ch = sh;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    int32_t off_x = cx - dx;
    int32_t off_y = cy - dy;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    for (int32_t r = 0; r < ch; ++r) {
        uint32_t *dst_row = target + (cy + r) * fb_cur_w + cx;
        const uint32_t *src_row = src + (off_y + r) * stride + off_x;
        fast_copy_span(dst_row, src_row, cw);
    }
}

void fb_blit_alpha(int32_t dx, int32_t dy, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{
    if (!fb_active || !src || sw <= 0 || sh <= 0) return;
    int32_t cx = dx, cy = dy, cw = sw, ch = sh;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    int32_t off_x = cx - dx;
    int32_t off_y = cy - dy;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    for (int32_t r = 0; r < ch; ++r) {
        uint32_t *dst_row = target + (cy + r) * fb_cur_w + cx;
        const uint32_t *src_row = src + (off_y + r) * stride + off_x;
        for (int32_t c = 0; c < cw; ++c) {
            dst_row[c] = fb_blend_color(dst_row[c], src_row[c]);
        }
    }
}

void fb_blit_scale(int32_t dx, int32_t dy, int32_t dw, int32_t dh, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride)
{
    if (!fb_active || !src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    int32_t cx = dx, cy = dy, cw = dw, ch = dh;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;

    fb_damage(cx, cy, cw, ch);
    uint32_t *target = fb_target();

    uint32_t step_x = ((uint32_t)sw << 16) / (uint32_t)dw;
    uint32_t step_y = ((uint32_t)sh << 16) / (uint32_t)dh;

    uint32_t start_src_y = (uint32_t)(cy - dy) * step_y;
    uint32_t start_src_x = (uint32_t)(cx - dx) * step_x;

    for (int32_t r = 0; r < ch; ++r) {
        uint32_t sy = (start_src_y + (uint32_t)r * step_y) >> 16;
        if (sy >= (uint32_t)sh) sy = (uint32_t)(sh - 1);

        const uint32_t *src_row = src + sy * stride;
        uint32_t *dst_row = target + (cy + r) * fb_cur_w + cx;

        uint32_t curr_x = start_src_x;
        for (int32_t c = 0; c < cw; ++c) {
            uint32_t sx = curr_x >> 16;
            if (sx >= (uint32_t)sw) sx = (uint32_t)(sw - 1);
            dst_row[c] = src_row[sx];
            curr_x += step_x;
        }
    }
}

/* ====================================================================
 * RENDERIZADO DE GLIFOS
 * ==================================================================== */

void fb_draw_glyph8_nodmg(int32_t x, int32_t y, const uint8_t *glyph, uint32_t fg, int scale)
{
    if (!fb_active || !glyph || scale <= 0) return;
    uint32_t *target = fb_target();

    for (int r = 0; r < 8; ++r) {
        uint8_t row_bits = glyph[r];
        if (!row_bits) continue;
        for (int col = 0; col < 8; ++col) {
            if (row_bits & (1 << (7 - col))) {
                int32_t px = x + col * scale;
                int32_t py = y + r * scale;
                if (scale == 1) {
                    if (px >= clip_x0 && px < clip_x1 && py >= clip_y0 && py < clip_y1) {
                        target[py * fb_cur_w + px] = fg;
                    }
                } else {
                    int32_t cx = px, cy = py, cw = scale, ch = scale;
                    if (clip_rect(&cx, &cy, &cw, &ch)) {
                        for (int32_t sr = 0; sr < ch; ++sr) {
                            uint32_t *drow = target + (cy + sr) * fb_cur_w + cx;
                            fast_fill_span(drow, fg, cw);
                        }
                    }
                }
            }
        }
    }
}

void fb_draw_glyph8(int32_t x, int32_t y, const uint8_t *glyph, uint32_t fg, int scale)
{
    if (!fb_active || !glyph || scale <= 0) return;
    fb_damage(x, y, 8 * scale, 8 * scale);
    fb_draw_glyph8_nodmg(x, y, glyph, fg, scale);
}


/* ==== SOMA_VECTOR_FONTS_PATCH: mascara alfa de 8 bits (texto/iconos vectoriales) ====
 * Mezcla 'rgb' sobre el backbuffer usando 'mask' (w*h bytes de cobertura) y un
 * alfa global 0..255. Respeta el recorte (scissor) y registra el area sucia. */
void fb_draw_mask8(int32_t x, int32_t y, const uint8_t *mask, int32_t w, int32_t h,
                   uint32_t rgb, uint32_t alpha)
{
    if (!fb_active || !mask || w <= 0 || h <= 0) return;
    if (alpha > 255u) alpha = 255u;

    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!clip_rect(&cx, &cy, &cw, &ch)) return;
    fb_damage(cx, cy, cw, ch);

    uint32_t *target = fb_target();
    uint32_t sr = (rgb >> 16) & 0xFFu;
    uint32_t sg = (rgb >> 8) & 0xFFu;
    uint32_t sb = rgb & 0xFFu;

    for (int32_t r = 0; r < ch; ++r) {
        const uint8_t *mrow = mask + (int32_t)(cy + r - y) * w + (cx - x);
        uint32_t *drow = target + (uint32_t)(cy + r) * fb_cur_w + (uint32_t)cx;
        for (int32_t c = 0; c < cw; ++c) {
            uint32_t a = mrow[c];
            if (!a) continue;
            if (alpha != 255u) a = (a * alpha + 127u) / 255u;
            if (!a) continue;
            if (a >= 255u) { drow[c] = rgb & 0x00FFFFFFu; continue; }
            uint32_t bg = drow[c];
            uint32_t ia = 255u - a;
            uint32_t rr = (sr * a + ((bg >> 16) & 0xFFu) * ia + 127u) / 255u;
            uint32_t gg = (sg * a + ((bg >> 8) & 0xFFu) * ia + 127u) / 255u;
            uint32_t bb = (sb * a + (bg & 0xFFu) * ia + 127u) / 255u;
            drow[c] = (rr << 16) | (gg << 8) | bb;
        }
    }
}
