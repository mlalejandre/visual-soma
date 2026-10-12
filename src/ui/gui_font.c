#include "gui_font.h"
#include "fb.h"
#include "fonts/font_classic.h"
#include "fonts/font_modern.h"
#include "fonts/font_extra.h"
#include "fonts/font_extra2.h"
#include "fonts/font_extra3.h"
#include "fonts/font_extra4.h"
#include "fonts/vraster.h"
#include "fonts/vfont.h"
#include "fonts/vicon.h"
#include "fonts/vfont_data.h"
/* SOMA_VECTOR_FONTS_PATCH */

struct soma_font_entry {
    const char *name;
    const char *desc;
    const uint8_t (*glyphs)[8];
};

static const struct soma_font_entry SOMA_FONTS[FONT_COUNT] = {
    [FONT_CLASSIC] = { "classic", "Fuente Clasica IBM/BIOS 8x8 (Pixel Art)", font_glyph_classic },
    [FONT_MODERN] = { "modern", "Fuente Moderna Sans-Serif 8x8 (Clean)", font_glyph_modern },
    [FONT_BOLD] = { "bold", "Clasica Negrita 8x8", font_glyph_bold },
    [FONT_MODERN_BOLD] = { "modern-bold", "Moderna Negrita 8x8", font_glyph_modern_bold },
    [FONT_ITALIC] = { "italic", "Clasica Cursiva 8x8", font_glyph_italic },
    [FONT_MODERN_ITALIC] = { "modern-italic", "Moderna Cursiva 8x8", font_glyph_modern_italic },
    [FONT_CAPS] = { "caps", "Clasica Solo Mayusculas 8x8", font_glyph_caps },
    [FONT_MODERN_CAPS] = { "modern-caps", "Moderna Solo Mayusculas 8x8", font_glyph_modern_caps },
    [FONT_BOLD_ITALIC] = { "bold-italic", "Clasica Negrita Cursiva 8x8", font_glyph_bold_italic },
    [FONT_MODERN_BOLD_ITALIC] = { "modern-bold-italic", "Moderna Negrita Cursiva 8x8", font_glyph_modern_bold_italic },
    [FONT_CLASSIC_ZERO] = { "classic-zero", "Clasica con Cero Tachado 8x8", font_glyph_classic_zero },
    [FONT_MODERN_HEAVY] = { "modern-heavy", "Moderna Extra Gruesa 8x8", font_glyph_modern_heavy },
    [FONT_BACKSLANT] = { "backslant", "Clasica Inclinada a la Izquierda 8x8", font_glyph_backslant },
    [FONT_MODERN_BACKSLANT] = { "modern-backslant", "Moderna Inclinada a la Izquierda 8x8", font_glyph_modern_backslant },
    [FONT_LOWER] = { "lower", "Clasica Solo Minusculas 8x8", font_glyph_lower },
    [FONT_MODERN_LOWER] = { "modern-lower", "Moderna Solo Minusculas 8x8", font_glyph_modern_lower },
    [FONT_HYBRID] = { "hybrid", "Hibrida: letras Modernas + simbolos Clasicos 8x8", font_glyph_hybrid },
    [FONT_HYBRID_ALT] = { "hybrid-alt", "Hibrida: letras Clasicas + simbolos Modernos 8x8", font_glyph_hybrid_alt },
    [FONT_ROUNDED] = { "rounded", "Redondeada de Trazo Grueso 8x8", font_glyph_rounded },
    [FONT_ROUNDED_ITALIC] = { "rounded-italic", "Redondeada Cursiva 8x8", font_glyph_rounded_italic },
    [FONT_LCD] = { "lcd", "Display LCD 7 Segmentos 8x8", font_glyph_lcd },
    [FONT_LCD_ITALIC] = { "lcd-italic", "Display LCD Inclinado 8x8", font_glyph_lcd_italic },
    [FONT_LCD_BOLD] = { "lcd-bold", "Display LCD Grueso 8x8", font_glyph_lcd_bold },
    [FONT_SEGOE] = { "segoe", "Sans humanista limpio, inspirado en Segoe UI 8x8", font_glyph_segoe },
    [FONT_ARIAL] = { "arial", "Sans clásico legible, inspirado en Arial 8x8", font_glyph_arial },
    [FONT_VERDANA] = { "verdana", "Sans ancho y abierto, inspirado en Verdana 8x8", font_glyph_verdana },
    [FONT_TAHOMA] = { "tahoma", "Sans compacto, inspirado en Tahoma 8x8", font_glyph_tahoma },
    [FONT_CONSOLAS] = { "consolas", "Monoespaciada técnica, inspirada en Consolas 8x8", font_glyph_consolas },
    [FONT_GEORGIA] = { "georgia", "Serif bitmap, inspirado en Georgia 8x8", font_glyph_georgia },
    [FONT_TREBUCHET] = { "trebuchet", "Sans dinámica, inspirada en Trebuchet MS 8x8", font_glyph_trebuchet },
    [FONT_UBUNTU] = { "ubuntu", "Sans geométrico de trazo marcado, estilo Ubuntu 8x8", font_glyph_ubuntu },
    [FONT_COMIC] = { "comic", "Estilo informal redondeado tipo cómic 8x8", font_glyph_comic },
    [FONT_VECTOR] = { "vector", "Vectorial TrueType suavizada y escalable", font_glyph_classic }
};

