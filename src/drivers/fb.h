#ifndef SOMA_FB_H
#define SOMA_FB_H

#include <stdint.h>

/* Resoluciones estándar */
#define FB_RES_FHD_W     1920
#define FB_RES_FHD_H     1080

#define FB_RES_HD_W      1280
#define FB_RES_HD_H      720

#define FB_RES_SVGA_W    800
#define FB_RES_SVGA_H    600

#define FB_VIRT_ADDR     0x00000000E0000000ULL

/* Paleta de colores TrueColor (0x00RRGGBB / 0xAARRGGBB) */
#define COLOR_DEEP_NAVY  0x000F172A
#define COLOR_ROYAL_BLUE 0x001E3A8A
#define COLOR_PURPLE     0x00311042
#define COLOR_EMERALD    0x00064E3B
#define COLOR_DARK       0x00090D13
#define COLOR_WHITE      0x00FFFFFF
#define COLOR_CYAN       0x0038BDF8
#define COLOR_CARD_BG    0x001E293B

/* Inicialización y control de resolución */
int  fb_init(void);
int  fb_set_mode(uint32_t width, uint32_t height);
int  fb_set_mode_hd(void);
void fb_restore_text_mode(void);
int  fb_is_active(void);

uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
volatile uint32_t *fb_get_buffer(void);

/* Gestión de Doble Búfer y Presentación (lista de rectángulos sucios) */
void fb_damage(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_present(void);
void fb_present_full(void);

/* Recorte (Scissor Clipping) */
void fb_set_clip(int32_t x, int32_t y, int32_t w, int32_t h);
void fb_reset_clip(void);
void fb_get_clip(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1);

/* Primitivas gráficas optimizadas */
void fb_clear(uint32_t color);
void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* Geometría moderna para UI y Web */
void fb_fill_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
void fb_draw_rounded_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
void fb_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void fb_draw_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);
void fb_fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);
void fb_gradient_v(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c_top, uint32_t c_bottom);

/* Transparencias y Composición Alfa */
uint32_t fb_blend_color(uint32_t bg, uint32_t fg);
void fb_fill_rect_alpha(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);

/* Renderizado de Mapas de Bits, Fotografías y Frames de Vídeo */
void fb_blit(int32_t dx, int32_t dy, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride);
void fb_blit_alpha(int32_t dx, int32_t dy, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride);
void fb_blit_scale(int32_t dx, int32_t dy, int32_t dw, int32_t dh, const uint32_t *src, int32_t sw, int32_t sh, int32_t stride);

/* SOMA_VECTOR_FONTS_PATCH: mascara alfa 8 bpp (texto e iconos vectoriales) */
void fb_draw_mask8(int32_t x, int32_t y, const uint8_t *mask, int32_t w, int32_t h,
                   uint32_t rgb, uint32_t alpha);

/* Glifos 8x8 1bpp */
void fb_draw_glyph8(int32_t x, int32_t y, const uint8_t *glyph, uint32_t fg, int scale);
void fb_draw_glyph8_nodmg(int32_t x, int32_t y, const uint8_t *glyph, uint32_t fg, int scale);

/* Cursor overlay: compuesto directamente en VRAM al presentar */
void fb_cursor_set_image(const uint32_t *argb, int32_t w, int32_t h);
void fb_cursor_show(int32_t x, int32_t y);
void fb_cursor_hide(void);

#include "gui_font.h"
#include "mouse.h"
#include "gui.h"

#endif