static int current_font_id = FONT_VECTOR;

int font_set(int font_id)
{
    if (font_id < 0 || font_id >= FONT_COUNT) return 0;
    current_font_id = font_id;
    return 1;
}

int font_set_by_name(const char *name)
{
    if (!name || !*name) return 0;
    for (int i = 0; i < FONT_COUNT; ++i) {
        const char *a = name, *b = SOMA_FONTS[i].name;
        int eq = 1;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
            if (ca != cb) { eq = 0; break; }
            a++; b++;
        }
        if (eq && *a == '\0' && *b == '\0') {
            current_font_id = i;
            return 1;
        }
    }
    return 0;
}

int font_get_current(void)
{
    return current_font_id;
}

const char *font_get_name(int font_id)
{
    if (font_id < 0 || font_id >= FONT_COUNT) return "";
    return SOMA_FONTS[font_id].name;
}

const char *font_get_desc(int font_id)
{
    if (font_id < 0 || font_id >= FONT_COUNT) return "";
    return SOMA_FONTS[font_id].desc;
}

int font_get_count(void)
{
    return FONT_COUNT;
}

/* Compara nombres ASCII sin distinguir mayúsculas/minúsculas. */
static int font_name_compare(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca < cb) return -1;
        if (ca > cb) return 1;
        ++a;
        ++b;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

/* Devuelve el ID real que ocupa una posición del listado alfabético.
 * Así se ordena `font` sin renumerar IDs ni romper `font <id>`. */
int font_get_sorted_id(int order)
{
    if (order < 0 || order >= FONT_COUNT) return -1;
    for (int i = 0; i < FONT_COUNT; ++i) {
        int rank = 0;
        for (int j = 0; j < FONT_COUNT; ++j) {
            int cmp = font_name_compare(SOMA_FONTS[j].name, SOMA_FONTS[i].name);
            if (cmp < 0 || (cmp == 0 && j < i)) ++rank;
        }
        if (rank == order) return i;
    }
    return -1;
}

const uint8_t *font_get_glyph(char c)
{
    if (c < 32 || c > 126) c = '?';
    return SOMA_FONTS[current_font_id].glyphs[(uint32_t)(c - 32)];
}

static void font_draw_char_bitmap(uint32_t x, uint32_t y, char c, uint32_t fg, int scale)
{
    const uint8_t *glyph = font_get_glyph(c);
    fb_draw_glyph8((int32_t)x, (int32_t)y, glyph, fg, scale);
}


struct soma_u8_dec {
    char base;
    uint8_t mark; /* 0=ninguno, 1=agudo, 2=virgulilla, 3=dieresis, 4=invertido */
    uint8_t bytes;
};

static inline struct soma_u8_dec soma_font_decode_utf8(const char *s)
{
    struct soma_u8_dec r = { '?', 0, 1 };
    uint8_t b1 = (uint8_t)s[0];
    if (b1 < 0x80) {
        r.base = (char)b1;
        r.mark = 0;
        r.bytes = 1;
        return r;
    }
    uint8_t b2 = (uint8_t)s[1];
    if (b1 == 0xC3 && b2) {
        r.bytes = 2;
        switch (b2) {
        case 0xA1: r.base = 'a'; r.mark = 1; return r; /* á */
        case 0xA9: r.base = 'e'; r.mark = 1; return r; /* é */
        case 0xAD: r.base = 'i'; r.mark = 1; return r; /* í */
        case 0xB3: r.base = 'o'; r.mark = 1; return r; /* ó */
        case 0xBA: r.base = 'u'; r.mark = 1; return r; /* ú */
        case 0x81: r.base = 'A'; r.mark = 1; return r; /* Á */
        case 0x89: r.base = 'E'; r.mark = 1; return r; /* É */
        case 0x8D: r.base = 'I'; r.mark = 1; return r; /* Í */
        case 0x93: r.base = 'O'; r.mark = 1; return r; /* Ó */
        case 0x9A: r.base = 'U'; r.mark = 1; return r; /* Ú */
        case 0xB1: r.base = 'n'; r.mark = 2; return r; /* ñ */
        case 0x91: r.base = 'N'; r.mark = 2; return r; /* Ñ */
        case 0xBC: r.base = 'u'; r.mark = 3; return r; /* ü */
        case 0x9C: r.base = 'U'; r.mark = 3; return r; /* Ü */
        default:   break;
        }
    } else if (b1 == 0xC2 && b2) {
        r.bytes = 2;
        switch (b2) {
        case 0xBF: r.base = '?'; r.mark = 4; return r; /* ¿ */
        case 0xA1: r.base = '!'; r.mark = 4; return r; /* ¡ */
        default:   break;
        }
    }
    if ((b1 & 0xE0) == 0xC0) r.bytes = 2;
    else if ((b1 & 0xF0) == 0xE0) r.bytes = 3;
    else if ((b1 & 0xF8) == 0xF0) r.bytes = 4;
    return r;
}

static void soma_draw_glyph_composed(int32_t x, int32_t y, char base, uint8_t mark, uint32_t fg, int scale)
{
    const uint8_t *glyph = font_get_glyph(base);
    uint8_t tmp[8];
    for (int r = 0; r < 8; ++r) {
        tmp[r] = (mark == 4) ? glyph[7 - r] : glyph[r];
    }
    if (mark != 0 && mark != 4 && base == 'i') {
        tmp[0] = 0;
    }
    fb_draw_glyph8_nodmg(x, y, tmp, fg, scale);
    if (!mark || mark == 4) return;

    int32_t s = scale;
    int32_t oy = (base >= 'A' && base <= 'Z') ? y - 2 * s : y;
    switch (mark) {
    case 1:
        fb_fill_rect((uint32_t)(x + 4 * s), (uint32_t)oy, (uint32_t)s, (uint32_t)s, fg);
        fb_fill_rect((uint32_t)(x + 3 * s), (uint32_t)(oy + s), (uint32_t)s, (uint32_t)s, fg);
        break;
    case 2:
        fb_fill_rect((uint32_t)(x + 2 * s), (uint32_t)(oy + s), (uint32_t)s, (uint32_t)s, fg);
        fb_fill_rect((uint32_t)(x + 3 * s), (uint32_t)oy, (uint32_t)s, (uint32_t)s, fg);
        fb_fill_rect((uint32_t)(x + 4 * s), (uint32_t)oy, (uint32_t)s, (uint32_t)s, fg);
        fb_fill_rect((uint32_t)(x + 5 * s), (uint32_t)(oy + s), (uint32_t)s, (uint32_t)s, fg);
        break;
    case 3:
        fb_fill_rect((uint32_t)(x + 2 * s), (uint32_t)(oy + s), (uint32_t)s, (uint32_t)s, fg);
        fb_fill_rect((uint32_t)(x + 5 * s), (uint32_t)(oy + s), (uint32_t)s, (uint32_t)s, fg);
        break;
    default:
        break;
    }
}

static void font_draw_string_bitmap(uint32_t x, uint32_t y, const char *str, uint32_t fg, int scale)
{
    if (!str || scale <= 0) return;
    uint32_t cur_x = x;
    uint32_t cur_y = y;
    uint32_t max_x = x;

    const char *p = str;
    uint32_t line_count = 1;
    uint32_t scan_x = x;

    while (*p) {
        if (*p == '\n') {
            line_count++;
            if (scan_x > max_x) max_x = scan_x;
            scan_x = x;
            p++;
        } else {
            struct soma_u8_dec dec = soma_font_decode_utf8(p);
            scan_x += (uint32_t)(8 * scale);
            p += dec.bytes;
        }
    }
    if (scan_x > max_x) max_x = scan_x;

    fb_damage((int32_t)x, (int32_t)y, (int32_t)(max_x - x), (int32_t)(line_count * 10 * scale));

    p = str;
    while (*p) {
        if (*p == '\n') {
            cur_x = x;
            cur_y += (uint32_t)(10 * scale);
            p++;
            continue;
        }
        struct soma_u8_dec dec = soma_font_decode_utf8(p);
        soma_draw_glyph_composed((int32_t)cur_x, (int32_t)cur_y, dec.base, dec.mark, fg, scale);
        cur_x += (uint32_t)(8 * scale);
        p += dec.bytes;
    }
}

static void font_draw_string_ansi_bitmap(uint32_t x, uint32_t y, const char *str, uint32_t def_color)
{
    if (!str) return;
    uint32_t cur_x = x;
    uint32_t cur_col = def_color;

    const char *p = str;
    uint32_t vis_chars = 0;
    while (*p) {
        if ((uint8_t)*p == 27 && *(p + 1) == '[') {
            p += 2;
            while ((*p >= '0' && *p <= '9') || *p == ';') p++;
            if (*p) p++;
            continue;
        }
        vis_chars++;
        p++;
    }
    fb_damage((int32_t)x, (int32_t)y, (int32_t)(vis_chars * 8), 10);

    while (*str) {
        if ((uint8_t)*str == 27 && *(str + 1) == '[') {
            str += 2;
            int code = 0;
            while ((*str >= '0' && *str <= '9') || *str == ';') {
                if (*str == ';') code = 0;
                else code = code * 10 + (*str - '0');
                str++;
            }
            if (*str == 'm') {
                if (code == 0) cur_col = def_color;
                else if (code == 30) cur_col = 0x00475569;
                else if (code == 31) cur_col = 0x00EF4444;
                else if (code == 32) cur_col = 0x0010B981;
                else if (code == 33) cur_col = 0x00F59E0B;
                else if (code == 34) cur_col = 0x003B82F6;
                else if (code == 35) cur_col = 0x00D946EF;
                else if (code == 36) cur_col = 0x0038BDF8;
                else if (code == 37) cur_col = 0x00F8FAFC;
                str++;
                continue;
            }
        }

        char ch = *str++;
        const uint8_t *glyph = font_get_glyph(ch);
        fb_draw_glyph8_nodmg((int32_t)cur_x, (int32_t)y, glyph, cur_col, 1);
        cur_x += 8;
    }
}


/* ======================================================================
 * SOMA_VECTOR_FONTS_PATCH: capa vectorial (FONT_VECTOR)
 * ====================================================================== */
static int vf_loaded = 0;   /* 0 = sin intentar, 1 = OK, -1 = fallo */

static int vf_active(void)
{
    if (current_font_id != FONT_VECTOR) return 0;
    if (vf_loaded == 0)
        vf_loaded = vfont_init(vfont_ttf_data, vfont_ttf_len) ? 1 : -1;
    return vf_loaded == 1;
}

/* Decodificador UTF-8 -> codepoint */
static uint32_t vf_utf8(const char *s, int *n)
{
    uint8_t b = (uint8_t)s[0];
    if (b < 0x80) { *n = 1; return b; }
    if ((b & 0xE0) == 0xC0 && ((uint8_t)s[1] & 0xC0) == 0x80) {
        *n = 2;
        return ((uint32_t)(b & 0x1F) << 6) | ((uint8_t)s[1] & 0x3Fu);
    }
    if ((b & 0xF0) == 0xE0 && ((uint8_t)s[1] & 0xC0) == 0x80 && ((uint8_t)s[2] & 0xC0) == 0x80) {
        *n = 3;
        return ((uint32_t)(b & 0x0F) << 12) | (((uint32_t)(uint8_t)s[1] & 0x3Fu) << 6)
             | ((uint8_t)s[2] & 0x3Fu);
    }
    if ((b & 0xF8) == 0xF0 && ((uint8_t)s[1] & 0xC0) == 0x80 && ((uint8_t)s[2] & 0xC0) == 0x80
        && ((uint8_t)s[3] & 0xC0) == 0x80) {
        *n = 4;
        return ((uint32_t)(b & 0x07) << 18) | (((uint32_t)(uint8_t)s[1] & 0x3Fu) << 12)
             | (((uint32_t)(uint8_t)s[2] & 0x3Fu) << 6) | ((uint8_t)s[3] & 0x3Fu);
    }
    *n = 1;
    return '?';
}

static void vf_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg, int scale)
{
    uint32_t cx = x, cy = y;
    const char *p = str;
    while (*p) {
        if (*p == '\n') { cx = x; cy += (uint32_t)(10 * scale); p++; continue; }
        int n = 1;
        uint32_t cp = vf_utf8(p, &n);
        vfont_draw_cp((int32_t)cx, (int32_t)cy, cp, fg, scale);
        cx += (uint32_t)(8 * scale);
        p += n;
    }
}

static void vf_draw_string_ansi(uint32_t x, uint32_t y, const char *str, uint32_t def_color)
{
    uint32_t cx = x, col = def_color;
    while (*str) {
        if ((uint8_t)*str == 27 && str[1] == '[') {
            const char *q = str + 2;
            int code = 0;
            while ((*q >= '0' && *q <= '9') || *q == ';') {
                if (*q == ';') code = 0;
                else code = code * 10 + (*q - '0');
                q++;
            }
            if (*q == 'm') {
                if (code == 0) col = def_color;
                else if (code == 30) col = 0x00475569;
                else if (code == 31) col = 0x00EF4444;
                else if (code == 32) col = 0x0010B981;
                else if (code == 33) col = 0x00F59E0B;
                else if (code == 34) col = 0x003B82F6;
                else if (code == 35) col = 0x00D946EF;
                else if (code == 36) col = 0x0038BDF8;
                else if (code == 37) col = 0x00F8FAFC;
                str = q + 1;
                continue;
            }
            str = q;
            if (!*str) break;
        }
        int n = 1;
        uint32_t cp = vf_utf8(str, &n);
        vfont_draw_cp((int32_t)cx, (int32_t)y, cp, col, 1);
        cx += 8;
        str += n;
    }
}

void font_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, int scale)
{
    if (vf_active()) {
        uint32_t cp = ((uint8_t)c >= 32 && (uint8_t)c < 127) ? (uint8_t)c : (uint32_t)'?';
        vfont_draw_cp((int32_t)x, (int32_t)y, cp, fg, scale);
        return;
    }
    font_draw_char_bitmap(x, y, c, fg, scale);
}

void font_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg, int scale)
{
    if (!str || scale <= 0) return;
    if (vf_active()) { vf_draw_string(x, y, str, fg, scale); return; }
    font_draw_string_bitmap(x, y, str, fg, scale);
}

void font_draw_string_ansi(uint32_t x, uint32_t y, const char *str, uint32_t def_color)
{
    if (!str) return;
    if (vf_active()) { vf_draw_string_ansi(x, y, str, def_color); return; }
    font_draw_string_ansi_bitmap(x, y, str, def_color);
}

int font_vector_set_size(int percent)
{
    if (percent < 40) percent = 40;
    if (percent > 300) percent = 300;
    vf_pct = percent;
    return vf_pct;
}

int font_vector_get_size(void) { return vf_pct; }

int font_vector_set_contrast(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    vf_contrast = percent;
    vr_set_contrast(percent);
    vf_cache_flush();
    return vf_contrast;
}

int font_vector_get_contrast(void) { return vf_contrast; }

void vicon_draw(int id, int32_t x, int32_t y, int32_t size, uint32_t color)
{
    if (id < 0 || id >= VICON_COUNT) return;
    if (size < 8) size = 8;
    if (size > VR_MAX_DIM) size = VR_MAX_DIM;
    const uint8_t *m = vi_mask(id, size);
    if (!m) return;
    uint32_t a = color >> 24;
    if (a == 0) a = 255;
    fb_draw_mask8(x, y, m, size, size, color & 0x00FFFFFFu, a);
}

void vicon_draw_tile(int id, int32_t x, int32_t y, int32_t size, uint32_t fg, uint32_t bg)
{
    if (id < 0 || id >= VICON_COUNT) return;
    if (size < 8) size = 8;
    if (size > VR_MAX_DIM) size = VR_MAX_DIM;
    const uint8_t *t = vi_mask(VI_TILE_ID, size);
    if (t) {
        uint32_t a = bg >> 24;
        if (a == 0) a = 255;
        fb_draw_mask8(x, y, t, size, size, bg & 0x00FFFFFFu, a);
    }
    vicon_draw(id, x, y, size, fg);
}


/* ==== SOMA_VECTOR_CELL_PATCH: celdas del escritorio HER en vectorial ==== */
int font_draw_cell_vector(int32_t x, int32_t y, uint8_t base, uint32_t mark,
                          uint32_t col, int sc)
{
    if (sc <= 0 || !vf_active()) return 0;
    uint32_t cp = base;
    if (mark == 1u) {
        switch (base) {
        case 'a': cp = 0xE1u; break;
        case 'e': cp = 0xE9u; break;
        case 'i': cp = 0xEDu; break;
        case 'o': cp = 0xF3u; break;
        case 'u': cp = 0xFAu; break;
        case 'A': cp = 0xC1u; break;
        case 'E': cp = 0xC9u; break;
        case 'I': cp = 0xCDu; break;
        case 'O': cp = 0xD3u; break;
        case 'U': cp = 0xDAu; break;
        default: break;
        }
    } else if (mark == 2u) {
        if (base == 'n') cp = 0xF1u;
        else if (base == 'N') cp = 0xD1u;
    } else if (mark == 3u) {
        if (base == 'u') cp = 0xFCu;
        else if (base == 'U') cp = 0xDCu;
    } else if (mark == 4u) {
        if (base == '?') cp = 0xBFu;
        else if (base == '!') cp = 0xA1u;
    }
    if (cp < 32u) return 1;
    vfont_draw_cp(x, y, cp, col, sc);
    return 1;
}
