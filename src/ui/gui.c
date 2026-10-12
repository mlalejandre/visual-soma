/* SOMA_PARCHE_V2_GUI */
/* SOMA FIX CMD_VSOMA DUP v3 */
/* SOMA GLOBAL AGENT SHELL PATCH v2 */
#include "gui.h"
#include "fb.h"
#include "gui_font.h"
#include "icons/icons.h"
#include "mouse.h"
#include "browser.h"
#include "imgview.h"
#include "fm.h"
#include "notepad.h"
#include "memview.h"
#include "layout.h"
#include "paint.h"
#include "console.h"
#include "idt.h"
#include "thread.h"
#include "arch.h"
#include "agent.h"
#include "shell.h"
#include "rtc.h"
#include "fs.h"
#include "mem.h"

enum app_mode {
    APP_DESKTOP = 0,
    APP_AGENT_CHAT,
    APP_TERMINAL_SHELL,
    APP_WEB_BROWSER,
    APP_IMAGE_VIEWER,
    APP_FILE_MANAGER,
    APP_NOTEPAD,
    APP_AGENT_MEMORY,
    APP_LAYOUT,
    APP_AGENTS,
    APP_PAINT
};

static enum app_mode current_app = APP_DESKTOP;

/* SOMA MULTI-WINDOW STACKING ARCHITECTURE */
#define HER_MAX_WINDOWS 10

struct her_win_node {
    enum app_mode app;
    uint32_t x, y, w, h;
    int dragging;
    int resizing;
    int resize_edge;
    int32_t drag_ox, drag_oy;
};

static struct her_win_node her_windows[HER_MAX_WINDOWS];
static int her_num_windows = 0;       /* Ventanas abiertas activas */
static int her_active_win_idx = -1;   /* Índice de la ventana enfocada en el tope */

static struct her_win_node *her_get_active_win(void)
{
    if (her_active_win_idx >= 0 && her_active_win_idx < her_num_windows)
        return &her_windows[her_active_win_idx];
    return 0;
}

static int her_find_window(enum app_mode app)
{
    for (int i = 0; i < her_num_windows; ++i) {
        if (her_windows[i].app == app) return i;
    }
    return -1;
}

static void her_bring_to_front(int idx)
{
    if (idx < 0 || idx >= her_num_windows || idx == her_num_windows - 1) return;
    struct her_win_node tmp = her_windows[idx];
    for (int i = idx; i < her_num_windows - 1; ++i) {
        her_windows[i] = her_windows[i + 1];
    }
    her_windows[her_num_windows - 1] = tmp;
    her_active_win_idx = her_num_windows - 1;
    current_app = tmp.app;
}

#define GUI_MAX_LOG_LINES 256
#define GUI_LINE_MAX_LEN  256

struct gui_chat_line { char text[GUI_LINE_MAX_LEN]; uint32_t color; };
static struct gui_chat_line *agent_log = 0;
static int agent_log_count = 0;

static void ensure_agent_log(void)
{
    if (!agent_log) {
        agent_log = (struct gui_chat_line *)kmalloc(sizeof(struct gui_chat_line) * GUI_MAX_LOG_LINES);
        if (agent_log) {
            memset((void *)agent_log, 0, sizeof(struct gui_chat_line) * GUI_MAX_LOG_LINES);
        }
    }
}

#define SOMA_AGENT_BAR_H        248u
#define SOMA_SYSTEM_BAR_H       44u
#define SOMA_WINDOW_GAP         10u
#define SOMA_AGENT_CMD_MAX      256u
#define SOMA_AGENT_LINE_H       16u
#define SOMA_AGENT_RESP_TOP     42u
#define SOMA_AGENT_RESP_H       148u
#define SOMA_DOCK_ROW1          (SOMA_AGENT_BAR_H + 34u)
#define SOMA_DOCK_ROW2          (SOMA_AGENT_BAR_H + 112u)
#define SOMA_DOCK_ROW3          (SOMA_AGENT_BAR_H + 190u)
#define SOMA_DOCK_ROW4          (SOMA_AGENT_BAR_H + 268u)
#define SOMA_DOCK_ROW5          (SOMA_AGENT_BAR_H + 346u)
#define SOMA_DOCK_COL1          40u
#define SOMA_DOCK_COL2          230u
#define SOMA_DOCK_ICON_W        150u
#define SOMA_DOCK_ICON_H        68u

static char agent_global_cmd[SOMA_AGENT_CMD_MAX];
static uint32_t agent_global_len = 0;
static uint32_t agent_global_cursor = 0;
static int agent_global_focus = 1;
static uint32_t agent_global_scroll = 0;

/* --- visibilidad / texto fantasma --- */
static int      her_icons_visible   = 0;
static int      her_vortex_closing  = 0;
static int      her_dock_pending_open = 0;
static int      soma_text_vortex    = 0;
/* Evita que la respuesta anterior reaparezca mientras el dock orbital esta activo. */
static int      her_text_suppressed_by_dock = 0;
/* SOMA ORBITAL ICONS PATCH v2: transicion y destello, sin buffers nuevos. */
static uint32_t her_icons_anim_phase = 0; /* 0 = ocultos; 256 = orbita estable */
static uint64_t her_icons_burst_at = 0;
static int her_icons_burst_opening = 1;
/* Estado de oscilacion axial lenta; no requiere memoria dinamica. */
static int32_t her_axis_phase = 0;
static int32_t her_axis_target = 0;
static uint64_t her_axis_next_change = 0;
static int      her_has_user_text   = 0;
static int      her_has_agent_text  = 0;
static uint32_t her_angle           = 0;
static uint64_t her_last_anim       = 0;

static int her_cursor_should_show(void)
{
    return her_icons_visible || her_num_windows > 0 || current_app != APP_DESKTOP;
}

/* --- geometría de ventana libre (drag + resize) --- */
static uint32_t her_win_x = 80,  her_win_y = 60;
static uint32_t her_win_w = 720, her_win_h = 480;
static int      her_dragging  = 0;
static int      her_resizing  = 0;
static int      her_resize_edge = 0;
static int32_t  her_drag_ox = 0, her_drag_oy = 0;
static int32_t  her_resize_start_mx = 0, her_resize_start_my = 0;
static int32_t  her_resize_start_x  = 0, her_resize_start_y  = 0;
static int32_t  her_resize_start_w  = 0, her_resize_start_h  = 0;

#define HER_TITLE_H   28
#define HER_EDGE      10
#define HER_MIN_W   280
#define HER_MIN_H   180

/* SOMA_PARCHE_V2_GUI: anchos minimos utiles por aplicacion */
static uint32_t her_app_min_w(enum app_mode app)
{
    switch (app) {
    case APP_WEB_BROWSER:    return 520u;
    case APP_AGENT_MEMORY:   return 600u;
    case APP_FILE_MANAGER:   return 480u;
    case APP_IMAGE_VIEWER:   return 460u;
    case APP_NOTEPAD:        return 400u;
    case APP_LAYOUT:         return 420u;
    case APP_PAINT:          return 420u;
    case APP_TERMINAL_SHELL: return 360u;
    case APP_AGENTS:         return 400u;
    default:                 return 340u;
    }
}

/* ====================================================================
 * MATEMÁTICA Y ESTRUCTURAS DEL VÓRTICE CÓSMICO (RING 0 / FIXED-POINT)
 * ==================================================================== */
static uint32_t her_isqrt(uint32_t val)
{
    uint32_t res = 0;
    uint32_t bit = 1u << 30;
    while (bit > val) bit >>= 2;
    while (bit != 0) {
        if (val >= res + bit) {
            val -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

#define HER_TORUS_PART  160  /* Rosquilla de partículas en acreción */
#define HER_STARS_PART  48   /* Estrellas cósmicas en toda la interfaz */
#define HER_MAX_PART    (HER_TORUS_PART + HER_STARS_PART)

struct her_particle {
    uint16_t angle;          /* Ángulo polar 0..1023 (1024 = 360°) */
    int32_t  r16;            /* Radio polar fixed-point (r << 8) */
    int32_t  vr16;           /* Velocidad radial hacia el centro */
    int16_t  omega;          /* Velocidad angular base */
    int32_t  prev_x, prev_y; /* Estela previa */
    uint8_t  size;
    uint32_t color;
};

struct her_cosmic_star {
    int32_t  x, y;           /* Posición fija 16.8 en pantalla */
    int32_t  vx, vy;         /* Velocidad fija 16.8 */
    int32_t  prev_x, prev_y;
    uint32_t color;
};

static struct her_particle     her_torus[HER_TORUS_PART];
static struct her_cosmic_star   her_stars[HER_STARS_PART];
static int her_cosmic_ready = 0;
static int soma_vortex_boost = 0;

/* Tabla seno 8-bit */
static const int8_t HER_SIN_TAB[64] = {
      0,  12,  25,  37,  49,  60,  71,  81,
     90,  98, 105, 111, 116, 120, 123, 125,
    127, 125, 123, 120, 116, 111, 105,  98,
     90,  81,  71,  60,  49,  37,  25,  12,
      0, -12, -25, -37, -49, -60, -71, -81,
    -90, -98,-105,-111,-116,-120,-123,-125,
   -127,-125,-123,-120,-116,-111,-105, -98,
    -90, -81, -71, -60, -49, -37, -25, -12
};
static int __attribute__((unused)) her_isin(uint32_t a) { return (int)HER_SIN_TAB[(a >> 2) & 63]; }
static int __attribute__((unused)) her_icos(uint32_t a) { return (int)HER_SIN_TAB[((a + 64) >> 2) & 63]; }

static uint32_t __attribute__((unused)) her_rainbow(uint32_t t)
{
    static const uint32_t pal[8] = {
        0x00FF6B6B, 0x00FFA94D, 0x00FFE66D, 0x006BCB77,
        0x004D96FF, 0x009B5DE5, 0x00F15BB5, 0x00FF6B6B
    };
    return pal[(t >> 3) & 7];
}

static uint32_t her_rng = 0xA53C9E7Bu;
static uint32_t her_rand(void)
{
    her_rng = her_rng * 1664525u + 1013904223u;
    return her_rng;
}

/* ====================================================================
 * SOMA HER-AURA v4 (ESTADO, ARMONICOS Y TEXTO)
 * ==================================================================== */
static volatile int her_agent_busy;

enum { HER_ST_IDLE = 0, HER_ST_LISTEN, HER_ST_THINK, HER_ST_SPEAK };

#define HER_RING_N   48
#define HER_ANS_MAX  1536
#define HER_THK_MAX  640

static const int16_t HER_S[256] = {
    0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369,
    392, 415, 438, 460, 483, 505, 526, 548, 569, 590, 610, 630, 650, 669, 688, 706,
    724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926, 936,
    946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25,
    0, -25, -50, -75, -100, -125, -150, -175, -200, -224, -249, -273, -297, -321, -345, -369,
    -392, -415, -438, -460, -483, -505, -526, -548, -569, -590, -610, -630, -650, -669, -688, -706,
    -724, -742, -759, -775, -792, -807, -822, -837, -851, -865, -878, -891, -903, -915, -926, -936,
    -946, -955, -964, -972, -980, -987, -993, -999, -1004, -1009, -1013, -1016, -1019, -1021, -1023, -1024,
    -1024, -1024, -1023, -1021, -1019, -1016, -1013, -1009, -1004, -999, -993, -987, -980, -972, -964, -955,
    -946, -936, -926, -915, -903, -891, -878, -865, -851, -837, -822, -807, -792, -775, -759, -742,
    -724, -706, -688, -669, -650, -630, -610, -590, -569, -548, -526, -505, -483, -460, -438, -415,
    -392, -369, -345, -321, -297, -273, -249, -224, -200, -175, -150, -125, -100, -75, -50, -25,
};

static int her_sn(uint32_t a)
{
    uint32_t i = (a >> 2) & 255u, f = a & 3u;
    int s0 = HER_S[i], s1 = HER_S[(i + 1u) & 255u];
    return s0 + ((s1 - s0) * (int)f) / 4;
}
static int her_cs(uint32_t a) { return her_sn(a + 256u); }

static uint32_t her_mix256(uint32_t c1, uint32_t c2, uint32_t k)
{
    uint32_t r = ((((c1 >> 16) & 255u) * (256u - k)) + (((c2 >> 16) & 255u) * k)) >> 8;
    uint32_t g = ((((c1 >> 8) & 255u) * (256u - k)) + (((c2 >> 8) & 255u) * k)) >> 8;
    uint32_t b = (((c1 & 255u) * (256u - k)) + ((c2 & 255u) * k)) >> 8;
    return (r << 16) | (g << 8) | b;
}
static uint32_t her_mix(uint32_t c1, uint32_t c2, uint32_t k16) { return her_mix256(c1, c2, k16 * 16u); }
static uint32_t her_scale(uint32_t c, uint32_t k)
{
    if (k > 256u) k = 256u;
    return ((((c >> 16) & 255u) * k >> 8) << 16) |
           ((((c >> 8) & 255u) * k >> 8) << 8) |
           (((c & 255u) * k) >> 8);
}
/* SOMA BLACK HOLE REALISM PATCH v1
 * Paleta acromatica fria: acero azulado -> blanco frio, sin arcoiris.
 * La variacion de luminancia es suave y usa la tabla seno ya existente.
 */
static uint32_t her_grad(uint32_t a)
{
    int wave = her_sn(a & 1023u);
    uint32_t k = 112u + (uint32_t)(wave + 1024) * 64u / 2048u;
    return her_mix256(0x00B9CBE0u, 0x00F7FAFFu, k);
}
static int her_ease(int cur, int tgt, uint32_t k)
{
    int d = tgt - cur;
    int s = (d * (int)k) / 256;
    if (s == 0 && d != 0) s = (d > 0) ? 1 : -1;
    return cur + s;
}

static uint16_t *her_ans = 0;
static uint32_t her_ans_len = 0, her_ans_reveal = 0;
static uint16_t *her_thk = 0;
static uint32_t her_thk_len = 0, her_thk_reveal = 0;
static char     her_last_q[SOMA_AGENT_CMD_MAX];
static uint32_t her_energy = 0, her_pulse = 0, her_prev_len = 0;
static uint32_t her_col_cur = 0x007DD3FCu;
static int      her_cur_r16 = 56 * 16, her_breath = 0;
static int      her_cur_amp[4] = { 32, 19, 0, 0 };
static int      her_cur_speed = 14, her_cur_spec = 70, her_cur_glow = 70;
static uint32_t her_phacc[4] = { 0, 0, 0, 0 };
static uint32_t her_grad_acc = 0, her_last_draw = 0;

static uint16_t her_ans_ls[40], her_ans_ll[40];
static uint16_t her_thk_ls[16], her_thk_ll[16];
static int her_bufs_ready = 0;

static void her_bufs_ensure(void)
{
    if (her_bufs_ready) return;
    her_ans = (uint16_t *)kmalloc(HER_ANS_MAX * sizeof(uint16_t));
    her_thk = (uint16_t *)kmalloc(HER_THK_MAX * sizeof(uint16_t));
    if (!her_ans || !her_thk) { her_ans_len = 0; her_thk_len = 0; return; }
    for (uint32_t i = 0; i < HER_ANS_MAX; ++i) her_ans[i] = 0;
    for (uint32_t i = 0; i < HER_THK_MAX; ++i) her_thk[i] = 0;
    her_bufs_ready = 1;
}

static int her_state(void)
{
    if (her_ans_reveal < her_ans_len || her_energy > 24u) return HER_ST_SPEAK;
    if (her_agent_busy) return HER_ST_THINK;
    if (agent_global_len > 0 && agent_global_focus) return HER_ST_LISTEN;
    return HER_ST_IDLE;
}

struct her_u8 { uint32_t cp; int need; };

static uint16_t her_cp_cell(uint32_t cp)
{
    if (cp == '\n') return (uint16_t)'\n';
    if (cp == '\t') return (uint16_t)' ';
    if (cp < 32u || cp == 127u) return 0;
    if (cp < 127u) return (uint16_t)cp;
    switch (cp) {
    case 0xE1: return (uint16_t)('a' | (1u << 8));
    case 0xE9: return (uint16_t)('e' | (1u << 8));
    case 0xED: return (uint16_t)('i' | (1u << 8));
    case 0xF3: return (uint16_t)('o' | (1u << 8));
    case 0xFA: return (uint16_t)('u' | (1u << 8));
    case 0xC1: return (uint16_t)('A' | (1u << 8));
    case 0xC9: return (uint16_t)('E' | (1u << 8));
    case 0xCD: return (uint16_t)('I' | (1u << 8));
    case 0xD3: return (uint16_t)('O' | (1u << 8));
    case 0xDA: return (uint16_t)('U' | (1u << 8));
    case 0xF1: return (uint16_t)('n' | (2u << 8));
    case 0xD1: return (uint16_t)('N' | (2u << 8));
    case 0xFC: return (uint16_t)('u' | (3u << 8));
    case 0xDC: return (uint16_t)('U' | (3u << 8));
    case 0xBF: return (uint16_t)('?' | (4u << 8));
    case 0xA1: return (uint16_t)('!' | (4u << 8));
    default:   return (uint16_t)'?';
    }
}

static int her_u8_feed(struct her_u8 *d, uint8_t b, uint16_t *out)
{
    uint16_t cell;
    if (b < 0x80u) {
        d->need = 0; cell = her_cp_cell(b);
    } else if (b >= 0xF0u) { d->need = 3; d->cp = b & 7u;  return 0; }
    else if (b >= 0xE0u)   { d->need = 2; d->cp = b & 15u; return 0; }
    else if (b >= 0xC0u)   { d->need = 1; d->cp = b & 31u; return 0; }
    else {
        if (d->need <= 0) return 0;
        d->cp = (d->cp << 6) | (b & 63u);
        if (--d->need > 0) return 0;
        cell = her_cp_cell(d->cp);
    }
    if (!cell) return 0;
    *out = cell;
    return 1;
}

static uint32_t her_text_cells_n(const char *s, uint32_t nb, uint16_t *dst, uint32_t max)
{
    struct her_u8 d = { 0, 0 };
    uint32_t n = 0;
    for (uint32_t i = 0; i < nb && s[i] && n < max; ++i) {
        uint16_t cell;
        if (her_u8_feed(&d, (uint8_t)s[i], &cell)) dst[n++] = cell;
    }
    return n;
}

static uint32_t her_text_cells(const char *s, uint16_t *dst, uint32_t max)
{
    uint32_t nb = 0; while (s[nb]) nb++;
    return her_text_cells_n(s, nb, dst, max);
}

static const char *her_find(const char *hay, const char *needle)
{
    for (; *hay; ++hay) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}

static void her_draw_cell(int32_t x, int32_t y, uint16_t cell, uint32_t col, int sc)
{
    uint8_t base = (uint8_t)(cell & 0xFFu);
    uint32_t mk = (uint32_t)(cell >> 8);
    /* SOMA_VECTOR_CELL_PATCH: texto del escritorio en vectorial */
    if (font_draw_cell_vector(x, y, base, mk, col, sc)) return;
    const uint8_t *g = font_get_glyph((char)base);
    uint8_t tmp[8];
    for (int r = 0; r < 8; ++r) tmp[r] = (mk == 4u) ? g[7 - r] : g[r];
    if (mk && mk != 4u && mk != 7u && base == 'i') tmp[0] = 0;
    fb_draw_glyph8_nodmg(x, y, tmp, col, sc);
    if (!mk || mk == 4u) return;

    int32_t s = sc;
    int32_t oy = (base >= 'A' && base <= 'Z') ? y - 2 * s : y;
#define HP(px, py) fb_fill_rect((uint32_t)(x + (px) * s), (uint32_t)(py), (uint32_t)s, (uint32_t)s, col)
    switch (mk) {
    case 1: HP(4, oy); HP(3, oy + s); break;
    case 2: HP(2, oy + s); HP(3, oy); HP(4, oy); HP(5, oy + s); break;
    case 3: HP(2, oy + s); HP(5, oy + s); break;
    default: break;
    }
#undef HP
}

static void her_draw_cells(int32_t x, int32_t y, const uint16_t *c, uint32_t len,
                           uint32_t g0, uint32_t reveal,
                           uint32_t base, uint32_t tip, int sc)
{
    int32_t adv = 9 * sc;
    for (uint32_t k = 0; k < len; ++k) {
        uint32_t gi = g0 + k;
        if (gi >= reveal) break;
        uint16_t cell = c[k];
        if ((cell & 0xFFu) == ' ') continue;
        uint32_t d = reveal - gi;
        uint32_t col = base;
        if (d < 14u) col = her_mix256(tip, base, d * 256u / 14u);
        her_draw_cell(x + (int32_t)k * adv, y, cell, col, sc);
    }
}

static void her_draw_label(uint32_t cur_w, uint32_t y, const char *s, uint32_t col)
{
    uint32_t n = 0; while (s[n]) n++;
    uint32_t w = n * 9u;
    uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
    for (uint32_t i = 0; i < n; ++i)
        her_draw_cell((int32_t)(x + i * 9u), (int32_t)y, (uint16_t)(uint8_t)s[i], col, 1);
}

static int her_wrap(const uint16_t *c, uint32_t n, uint32_t cols,
                    uint16_t *ls, uint16_t *ll, int maxl)
{
    int cnt = 0; uint32_t i = 0;
    if (cols < 1u) cols = 1u;
    while (i < n && cnt < maxl) {
        uint32_t start = i, j = i, end, next;
        int32_t last_sp = -1;
        while (j < n && (j - start) < cols) {
            uint8_t b = (uint8_t)(c[j] & 0xFFu);
            if (b == '\n') break;
            if (b == ' ') last_sp = (int32_t)j;
            j++;
        }
        if (j >= n)                         { end = n; next = n; }
        else if ((c[j] & 0xFFu) == '\n')    { end = j; next = j + 1u; }
        else if ((c[j] & 0xFFu) == ' ')     { end = j; next = j + 1u; }
        else if (last_sp > (int32_t)start)  { end = (uint32_t)last_sp; next = end + 1u; }
        else                                { end = j; next = j; }
        ls[cnt] = (uint16_t)start;
        ll[cnt] = (uint16_t)(end - start);
        cnt++; i = next;
    }
    return cnt;
}

static int her_ring_r16(uint32_t a)
{
    int r = her_cur_r16 + her_breath;
    r += her_sn(2u * a + (her_phacc[0] >> 10)) * her_cur_amp[0] / 1024;
    r += her_sn(3u * a + (her_phacc[1] >> 10)) * her_cur_amp[1] / 1024;
    r += her_sn(5u * a + (her_phacc[2] >> 10)) * her_cur_amp[2] / 1024;
    r += her_sn(7u * a + (her_phacc[3] >> 10)) * her_cur_amp[3] / 1024;
    return r;
}

static void her_reveal_step(void)
{
    her_bufs_ensure();
    if (!her_ans || !her_thk) return;
    if (her_ans_reveal < her_ans_len) {
        uint32_t step = 1u + her_ans_len / 160u;
        if (step > 6u) step = 6u;
        while (step-- && her_ans_reveal < her_ans_len) {
            uint8_t b = (uint8_t)(her_ans[her_ans_reveal] & 0xFFu);
            if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9'))
                her_energy += 40u;
            her_ans_reveal++;
        }
        if (her_energy > 255u) her_energy = 255u;
    }
    her_energy = (her_energy * 192u) >> 8;
    if (her_thk_reveal < her_thk_len) {
        her_thk_reveal += 3u + her_thk_len / 120u;
        if (her_thk_reveal > her_thk_len) her_thk_reveal = her_thk_len;
    }
}

static void her_begin_turn(const char *q)
{
    her_bufs_ensure();
    uint32_t i = 0; while (q && q[i] && i < sizeof(her_last_q) - 1u) { her_last_q[i] = q[i]; i++; }
    her_last_q[i] = '\0';
    her_ans_len = 0; her_ans_reveal = 0;
    her_thk_len = 0; her_thk_reveal = 0;
    her_energy = 0;
    her_text_suppressed_by_dock = 0;
}

static void her_finish_turn(const char *resp)
{
    her_bufs_ensure();
    static const char tag_r[] = "[Razonamiento]:\n";
    static const char tag_d[] = "\n\n[Dictamen]:\n";
    if (!resp) resp = "";
    const char *ans = resp;
    const char *d = her_find(resp, tag_d);
    uint32_t i = 0; while (tag_r[i] && resp[i] == tag_r[i]) i++;
    if (tag_r[i] == '\0' && d) {
        const char *ts = resp + (sizeof(tag_r) - 1u);
        if (her_thk_len == 0 && d > ts) {
            her_thk_reveal = 0;
            her_thk_len = her_text_cells_n(ts, (uint32_t)(d - ts), her_thk, HER_THK_MAX);
        }
        ans = d + (sizeof(tag_d) - 1u);
    }
    if (her_ans_len == 0) {
        her_ans_reveal = 0;
        her_ans_len = her_text_cells(*ans ? ans : "Mision ejecutada en hardware.",
                                     her_ans, HER_ANS_MAX);
    }
}

void gui_thought_update(const char *txt)
{
    her_bufs_ensure();
    if (!txt || !her_agent_busy) return;
    her_thk_reveal = 0; her_thk_len = 0;
    her_thk_len = her_text_cells(txt, her_thk, HER_THK_MAX);
}

static struct her_u8 her_stream_dec = { 0, 0 };

void gui_stream_begin(void)
{
    her_bufs_ensure();
    her_ans_len = 0; her_ans_reveal = 0;
    her_stream_dec.cp = 0; her_stream_dec.need = 0;
}

void gui_stream_push(const char *chunk)
{
    if (!chunk) return;
    for (uint32_t i = 0; chunk[i] && her_ans_len < HER_ANS_MAX; ++i) {
        uint16_t cell;
        if (her_u8_feed(&her_stream_dec, (uint8_t)chunk[i], &cell))
            her_ans[her_ans_len++] = cell;
    }
}

/* ====================================================================
 * ROSQUILLA DE ACRECIÓN Y ESTRELLAS CÓSMICAS (HER-COSMIC TORUS)
 * ==================================================================== */
static void her_cosmic_init_all(uint32_t cx, uint32_t cy)
{
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    uint32_t max_reach = (cur_w > cur_h ? cur_w : cur_h);

    for (int i = 0; i < HER_TORUS_PART; ++i) {
        her_torus[i].angle = (uint16_t)(((uint32_t)i * 1024u) / HER_TORUS_PART);
        uint32_t r_base = 36u + ((uint32_t)i * 44u) / HER_TORUS_PART + (her_rand() & 15u);
        her_torus[i].r16 = (int32_t)r_base << 8;
        her_torus[i].vr16 = 20 + (int32_t)(her_rand() & 23u);
        her_torus[i].omega = 10 + (int32_t)(her_rand() & 7u);
        her_torus[i].color = her_grad(her_torus[i].angle);
        her_torus[i].size = ((i & 5) == 0) ? 2 : 1;
        her_torus[i].prev_x = -1;
        her_torus[i].prev_y = -1;
    }

    for (int s = 0; s < HER_STARS_PART; ++s) {
        uint32_t ang = (her_rand() >> 8) & 1023u;
        uint32_t dist = 110u + ((uint32_t)s * (max_reach / 2u)) / HER_STARS_PART + (her_rand() & 63u);
        int32_t sx = (int32_t)cx + ((dist * (uint32_t)her_cs(ang)) >> 10);
        int32_t sy = (int32_t)cy + ((dist * (uint32_t)her_sn(ang)) >> 10);
        her_stars[s].x = sx << 8;
        her_stars[s].y = sy << 8;
        her_stars[s].vx = 0;
        her_stars[s].vy = 0;
        her_stars[s].prev_x = sx;
        her_stars[s].prev_y = sy;
        her_stars[s].color = ((s & 3) == 0) ? 0x00FFFFFF : 0x00DCE8F5;
    }

    her_cosmic_ready = 1;
}

static void her_parts_spawn(uint32_t orb_cx, uint32_t orb_cy, uint32_t angle)
{
    (void)angle;
    if (!her_cosmic_ready) her_cosmic_init_all(orb_cx, orb_cy);
}

static void her_parts_update_draw(void)
{
    uint32_t cx = fb_get_width() / 2;
    uint32_t cy = fb_get_height() / 2;
    if (!her_cosmic_ready) her_cosmic_init_all(cx, cy);

    int spd = her_cur_speed + soma_vortex_boost;
    if (spd < 10) spd = 10;
    int step_mul = spd / 12;
    if (step_mul < 1) step_mul = 1;

    /* A. ESTRELLAS CÓSMICAS EN TODA LA PANTALLA */
    uint32_t max_d = fb_get_width() > fb_get_height() ? fb_get_width() : fb_get_height();
    for (int s = 0; s < HER_STARS_PART; ++s) {
        struct her_cosmic_star *st = &her_stars[s];
        int32_t cur_x = st->x >> 8;
        int32_t cur_y = st->y >> 8;
        int32_t dx = (int32_t)cx - cur_x;
        int32_t dy = (int32_t)cy - cur_y;
        uint32_t dist = (uint32_t)her_isqrt((uint32_t)(dx * dx + dy * dy));

        if (dist <= 30u) {
            uint32_t r_ang = (her_rand() >> 8) & 1023u;
            uint32_t r_dist = max_d / 2u + 20u + (her_rand() & 127u);
            int32_t nx = (int32_t)cx + ((r_dist * (uint32_t)her_cs(r_ang)) >> 10);
            int32_t ny = (int32_t)cy + ((r_dist * (uint32_t)her_sn(r_ang)) >> 10);
            st->x = nx << 8;
            st->y = ny << 8;
            st->vx = 0; st->vy = 0;
            st->prev_x = nx; st->prev_y = ny;
            continue;
        }

        int32_t pull = 6 + (int32_t)(1200u / (dist + 30u));
        int32_t swirl = 5 + (int32_t)(1500u / (dist + 20u));
        int32_t ax = (dx * pull + dy * swirl) / (int32_t)dist;
        int32_t ay = (dy * pull - dx * swirl) / (int32_t)dist;

        st->vx = ((st->vx * 248) >> 8) + ax;
        st->vy = ((st->vy * 248) >> 8) + ay;
        st->x += st->vx;
        st->y += st->vy;

        int32_t px = st->x >> 8;
        int32_t py = st->y >> 8;

        if (dist < 90u && (st->prev_x != px || st->prev_y != py)) {
            fb_draw_line(st->prev_x, st->prev_y, px, py, 0x008CA8C7);
        }
        st->prev_x = px;
        st->prev_y = py;

        if (dist < 80u) {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, st->color);
        }
    }

    /* B. ROSQUILLA DE ACRECIÓN (Toro de partículas denso) */
    for (int i = 0; i < HER_TORUS_PART; ++i) {
        struct her_particle *p = &her_torus[i];
        int32_t curr_r = p->r16 >> 8;

        int wave_offset = (her_ring_r16(p->angle) >> 4) - 56;
        int32_t effective_r = curr_r + wave_offset;

        int32_t ang_v = (int32_t)p->omega + (3600 / (curr_r > 5 ? curr_r : 5));
        p->angle = (uint16_t)((p->angle + (ang_v * step_mul) / 4) & 1023u);

        int32_t rad_v = p->vr16 + (2400 / (curr_r > 6 ? curr_r : 6));
        p->r16 -= (rad_v * step_mul) / 4;
        curr_r = p->r16 >> 8;

        if (curr_r <= 28) {
            p->r16 = (int32_t)(76u + (her_rand() & 23u)) << 8;
            p->angle = (uint16_t)((p->angle + 512u + (her_rand() & 255u)) & 1023u);
            p->color = her_grad((uint32_t)p->angle + (her_grad_acc >> 10));
            p->prev_x = -1;
            p->prev_y = -1;
            continue;
        }

        uint32_t axis = (uint32_t)her_axis_phase & 1023u;
        int32_t local_x = (effective_r * her_cs(p->angle)) >> 10;
        int32_t local_y = (effective_r * her_sn(p->angle)) >> 10;
        int32_t tilted_x = (local_x * her_cs(axis) - local_y * her_sn(axis)) >> 10;
        int32_t tilted_y = (local_x * her_sn(axis) + local_y * her_cs(axis)) >> 10;
        int32_t px = (int32_t)cx + tilted_x;
        int32_t py = (int32_t)cy + tilted_y;

        if (p->prev_x >= 0 && (p->prev_x != px || p->prev_y != py)) {
            /* Estelas blanco-frio; el nucleo sigue siendo blanco puro. */
            uint32_t trail_col = (curr_r < 42) ? 0x00F7FBFF : 0x00D5E2F0;
            uint32_t trail_light = (curr_r < 42) ? 184u : 120u;
            fb_draw_line(p->prev_x, p->prev_y, px, py,
                         her_scale(trail_col, trail_light));
        }
        p->prev_x = px;
        p->prev_y = py;

        if (curr_r <= 38) {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else if (p->size > 1) {
            fb_fill_circle(px, py, (int32_t)p->size, p->color);
            fb_draw_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
        } else {
            fb_draw_pixel((uint32_t)px, (uint32_t)py, p->color);
        }
    }
}

/* El eje cambia de objetivo cada pocos segundos y se acerca suavemente.
 * Angulos en unidades de la tabla seno (1024 = vuelta completa). */
static void her_update_axis_wobble(uint64_t now_ms)
{
    if (her_axis_next_change == 0 || now_ms >= her_axis_next_change) {
        her_axis_target = (int32_t)(her_rand() % 97u) - 48;
        her_axis_next_change = now_ms + 2300u + (her_rand() % 2600u);
    }
    int32_t delta = her_axis_target - her_axis_phase;
    if (delta > 2) her_axis_phase += 2;
    else if (delta < -2) her_axis_phase -= 2;
    else her_axis_phase = her_axis_target;
}

/* ---------- orbe central: horizonte de sucesos ampliado y onda fluida ---------- */
static void her_draw_orb(uint32_t cx, uint32_t cy, uint32_t angle)
{
    static const int32_t W[4] = { 90, -140, 230, -330 };
    (void)angle;

    uint32_t t = (uint32_t)timer_get_uptime_ms();
    uint32_t dt = her_last_draw ? (t - her_last_draw) : 33u;
    her_last_draw = t;
    if (dt < 1u) dt = 1u;
    if (dt > 100u) dt = 100u;

    int st = her_state();

    if (agent_global_len != her_prev_len) {
        her_pulse = 190u;
        her_prev_len = agent_global_len;
        soma_vortex_boost += 5;
        if (soma_vortex_boost > 14) soma_vortex_boost = 14;
    } else {
        her_pulse = (her_pulse > dt) ? her_pulse - dt : 0u;
    }

    int fluid_wave = her_sn((t % 2800u) * 1024u / 2800u) * 20 / 1024;

    int tr = 58 * 16, tsp = 15, tspec = 70, tglow = 70, bamp = 40;
    int ta[4] = { 42 + fluid_wave, 26 - fluid_wave / 2, 16 + fluid_wave / 3, 10 };
    uint32_t ttint = 0x007DD3FCu, bp = 4200u;

    int non_uniform_glide = (her_sn((t % 3600u) * 1024u / 3600u) * 8) / 1024;
    tsp += non_uniform_glide;

    switch (st) {
    case HER_ST_LISTEN:
        tr = 60 * 16;
        ta[0] = 43 + (int)(her_pulse * 4u / 25u);
        ta[1] = 30 + (int)(her_pulse / 18u);
        ta[2] = 16;
        tsp = 18 + non_uniform_glide; tspec = 52; tglow = 86; bamp = 42;
        ttint = 0x00BAE6FDu; bp = 2600u;
        break;
    case HER_ST_THINK:
        tr = 56 * 16;
        ta[0] = 62; ta[1] = 36; ta[2] = 20; ta[3] = 8;
        tsp = 20 + non_uniform_glide; tspec = 100; tglow = 88; bamp = 42;
        ttint = 0x00C4B5FDu; bp = 1700u;
        break;
    case HER_ST_SPEAK: {
        int e = (int)her_energy;
        tr = 58 * 16 + e * 28 / 255;
        ta[0] = 45 + e * 15 / 255;
        ta[1] = 30 + e * 18 / 255;
        ta[2] = 17 + e * 20 / 255;
        ta[3] = e * 10 / 255;
        tsp = 19 + e / 32; tspec = 100; tglow = 86 + e / 10; bamp = 42;
        ttint = 0x00FDA4AFu; bp = 1400u;
        break; }
    default:
        break;
    }

    /* Las acciones aportan un impulso pequeno y amortiguado, no un latigazo. */
    if (soma_vortex_boost > 0) {
        int boost_amp = (soma_vortex_boost * 8) / 100;
        ta[0] += boost_amp;
        ta[1] += boost_amp / 2;
        ta[2] += boost_amp / 3;
        tsp += soma_vortex_boost / 8;
        tglow += soma_vortex_boost / 12;
        soma_vortex_boost = (soma_vortex_boost * 210) >> 8;
    }

    uint32_t kk = (st == HER_ST_SPEAK) ? dt * 256u / 140u : dt * 256u / 360u;
    if (kk > 230u) kk = 230u;
    if (kk < 8u) kk = 8u;
    her_cur_r16 = her_ease(her_cur_r16, tr, kk);
    for (int k = 0; k < 4; ++k) her_cur_amp[k] = her_ease(her_cur_amp[k], ta[k], kk);
    her_cur_speed = her_ease(her_cur_speed, tsp, kk);
    her_cur_spec  = her_ease(her_cur_spec, tspec, kk);
    her_cur_glow  = her_ease(her_cur_glow, tglow, kk);
    her_col_cur = her_mix256(her_col_cur, ttint, kk);

    her_breath = her_sn((t % bp) * 1024u / bp) * bamp / 1024;
    for (int k = 0; k < 4; ++k)
        her_phacc[k] += (uint32_t)((int32_t)dt * W[k] * her_cur_speed / 16);
    her_grad_acc += dt * 60u * (uint32_t)her_cur_speed / 16u;

    /* ---- HORIZONTE DE SUCESOS ----
     * Sin circunferencia ni anillo fotonico dibujado: solo el nucleo negro.
     * La estructura se percibe a traves del flujo de particulas y sus estelas.
     * El eje del flujo cambia de inclinacion de forma lenta e irregular.
     */
    int32_t icx = (int32_t)cx, icy = (int32_t)cy;
    her_update_axis_wobble(t);
    fb_fill_circle(icx, icy, 27, 0x00000000);

}

static void agent_global_insert_str(const char *s)
{
    uint32_t slen = 0; while (s[slen]) slen++;
    if (slen == 0 || agent_global_len + slen >= sizeof(agent_global_cmd) - 2) return;
    for (uint32_t i = agent_global_len + slen; i >= agent_global_cursor + slen; --i)
        agent_global_cmd[i] = agent_global_cmd[i - slen];
    for (uint32_t i = 0; i < slen; ++i)
        agent_global_cmd[agent_global_cursor + i] = s[i];
    agent_global_cursor += slen;
    agent_global_len += slen;
    agent_global_cmd[agent_global_len] = '\0';
    agent_global_scroll = 0;
}

static void her_draw_user_zone(uint32_t cur_w, uint32_t cur_h, uint32_t orb_cy)
{
    (void)cur_h;
    if (her_icons_visible) return;
    if (cur_w < 160u) return;
    uint32_t t = (uint32_t)timer_get_uptime_ms();

    if (agent_global_len > 0) {
        static uint16_t ucells[SOMA_AGENT_CMD_MAX];
        uint32_t n = her_text_cells_n(agent_global_cmd, agent_global_len, ucells, SOMA_AGENT_CMD_MAX);
        int sc = (n * 18u <= cur_w - 120u) ? 2 : 1;
        uint32_t adv = 9u * (uint32_t)sc;
        uint32_t maxc = (cur_w - 80u) / adv;
        static uint16_t cur_dummy[SOMA_AGENT_CMD_MAX];
        uint32_t cur = her_text_cells_n(agent_global_cmd, agent_global_cursor, cur_dummy, SOMA_AGENT_CMD_MAX);
        if (cur > n) cur = n;
        uint32_t start = 0;
        if (n > maxc) {
            start = (cur > maxc) ? cur - maxc : 0u;
            if (start + maxc > n) start = n - maxc;
        }
        uint32_t shown = n - start;
        if (shown > maxc) shown = maxc;
        uint32_t w = shown * adv;
        uint32_t tx = (cur_w > w) ? (cur_w - w) / 2u : 0u;
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 150u : 20u;
        for (uint32_t k = 0; k < shown; ++k) {
            uint16_t cell = ucells[start + k];
            if ((cell & 0xFFu) == ' ') continue;
            her_draw_cell((int32_t)(tx + k * adv), (int32_t)ty,
                          cell, 0x00F1F5F9u, sc);
        }
        if (agent_global_focus) {
            uint32_t lvl = (uint32_t)(her_sn((t % 1400u) * 1024u / 1400u) + 1024) * 256u / 2048u;
            uint32_t col = her_mix256(0x00334155u, her_col_cur, lvl);
            uint32_t cx = tx + (cur - start) * adv;
            fb_fill_rect((cx > 1u) ? cx - 1u : 0u, ty - 2u, (uint32_t)sc, 8u * (uint32_t)sc + 4u, col);
        }
    } else if (her_last_q[0] && (her_ans_len > 0 || her_agent_busy)) {
        static uint16_t qcells[SOMA_AGENT_CMD_MAX];
        uint32_t n = her_text_cells(her_last_q, qcells, SOMA_AGENT_CMD_MAX);
        uint32_t maxc = (cur_w - 80u) / 9u;
        if (n > maxc) n = maxc;
        uint32_t tx = (cur_w > n * 9u) ? (cur_w - n * 9u) / 2u : 0u;
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 146u : 20u;
        for (uint32_t k = 0; k < n; ++k) {
            uint16_t cell = qcells[k];
            if ((cell & 0xFFu) == ' ') continue;
            her_draw_cell((int32_t)(tx + k * 9u), (int32_t)ty,
                          cell, 0x00475569u, 1);
        }
    } else if (current_app == APP_DESKTOP && !her_vortex_closing) {
        /* Prompt interactivo listo para escribir al agente */
        uint32_t ty = (orb_cy > 170u) ? orb_cy - 146u : 20u;
        const char *hint = "soma> escribe una mision...";
        uint32_t n = 0; while (hint[n]) n++;
        uint32_t w = n * 9u;
        uint32_t tx = (cur_w > w) ? (cur_w - w) / 2u : 0u;
        for (uint32_t k = 0; k < n; ++k) {
            her_draw_cell((int32_t)(tx + k * 9u), (int32_t)ty,
                          (uint16_t)(uint8_t)hint[k], 0x00334155u, 1);
        }
        uint32_t lvl = (uint32_t)(her_sn((t % 1400u) * 1024u / 1400u) + 1024) * 256u / 2048u;
        uint32_t col = her_mix256(0x001E293Bu, her_col_cur, lvl);
        fb_fill_rect(tx + w + 3u, ty - 1u, 2u, 10u, col);
    }
}

static void her_draw_agent_zone(uint32_t cur_w, uint32_t cur_h, uint32_t orb_cy)
{
    if (her_icons_visible || her_text_suppressed_by_dock) return;
    her_bufs_ensure();
    if (her_ans_len == 0 && her_thk_len == 0) return;
    if (cur_w < 200u) return;

    uint32_t y = orb_cy + 112u;
    uint32_t bottom = (cur_h > 40u) ? cur_h - 40u : cur_h;
    if (her_icons_visible && cur_h > 130u) bottom = cur_h - 130u;
    if (bottom <= y + 32u) return;
    uint32_t avail = bottom - y;
    uint32_t width_cap = cur_w - 120u;
    uint32_t block_end = y;

    if (her_ans_len > 0) {
        int sc = (her_ans_len <= 150u) ? 2 : 1;
        uint32_t adv = 9u * (uint32_t)sc;
        uint32_t lh = (sc == 2) ? 28u : 16u;
        uint32_t wmax = width_cap;
        uint32_t cap = (sc == 2) ? 900u : 820u;
        if (wmax > cap) wmax = cap;
        uint32_t cols = wmax / adv;
        if (cols < 8u) cols = 8u;
        int n = her_wrap(her_ans, her_ans_len, cols, her_ans_ls, her_ans_ll, 40);
        uint32_t reserve = (her_thk_len > 0) ? 96u : 0u;
        uint32_t maxl = (avail > reserve + lh) ? (avail - reserve) / lh : 1u;
        if (maxl < 2u) maxl = 2u;
        uint32_t total = ((uint32_t)n < maxl) ? (uint32_t)n : maxl;
        uint32_t started = 0;
        while ((int)started < n && her_ans_ls[started] < her_ans_reveal) started++;
        uint32_t first = (started > maxl) ? started - maxl : 0u;
        for (uint32_t l = first; l < started; ++l) {
            uint32_t w = (uint32_t)her_ans_ll[l] * adv;
            uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
            her_draw_cells((int32_t)x, (int32_t)(y + (l - first) * lh),
                           her_ans + her_ans_ls[l], her_ans_ll[l], her_ans_ls[l],
                           her_ans_reveal, 0x00F1F5F9u, her_col_cur, sc);
        }
        block_end = y + total * lh;
    }

    if (her_thk_len > 0) {
        int has_ans = (her_ans_len > 0);
        uint32_t ty = has_ans ? block_end + 22u : y;
        uint32_t wmax = (width_cap > 760u) ? 760u : width_cap;
        uint32_t cols = wmax / 9u;
        if (cols < 8u) cols = 8u;
        int n = her_wrap(her_thk, her_thk_len, cols, her_thk_ls, her_thk_ll, 16);
        uint32_t started = 0;
        while ((int)started < n && her_thk_ls[started] < her_thk_reveal) started++;
        uint32_t first = (started > 3u) ? started - 3u : 0u;
        if (has_ans) {
            her_draw_label(cur_w, ty, "pensamiento", 0x00334155u);
            ty += 20u;
        }
        uint32_t base = has_ans ? 0x00475569u : 0x0064748Bu;
        uint32_t tip = her_mix(her_col_cur, base, 6u);
        if (ty + 48u < cur_h) {
            for (uint32_t l = first; l < started; ++l) {
                uint32_t w = (uint32_t)her_thk_ll[l] * 9u;
                uint32_t x = (cur_w > w) ? (cur_w - w) / 2u : 0u;
                her_draw_cells((int32_t)x, (int32_t)(ty + (l - first) * 16u),
                               her_thk + her_thk_ls[l], her_thk_ll[l], her_thk_ls[l],
                               her_thk_reveal, base, tip, 1);
            }
        }
    }
}

/* SOMA TEXT ABSORPTION PATCH v5 */
/* SOMA ASYNC MATERIALIZE PATCH v4 */
/* SOMA ORBITAL CALM PATCH v3 */
/* SOMA ORBITAL ICONS PATCH v2: iconos como cuerpos en orbita estable. */
#define HER_DOCK_ITEM_COUNT 10
#define HER_ICON_ANIM_MAX   256u
#define HER_ICON_BOX_SIZE   44

struct her_dock_item {
    int icon;
    uint32_t color;
    const char *label;
    enum app_mode app;
};

static const struct her_dock_item her_dock_items[HER_DOCK_ITEM_COUNT] = {
    { ICON_AGENT,    0x00E0F2FE,  "SOMA",    APP_AGENT_CHAT },      /* Blanco plasma estelar */
    { ICON_TERMINAL, 0x0038BDF8,  "TERM",    APP_TERMINAL_SHELL },  /* Cian radiación */
    { ICON_BROWSER,  0x0060A5FA,  "WEB",     APP_WEB_BROWSER },     /* Azul fotónico */
    { ICON_FILES,    0x0094A3B8,  "FILES",   APP_FILE_MANAGER },    /* Titanio frío */
    { ICON_IMAGE,    0x00A78BFA,  "IMG",     APP_IMAGE_VIEWER },    /* Nebulosa cuántica */
    { ICON_NOTES,    0x00CBD5E1,  "NOTES",   APP_NOTEPAD },         /* Acero claro / Cristal */
    { ICON_MEMORY,   0x00818CF8,  "MEM",     APP_AGENT_MEMORY },    /* Zafiro sináptico */
    { ICON_LAYOUT,   0x0038BDF8,  "LAYOUT",  APP_LAYOUT },          /* Cian polar */
    { ICON_PAINT,    0x00F59E0B,  "PAINT",   APP_PAINT },           /* Ámbar / Pintura */
    { ICON_AGENTS,   0x00BAE6FD,  "AGENTES", APP_AGENTS },          /* Éter orbital */
};

/* La aparicion se escalona ligeramente para que los iconos escapen uno tras
 * otro. Esta funcion es compartida por el render y la deteccion de clics.
 */
static uint32_t her_icon_item_progress(int i)
{
    if (i < 0 || i >= HER_DOCK_ITEM_COUNT) return 0;
    uint32_t delay = (uint32_t)i * 6u; /* Velocidad 2x */
    if (her_icons_anim_phase <= delay) return 0;
    return ((her_icons_anim_phase - delay) * HER_ICON_ANIM_MAX) /
           (HER_ICON_ANIM_MAX - delay);
}

/* Orbitas amplias, adaptadas a resoluciones pequeñas para no expulsar iconos
 * fuera de pantalla. El radio supera ampliamente el horizonte de sucesos. */
static int32_t her_orbit_radius_x(uint32_t cur_w)
{
    int32_t r = (int32_t)(cur_w * 23u / 100u);
    int32_t cap = (int32_t)(cur_w / 2u) - 36;
    if (cap < 40) cap = 40;
    if (r > 280) r = 280;
    if (r > cap) r = cap;
    if (r < 110 && cap >= 110) r = 110;
    if (r > cap) r = cap;
    return r;
}

static int32_t her_orbit_radius_y(uint32_t cur_h)
{
    uint32_t percent = (cur_h < 650u) ? 13u : 21u;
    int32_t r = (int32_t)(cur_h * percent / 100u);
    int32_t cap = (int32_t)(cur_h / 2u) - 42;
    if (cap < 35) cap = 35;
    if (r > 175) r = 175;
    if (r > cap) r = cap;
    if (r < 64 && cap >= 64) r = 64;
    if (r > cap) r = cap;
    return r;
}

/* La ventana se limita al cuadrante exterior de la orbita. El nucleo y la
 * mayor parte de la trayectoria orbital quedan visibles incluso en SVGA. */
static void her_fit_window_to_orbit(uint32_t cur_w, uint32_t cur_h)
{
    int32_t min_w = (cur_w < 900u) ? 180 : ((cur_w < 1200u) ? 220 : HER_MIN_W);
    int32_t max_w = (int32_t)(cur_w / 2u) - her_orbit_radius_x(cur_w) - 60;
    int32_t avail_w = (int32_t)cur_w - 32;
    int32_t max_h = (int32_t)cur_h - 48;
    if (max_w < min_w) max_w = min_w;
    if (max_w > avail_w) max_w = avail_w;
    if (max_h < HER_MIN_H) max_h = HER_MIN_H;
    if (her_win_w < (uint32_t)min_w || her_win_w > (uint32_t)max_w)
        her_win_w = (uint32_t)max_w;
    if (her_win_h < HER_MIN_H || her_win_h > (uint32_t)max_h)
        her_win_h = (uint32_t)max_h;
}

/* Las aplicaciones se distribuyen por las cuatro esquinas segun su tipo. */
static void her_place_window_corner(uint32_t cur_w, uint32_t cur_h, int corner)
{
    her_fit_window_to_orbit(cur_w, cur_h);
    her_win_x = ((corner & 1) == 0 || cur_w <= her_win_w + 32u)
              ? 16u : cur_w - her_win_w - 16u;
    her_win_y = ((corner & 2) == 0 || cur_h <= her_win_h + 48u)
              ? 32u : cur_h - her_win_h - 16u;
}

/* Orbita eliptica amplia sin linea guia: una revolucion cada 60 segundos. */
static void her_icon_orbit_target(int i, uint32_t cur_w, uint32_t cur_h,
                                  uint64_t now_ms, int32_t *out_x, int32_t *out_y)
{
    int32_t cx = (int32_t)(cur_w / 2u);
    int32_t cy = (int32_t)(cur_h / 2u);
    int32_t rx = her_orbit_radius_x(cur_w);
    int32_t ry = her_orbit_radius_y(cur_h);
    uint32_t orbital_phase = (uint32_t)((now_ms % 60000u) * 1024u / 60000u);
    uint32_t a = (orbital_phase + ((uint32_t)i * 1024u) / HER_DOCK_ITEM_COUNT) & 1023u;
    *out_x = cx + (rx * her_cs(a)) / 1024;
    *out_y = cy + (ry * her_sn(a)) / 1024;
}

/* Destello corto de materia expulsada/absorbida. No usa arrays ni memoria
 * adicional; se dibuja solo durante los primeros 280 ms de cada transicion.
 */
static void her_draw_icon_burst(uint32_t cur_w, uint32_t cur_h, uint64_t now_ms)
{
    if (her_icons_burst_at == 0) return;
    uint64_t elapsed64 = (now_ms >= her_icons_burst_at) ? now_ms - her_icons_burst_at : 0;
    if (elapsed64 >= 140u) {
        her_icons_burst_at = 0;
        return;
    }

    uint32_t elapsed = (uint32_t)elapsed64;
    uint32_t travel = elapsed * 48u / 140u;
    uint32_t fade = (140u - elapsed) * 256u / 140u;
    int32_t cx = (int32_t)(cur_w / 2u), cy = (int32_t)(cur_h / 2u);
    uint32_t tint = her_scale(0x00DCEBFFu, fade);

    for (int i = 0; i < 12; ++i) {
        uint32_t a = (((uint32_t)i * 1024u) / 12u + elapsed / 8u) & 1023u;
        int32_t r0, r1;
        if (her_icons_burst_opening) {
            r0 = 23 + (int32_t)travel;
            r1 = r0 + 8;
        } else {
            r1 = 23 + (int32_t)(48u - travel);
            r0 = r1 + 8;
        }
        int32_t x0 = cx + (r0 * her_cs(a)) / 1024;
        int32_t y0 = cy + (r0 * her_sn(a)) / 1024;
        int32_t x1 = cx + (r1 * her_cs(a)) / 1024;
        int32_t y1 = cy + (r1 * her_sn(a)) / 1024;
        fb_draw_line(x0, y0, x1, y1, tint);
    }
}

/* Particulas discretas que viajan desde el centro y se condensan en el icono. */
static void her_draw_icon_materialization(int i, int32_t px, int32_t py,
                                          uint32_t progress, uint64_t now_ms,
                                          uint32_t cur_w, uint32_t cur_h)
{
    uint32_t tick = (uint32_t)(now_ms / 24u);
    int32_t spread = 3 + (int32_t)((HER_ICON_ANIM_MAX - progress) * 25u /
                                   HER_ICON_ANIM_MAX);
    uint32_t brightness = 76u + (progress * 170u) / HER_ICON_ANIM_MAX;
    uint32_t color = her_scale(0x00E7F3FFu, brightness);

    for (int k = 0; k < 8; ++k) {
        uint32_t a = ((uint32_t)i * 113u + (uint32_t)k * 149u +
                      tick * (9u + (uint32_t)(k & 3))) & 1023u;
        int32_t ring = 2 + (int32_t)(((uint32_t)k * 7u + tick +
                                     (uint32_t)i * 3u) % (uint32_t)spread);
        int32_t sx = px + (ring * her_cs(a)) / 1024;
        int32_t sy = py + (ring * her_sn(a)) / 1024;
        int32_t tail = 2 + (k % 3);
        int32_t ex = sx - (tail * her_cs(a)) / 1024;
        int32_t ey = sy - (tail * her_sn(a)) / 1024;

        if (sx >= 0 && sy >= 0 && sx < (int32_t)cur_w &&
            sy < (int32_t)cur_h) {
            fb_draw_line(sx, sy, ex, ey, color);
            fb_draw_pixel((uint32_t)sx, (uint32_t)sy, 0x00FFFFFFu);
        }
    }
}

/* SOMA VECTOR ICONS PATCH v1: iconos vectoriales suavizados, sin recuadro. */
#define HER_ICON_MAX_S 96

static uint32_t *her_icon_sprites = 0;
static int       her_icon_sprite_s = 0;
static uint8_t   her_icon_ready[HER_DOCK_ITEM_COUNT];

static int her_icon_canvas(uint32_t cur_h)
{
    return (cur_h < 650u) ? 64 : 88;   /* lado del sprite en pixeles */
}

/* Geometria en una rejilla de diseno de 64x64 unidades.
 * X, Y llegan en 1/16 de unidad; los parametros de forma, en unidades. */
static int32_t ic_abs(int32_t v) { return v < 0 ? -v : v; }

static int32_t ic_circ(int32_t X, int32_t Y, int cx, int cy, int r)
{
    int32_t dx = X - cx * 16, dy = Y - cy * 16;
    return (int32_t)her_isqrt((uint32_t)(dx * dx + dy * dy)) - r * 16;
}

static int32_t ic_rbox(int32_t X, int32_t Y, int cx, int cy, int hw, int hh, int r)
{
    int32_t dx = ic_abs(X - cx * 16), dy = ic_abs(Y - cy * 16);
    int32_t qx = dx - (hw - r) * 16, qy = dy - (hh - r) * 16;
    int32_t ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    int32_t inner = qx > qy ? qx : qy;
    if (inner > 0) inner = 0;
    return (int32_t)her_isqrt((uint32_t)(ox * ox + oy * oy)) + inner - r * 16;
}

/* Capsula entre (ax,ay) y (bx,by); hw16 = medio grosor en 1/16 de unidad. */
static int32_t ic_seg(int32_t X, int32_t Y, int ax, int ay, int bx, int by, int hw16)
{
    int32_t px = X - ax * 16, py = Y - ay * 16;
    int32_t vx = (bx - ax) * 16, vy = (by - ay) * 16;
    int32_t l2 = vx * vx + vy * vy;
    int32_t t = l2 ? (px * vx + py * vy) * 256 / l2 : 0;
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    int32_t hx = px - vx * t / 256, hy = py - vy * t / 256;
    return (int32_t)her_isqrt((uint32_t)(hx * hx + hy * hy)) - hw16;
}

static int32_t ic_ell(int32_t X, int32_t Y, int cx, int cy, int rx, int ry)
{
    int32_t dx = (X - cx * 16) * ry / rx, dy = Y - cy * 16;
    int32_t d = (int32_t)her_isqrt((uint32_t)(dx * dx + dy * dy)) - ry * 16;
    return d * rx / ry;
}

#define IC_RING(d, h) (ic_abs(d) - (h))
#define IC_M(v) do { int32_t _v = (v); if (_v < m) m = _v; } while (0)
#define IC_A(v) do { int32_t _v = (v); if (_v < a) a = _v; } while (0)

/* Distancia con signo al trazo principal (dm) y al acento (da). */
static void her_icon_dist(int icon, int32_t X, int32_t Y, int32_t *dm, int32_t *da)
{
    int32_t m = 1 << 20, a = 1 << 20;
    switch (icon) {
    case ICON_AGENT:
        IC_M(IC_RING(ic_circ(X, Y, 32, 32, 20), 24));
        IC_A(ic_circ(X, Y, 32, 32, 9));
        IC_M(ic_circ(X, Y, 46, 18, 3));
        break;
    case ICON_TERMINAL:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 22, 17, 6), 24));
        IC_A(ic_seg(X, Y, 20, 26, 28, 32, 28));
        IC_A(ic_seg(X, Y, 28, 32, 20, 38, 28));
        IC_A(ic_seg(X, Y, 33, 39, 44, 39, 28));
        break;
    case ICON_BROWSER:
        IC_M(IC_RING(ic_circ(X, Y, 32, 32, 20), 24));
        IC_M(IC_RING(ic_ell(X, Y, 32, 32, 9, 20), 20));
        IC_A(ic_seg(X, Y, 12, 32, 52, 32, 20));
        break;
    case ICON_FILES:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 35, 22, 15, 5), 24));
        IC_A(ic_rbox(X, Y, 19, 19, 10, 4, 2));
        IC_A(ic_seg(X, Y, 22, 36, 42, 36, 20));
        break;
    case ICON_IMAGE:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 22, 18, 6), 24));
        IC_A(ic_circ(X, Y, 43, 24, 4));
        IC_M(ic_seg(X, Y, 15, 44, 25, 33, 24));
        IC_M(ic_seg(X, Y, 25, 33, 33, 41, 24));
        IC_M(ic_seg(X, Y, 33, 41, 40, 35, 24));
        IC_M(ic_seg(X, Y, 40, 35, 49, 44, 24));
        break;
    case ICON_NOTES:
        IC_M(IC_RING(ic_rbox(X, Y, 32, 32, 16, 22, 5), 24));
        IC_A(ic_seg(X, Y, 24, 23, 40, 23, 20));
        IC_A(ic_seg(X, Y, 24, 31, 40, 31, 20));
        IC_A(ic_seg(X, Y, 24, 39, 34, 39, 20));
        break;
    case ICON_MEMORY:
        IC_A(ic_circ(X, Y, 32, 17, 6));
        IC_M(ic_seg(X, Y, 32, 23, 16, 42, 20));
        IC_M(ic_seg(X, Y, 32, 23, 32, 42, 20));
        IC_M(ic_seg(X, Y, 32, 23, 48, 42, 20));
        IC_M(IC_RING(ic_circ(X, Y, 16, 46, 5), 20));
        IC_M(IC_RING(ic_circ(X, Y, 32, 46, 5), 20));
        IC_M(IC_RING(ic_circ(X, Y, 48, 46, 5), 20));
        break;
    case ICON_LAYOUT:
        IC_M(IC_RING(ic_rbox(X, Y, 21, 32, 10, 20, 3), 20));
        IC_M(IC_RING(ic_rbox(X, Y, 43, 23, 10, 10, 3), 20));
        IC_A(ic_rbox(X, Y, 43, 45, 10, 6, 3));
        break;
    case ICON_PAINT:
        IC_M(IC_RING(ic_circ(X, Y, 30, 32, 18), 24));
        IC_A(ic_circ(X, Y, 24, 25, 4));
        IC_A(ic_circ(X, Y, 36, 23, 4));
        IC_A(ic_circ(X, Y, 42, 33, 4));
        IC_M(ic_circ(X, Y, 25, 40, 4));
        IC_A(ic_seg(X, Y, 44, 16, 52, 8, 20));
        break;
    case ICON_AGENTS:
    default:
        IC_M(ic_seg(X, Y, 32, 17, 15, 46, 18));
        IC_M(ic_seg(X, Y, 32, 17, 49, 46, 18));
        IC_M(ic_seg(X, Y, 15, 46, 49, 46, 18));
        IC_A(ic_circ(X, Y, 32, 17, 7));
        IC_A(ic_circ(X, Y, 15, 46, 7));
        IC_A(ic_circ(X, Y, 49, 46, 7));
        IC_M(ic_circ(X, Y, 32, 37, 3));
        break;
    }
    *dm = m;
    *da = a;
}

/* Composicion "over" con alfa 0..256 (a tambien en 0..256). */
static void ic_over(uint32_t *r, uint32_t *g, uint32_t *b, uint32_t *a,
                    uint32_t col, uint32_t c)
{
    if (!c) return;
    uint32_t ia = (*a * (256u - c)) >> 8;
    uint32_t na = ia + c;
    *r = (((col >> 16) & 255u) * c + *r * ia) / na;
    *g = (((col >> 8) & 255u) * c + *g * ia) / na;
    *b = ((col & 255u) * c + *b * ia) / na;
    *a = na;
}

static uint32_t ic_cov(int32_t d, int32_t pw)
{
    int32_t v = 128 - (d * 256) / pw;
    if (v < 0) v = 0;
    if (v > 256) v = 256;
    return (uint32_t)v;
}

static void her_icon_render(int icon, uint32_t col, int S, uint32_t *out)
{
    uint32_t acc = her_mix256(col, 0x00FFFFFFu, 170u);
    int32_t pw = 1024 / S;
    if (pw < 1) pw = 1;

    for (int j = 0; j < S; ++j) {
        int32_t Y = ((2 * j + 1) * 512) / S;
        for (int i = 0; i < S; ++i) {
            int32_t X = ((2 * i + 1) * 512) / S;
            int32_t dm, da;
            uint32_t r = 0, g = 0, b = 0, a = 0;
            her_icon_dist(icon, X, Y, &dm, &da);
            if (dm > 0 && dm < 112)
                ic_over(&r, &g, &b, &a, col, (uint32_t)((112 - dm) * 44 / 112));
            ic_over(&r, &g, &b, &a, col, ic_cov(dm, pw));
            ic_over(&r, &g, &b, &a, acc, ic_cov(da, pw));
            out[j * S + i] = ((a > 255u ? 255u : a) << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

static const uint32_t *her_icon_get(int i, int S)
{
    if (S > HER_ICON_MAX_S) S = HER_ICON_MAX_S;
    if (!her_icon_sprites) {
        her_icon_sprites = (uint32_t *)kmalloc((size_t)HER_DOCK_ITEM_COUNT *
                           HER_ICON_MAX_S * HER_ICON_MAX_S * sizeof(uint32_t));
        if (!her_icon_sprites) return 0;
        for (int k = 0; k < HER_DOCK_ITEM_COUNT; ++k) her_icon_ready[k] = 0;
    }
    if (her_icon_sprite_s != S) {
        for (int k = 0; k < HER_DOCK_ITEM_COUNT; ++k) her_icon_ready[k] = 0;
        her_icon_sprite_s = S;
    }
    uint32_t *spr = her_icon_sprites + (size_t)i * HER_ICON_MAX_S * HER_ICON_MAX_S;
    if (!her_icon_ready[i]) {
        her_icon_render(her_dock_items[i].icon, her_dock_items[i].color, S, spr);
        her_icon_ready[i] = 1;
    }
    return spr;
}

/* Copia el sprite (lado S) centrado, escalado a D px, con alfa global 0..256. */
static void her_icon_blit(int32_t cx, int32_t cy, const uint32_t *spr, int S, int D,
                          uint32_t bright)
{
    volatile uint32_t *buf = fb_get_buffer();
    if (!buf || !spr || D < 2) return;
    int32_t W = (int32_t)fb_get_width(), H = (int32_t)fb_get_height();
    int32_t x0 = cx - D / 2, y0 = cy - D / 2;
    fb_damage(x0, y0, D, D);

    for (int y = 0; y < D; ++y) {
        int32_t py = y0 + y;
        if (py < 0 || py >= H) continue;
        const uint32_t *row = spr + (y * S / D) * S;
        for (int x = 0; x < D; ++x) {
            int32_t px = x0 + x;
            if (px < 0 || px >= W) continue;
            uint32_t p = row[x * S / D];
            uint32_t al = ((p >> 24) * bright) >> 8;
            if (!al) continue;
            uint32_t bg = buf[py * W + px];
            buf[py * W + px] = fb_blend_color(bg, (al << 24) | (p & 0x00FFFFFFu)) & 0x00FFFFFFu;
        }
    }
}

static void her_draw_dock(uint32_t cur_w, uint32_t cur_h)
{
    uint64_t now_ms = timer_get_uptime_ms();
    her_draw_icon_burst(cur_w, cur_h, now_ms);
    if (her_icons_anim_phase == 0u) return;

    int32_t cx = (int32_t)(cur_w / 2u);
    int32_t cy = (int32_t)(cur_h / 2u);
    int S = her_icon_canvas(cur_h);
    int32_t half_px = S * 11 / 32;

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        uint32_t p = her_icon_item_progress(i);
        if (p == 0u) continue;

        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);

        int32_t px = cx + ((target_x - cx) * (int32_t)p) / (int32_t)HER_ICON_ANIM_MAX;
        int32_t py = cy + ((target_y - cy) * (int32_t)p) / (int32_t)HER_ICON_ANIM_MAX;
        her_draw_icon_materialization(i, px, py, p, now_ms, cur_w, cur_h);

        /* Fase final: el icono crece y se enciende donde se condensan las particulas. */
        if (p < 200u) continue;
        uint32_t solid = p - 200u;                       /* 0..56 */
        uint32_t bright = 40u + (solid * 216u) / 56u;
        if (bright > 256u) bright = 256u;
        int D = S * (80 + (int)((solid * 176u) / 56u)) / 256;
        if (D > S) D = S;

        const uint32_t *spr = her_icon_get(i, S);
        if (spr) her_icon_blit(target_x, target_y, spr, S, D, bright);

        if (p >= HER_ICON_ANIM_MAX) {
            uint32_t label_len = 0;
            while (her_dock_items[i].label[label_len]) label_len++;
            int32_t label_x = target_x - (int32_t)(label_len * 8u) / 2;
            int32_t label_y = target_y + half_px + 4;
            uint32_t label_color = her_scale(0x00A9BCD1u, 230u);
            font_draw_string((uint32_t)(label_x < 0 ? 0 : label_x),
                             (uint32_t)label_y,
                             her_dock_items[i].label, label_color, 1);

            /* Indicador de app abierta: punto brillante bajo la etiqueta. */
            int is_open = (her_find_window(her_dock_items[i].app) >= 0);
            if (is_open) {
                uint32_t dot_col = (current_app == her_dock_items[i].app) ? 0x00E2F1FFu : 0x0064748Bu;
                fb_fill_circle(target_x, label_y + 16, (current_app == her_dock_items[i].app) ? 3 : 2, dot_col);
            }
        }
    }
}

static int her_dock_hit(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h)
{
    if (!her_icons_visible) return -1;
    uint64_t now_ms = timer_get_uptime_ms();
    const int32_t half = her_icon_canvas(cur_h) * 11 / 32;

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        uint32_t p = her_icon_item_progress(i);
        if (p < 232u) continue;

        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);
        uint32_t label_len = 0;
        while (her_dock_items[i].label[label_len]) label_len++;
        int32_t label_half = (int32_t)(label_len * 8u) / 2;
        int32_t pad_x = (label_half > half) ? label_half : half;
        if (mx >= target_x - pad_x - 4 && mx <= target_x + pad_x + 4 &&
            my >= target_y - half - 4 && my <= target_y + half + 24)
            return (int)her_dock_items[i].app;
    }
    return -1;
}

static void her_draw_one_window_chrome(struct her_win_node *win, int is_active)
{
    if (!win) return;
    uint32_t x = win->x, y = win->y, w = win->w, h = win->h;
    uint32_t border_col = is_active ? 0x0038BDF8 : 0x00334155;
    uint32_t title_bg   = is_active ? 0x001E293B : 0x000F172A;
    uint32_t title_txt  = is_active ? 0x00F8FAFC : 0x0094A3B8;

    fb_fill_rect(x + 4, y + 4, w, h, 0x0008080C);
    fb_fill_rounded_rect((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, 6, 0x000B1220);
    fb_draw_rounded_rect((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, 6, border_col);
    fb_fill_rounded_rect((int32_t)x, (int32_t)y, (int32_t)w, HER_TITLE_H, 6, title_bg);
    fb_fill_rect(x, y + HER_TITLE_H - 4, w, 4, title_bg);

    const char *title = "SOMA";
    switch (win->app) {
        case APP_AGENT_CHAT:     title = "Agente SOMA"; break;
        case APP_TERMINAL_SHELL: title = "Terminal"; break;
        case APP_WEB_BROWSER:    title = "Navegador"; break;
        case APP_FILE_MANAGER:   title = "Archivos"; break;
        case APP_IMAGE_VIEWER:   title = "Imagenes"; break;
        case APP_NOTEPAD:        title = "Notepad"; break;
        case APP_AGENT_MEMORY:   title = "Memoria"; break;
        case APP_LAYOUT:         title = "Maquetador"; break;
        case APP_AGENTS:         title = "Agentes"; break;
        case APP_PAINT:          title = "SOMA Paint"; break;
        default: break;
    }
    font_draw_string(x + 12, y + 8, title, title_txt, 1);
    font_draw_string(x + w - 24, y + 8, "x", is_active ? 0x00EF4444 : 0x00991B1B, 1);
    /* Tirador táctil en esquina inferior derecha */
    uint32_t grip_col = is_active ? 0x0038BDF8 : 0x00475569;
    fb_fill_rect(x + w - 14, y + h - 5, 10, 2, grip_col);
    fb_fill_rect(x + w - 10, y + h - 9,  6, 2, grip_col);
    fb_fill_rect(x + w - 6,  y + h - 13, 2, 2, grip_col);
}

static void her_draw_window_chrome(uint32_t cur_w, uint32_t cur_h)
{
    (void)cur_w; (void)cur_h;
    for (int i = 0; i < her_num_windows; ++i) {
        her_draw_one_window_chrome(&her_windows[i], (i == her_num_windows - 1));
    }
}

static int her_win_hit_target(struct her_win_node *win, int32_t mx, int32_t my, int *out_edge)
{
    *out_edge = 0;
    if (!win) return 0;
    const int32_t OUT = 8, IN = 5, CORNER = 14;
    int32_t x = (int32_t)win->x, y = (int32_t)win->y;
    int32_t w = (int32_t)win->w, h = (int32_t)win->h;

    if (mx < x - OUT || mx > x + w + OUT || my < y - OUT || my > y + h + OUT)
        return 0;

    /* Boton cerrar */
    if (mx >= x + w - 28 && mx <= x + w - 4 && my >= y + 2 && my <= y + HER_TITLE_H - 2)
        return 3;

    /* Tirador de esquina inferior derecha */
    if (mx >= x + w - 18 && my >= y + h - 18) { *out_edge = 2 | 8; return 2; }

    int edge = 0;
    if (mx <= x + IN)     edge |= 1;
    if (mx >= x + w - IN) edge |= 2;
    if (my <= y + IN)     edge |= 4;
    if (my >= y + h - IN) edge |= 8;
    /* Esquinas: zona ampliada para poder redimensionar en diagonal */
    if (edge & 3)  { if (my <= y + CORNER) edge |= 4; else if (my >= y + h - CORNER) edge |= 8; }
    if (edge & 12) { if (mx <= x + CORNER) edge |= 1; else if (mx >= x + w - CORNER) edge |= 2; }
    if (edge) { *out_edge = edge; return 2; }

    if (my >= y && my < y + HER_TITLE_H) return 1;
    return 4;
}

static int __attribute__((unused)) her_win_hit(int32_t mx, int32_t my, int *out_edge)
{
    *out_edge = 0;
    struct her_win_node *win = her_get_active_win();
    if (!win) return 0;
    return her_win_hit_target(win, mx, my, out_edge);
}

static void her_win_apply_motion(int32_t mx, int32_t my, uint32_t cur_w, uint32_t cur_h)
{
    struct her_win_node *win = (her_num_windows > 0) ? &her_windows[her_num_windows - 1] : 0;
    if (!win) return;
    int32_t max_w = (int32_t)cur_w - 20;
    int32_t max_h = (int32_t)cur_h - 20;
    int32_t min_w = (int32_t)her_app_min_w(win->app);
    int32_t min_h = (cur_h < 650u) ? HER_MIN_H : 260;
    if (min_w > max_w) min_w = max_w;
    if (min_h > max_h) min_h = max_h;

    if (her_dragging) {
        int32_t nx = mx - her_drag_ox;
        int32_t ny = my - her_drag_oy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + (int32_t)win->w > (int32_t)cur_w) nx = (int32_t)cur_w - (int32_t)win->w;
        if (ny + (int32_t)win->h > (int32_t)cur_h) ny = (int32_t)cur_h - (int32_t)win->h;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        win->x = (uint32_t)nx;
        win->y = (uint32_t)ny;
        her_win_x = win->x; her_win_y = win->y;
    } else if (her_resizing) {
        int32_t dx = mx - her_resize_start_mx;
        int32_t dy = my - her_resize_start_my;
        int32_t new_x = her_resize_start_x;
        int32_t new_y = her_resize_start_y;
        int32_t new_w = her_resize_start_w;
        int32_t new_h = her_resize_start_h;

        /* Borde derecho */
        if (her_resize_edge & 2) {
            new_w = her_resize_start_w + dx;
            if (new_w < min_w) new_w = min_w;
            if (new_x + new_w > (int32_t)cur_w) new_w = (int32_t)cur_w - new_x;
        }
        /* Borde izquierdo */
        if (her_resize_edge & 1) {
            new_w = her_resize_start_w - dx;
            new_x = her_resize_start_x + dx;
            if (new_w < min_w) {
                new_x = her_resize_start_x + (her_resize_start_w - min_w);
                new_w = min_w;
            }
            if (new_x < 0) {
                new_w += new_x;
                new_x = 0;
            }
        }
        /* Borde inferior */
        if (her_resize_edge & 8) {
            new_h = her_resize_start_h + dy;
            if (new_h < min_h) new_h = min_h;
            if (new_y + new_h > (int32_t)cur_h) new_h = (int32_t)cur_h - new_y;
        }
        /* Borde superior */
        if (her_resize_edge & 4) {
            new_h = her_resize_start_h - dy;
            new_y = her_resize_start_y + dy;
            if (new_h < min_h) {
                new_y = her_resize_start_y + (her_resize_start_h - min_h);
                new_h = min_h;
            }
            if (new_y < 0) {
                new_h += new_y;
                new_y = 0;
            }
        }

        if (new_w < min_w) new_w = min_w;
        if (new_h < min_h) new_h = min_h;
        if (new_w > max_w) new_w = max_w;
        if (new_h > max_h) new_h = max_h;

        win->x = (uint32_t)new_x;
        win->y = (uint32_t)new_y;
        win->w = (uint32_t)new_w;
        win->h = (uint32_t)new_h;

        her_win_x = win->x; her_win_y = win->y;
        her_win_w = win->w; her_win_h = win->h;
    }
}

static char     cur_cmd[128];
static uint32_t cur_cmd_len = 0;
static uint32_t cur_cursor_pos = 0;
static volatile int her_anim_alive = 0;

static void render_desktop(void);
static void her_refresh_all(void);
static void her_vortex_close_step(void);
static void her_draw_vortex_particles(void);
static int her_cursor_should_show(void);
static void her_anim_start(void);
static void her_anim_halt(void);
static void her_anim_stop(void);

#define HER_SYNC_WIN() do {                                              \
        win_x = her_win_x;                                               \
        win_y = her_win_y + HER_TITLE_H;                                 \
        win_w = her_win_w;                                               \
        win_h = (her_win_h > HER_TITLE_H) ? her_win_h - HER_TITLE_H : 1; \
        g_live_win_x = win_x; g_live_win_y = win_y;                      \
        g_live_win_w = win_w; g_live_win_h = win_h;                      \
    } while (0)

static void log_add_raw(struct gui_chat_line *lines, int *count, const char *msg, uint32_t color) {
    ensure_agent_log();
    if (!lines) lines = agent_log;
    if (!lines) return;
    if (*count >= GUI_MAX_LOG_LINES) { for (int i=0; i<GUI_MAX_LOG_LINES-1; ++i) lines[i] = lines[i+1]; *count = GUI_MAX_LOG_LINES-1; }
    uint32_t p = 0; while (*msg && p < GUI_LINE_MAX_LEN-1) lines[*count].text[p++] = *msg++;
    lines[*count].text[p] = '\0'; lines[*count].color = color; (*count)++;
}

static void log_add(struct gui_chat_line *lines, int *count, const char *prefix, const char *msg, uint32_t color, uint32_t max_chars) {
    if (max_chars == 0 || max_chars > GUI_LINE_MAX_LEN - 4) max_chars = GUI_LINE_MAX_LEN - 4;
    char line_buf[GUI_LINE_MAX_LEN]; uint32_t lp = 0;
    if (prefix) while (*prefix && lp < max_chars-1) line_buf[lp++] = *prefix++;
    while (*msg) {
        if (*msg == '\n' || *msg == '\r') { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); lp = 0; line_buf[lp++]=' '; line_buf[lp++]=' '; msg++; continue; }
        line_buf[lp++] = *msg++;
        if (lp >= max_chars - 2) { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); lp = 0; line_buf[lp++]=' '; line_buf[lp++]=' '; }
    }
    if (lp > 2 || (prefix && lp > 0)) { line_buf[lp] = '\0'; log_add_raw(lines, count, line_buf, color); }
}

#define TERM_MAX_ROWS   36
#define TERM_LINE_LEN 256
static char term_lines[TERM_MAX_ROWS][TERM_LINE_LEN];
static int  term_num_lines = 0;
static char term_hist[16][128];
static int  term_hist_count = 0, term_hist_idx = -1;

static void term_scroll_up(void) {
    if (term_num_lines == 0) return;
    for (int i=0; i<term_num_lines-1; ++i) for (int c=0; c<TERM_LINE_LEN; ++c) term_lines[i][c] = term_lines[i+1][c];
    term_num_lines--;
}

static void term_add_line(const char *line, uint32_t max_visible) {
    while (term_num_lines >= (int)max_visible - 1 && term_num_lines > 0) term_scroll_up();
    if (term_num_lines < TERM_MAX_ROWS) { uint32_t p = 0; while (*line && p < TERM_LINE_LEN-1) term_lines[term_num_lines][p++] = *line++; term_lines[term_num_lines][p] = '\0'; term_num_lines++; }
}

static void term_init_banner(uint32_t max_visible) {
    term_num_lines = 0;
    term_add_line("\033[1;36m  ____  ____  __  __    _      \033[1;33mOS:     \033[1;37mSOMA v0.2 \033[0;36m(Multi-Agente)\033[0m", max_visible);
    term_add_line("\033[1;36m / ___|/ __ \\|  \\/  |  / \\     \033[1;33mKernel: \033[1;37mx86_64 Bare-Metal \033[1;32m(HAL v1)\033[0m", max_visible);
    term_add_line("\033[1;36m \\___ \\ |  | | |\\/| | / _ \\    \033[1;33mUptime: \033[1;37mPIT 1 kHz \033[0;32m(1 ms tick)\033[0m", max_visible);
    term_add_line("\033[1;36m  ___)| |__| | |  | |/ ___ \\   \033[1;33mCPU:    \033[1;37mRing 0 \033[1;35m(CR0:WP, NXE)\033[0m", max_visible);
    term_add_line("\033[1;36m |____/\\____/|_|  |_/_/   \\_\\  \033[1;33mRAM:    \033[1;37m512 MiB \033[1;32m(PMM Allocator)\033[0m", max_visible);
    term_add_line("                               \033[1;33mDisco:  \033[1;37mRamFS (16 MiB virtio-blk)\033[0m", max_visible);
    term_add_line("                               \033[1;33mRed:    \033[1;37m10.0.2.15 (VirtIO-NET NAT)\033[0m", max_visible);
    term_add_line("                               \033[1;33mAgente: \033[1;37mnail-35b (ReAct LAN)\033[0m", max_visible);
    term_add_line("", max_visible);
    term_add_line("\033[1;36m----------------------------------------------------------------------\033[0m", max_visible);
    term_add_line(" \033[1;37mSOMA 0.2 :: Escribe '\033[1;32msoma <mision>\033[1;37m' para el Agente o '\033[1;33mhelp\033[1;37m'.\033[0m", max_visible);
    term_add_line("\033[1;36m----------------------------------------------------------------------\033[0m", max_visible);
}

static void render_real_terminal(uint32_t lx, uint32_t ly, uint32_t lw, uint32_t lh, const char *cmd, uint32_t cmd_len, uint32_t cursor_pos, int show_cursor) {
    fb_fill_rect(lx, ly, lw, lh, 0x00050810); fb_draw_rect(lx, ly, lw, lh, 0x0010B981);
    fb_fill_rect(lx, ly, lw, 36, 0x001E293B); fb_draw_rect(lx, ly, lw, 36, 0x00334155);
    font_draw_string(lx + 16, ly + 14, "soma@soma-node1: ~ (tty1) - Terminal SOMA", 0x00F8FAFC, 1);
    uint32_t line_y = ly + 48;
    for (int i = 0; i < term_num_lines; ++i) { font_draw_string_ansi(lx + 16, line_y, term_lines[i], 0x00F8FAFC); line_y += 15; }
    uint32_t prompt_x = lx + 16;
    font_draw_string_ansi(prompt_x, line_y, "\033[1;36msoma\033[1;32m>\033[0m ", 0x0038BDF8);
    uint32_t text_x = prompt_x + 48;
    fb_set_clip((int32_t)text_x, (int32_t)line_y, (int32_t)(lw - 80), 16);
    int offset = 0;
    if (cursor_pos * 8 > lw - 100) offset = cursor_pos * 8 - (lw - 100);
    font_draw_string(text_x - offset, line_y, cmd, 0x00F8FAFC, 1);
    if (show_cursor) {
        uint32_t cur_px = text_x - offset + cursor_pos * 8;
        fb_fill_rect(cur_px, line_y, 8, 12, COLOR_CYAN);
        if (cursor_pos < cmd_len && cmd[cursor_pos] >= 32 && cmd[cursor_pos] <= 126) {
            char ch_tmp[2] = {cmd[cursor_pos], '\0'};
            font_draw_string(cur_px, line_y, ch_tmp, 0x00000000, 1);
        }
    }
    fb_reset_clip();
}

static void render_log_window_chat(struct gui_chat_line *lines, int count, uint32_t lx, uint32_t ly, uint32_t lw, uint32_t lh) {
    fb_fill_rect(lx, ly, lw, lh, 0x0004070E); fb_draw_rect(lx, ly, lw, lh, COLOR_CYAN);
    fb_fill_rect(lx, ly, lw, 36, 0x001E293B); fb_draw_rect(lx, ly, lw, 36, 0x00334155);
    font_draw_string(lx + 16, ly + 14, "SOMA v0.2 :: Agente IA (ReAct & LLM)", 0x00F8FAFC, 1);
    uint32_t max_visible = (lh > 50) ? (lh - 50) / 16 : 10;
    int start_idx = 0; if (count > (int)max_visible) start_idx = count - (int)max_visible;
    uint32_t line_y = ly + 48;
    for (int i = start_idx; i < count; ++i) { font_draw_string(lx + 16, line_y, lines[i].text, lines[i].color, 1); line_y += 16; }
}

static void render_input_bar_chat(const char *prompt, uint32_t prompt_color, uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh, const char *text, uint32_t len, uint32_t cursor_pos, int show_cursor) {
    fb_fill_rect(bx, by, bw, bh, 0x00020617); fb_draw_rect(bx, by, bw, bh, prompt_color);
    font_draw_string(bx + 14, by + 14, prompt, prompt_color, 2);
    uint32_t p_len = 0; while (prompt[p_len]) p_len++;
    uint32_t text_x = bx + 14 + (p_len * 8 * 2);
    fb_set_clip((int32_t)text_x, (int32_t)(by + 2), (int32_t)(bw - 28 - (p_len * 16)), (int32_t)(bh - 4));
    int offset = 0;
    if (cursor_pos * 16 > bw - 60 - (p_len * 16)) offset = cursor_pos * 16 - (bw - 60 - (p_len * 16));
    font_draw_string(text_x - offset, by + 14, text, 0x00F8FAFC, 2);
    if (show_cursor) {
        uint32_t cx = text_x - offset + (cursor_pos * 16);
        fb_fill_rect(cx, by + 12, 16, 20, prompt_color);
        if (cursor_pos < len && text[cursor_pos] >= 32 && text[cursor_pos] <= 126) {
            char ch[2] = { text[cursor_pos], '\0' };
            font_draw_string(cx, by + 14, ch, 0x00000000, 2);
        }
    }
    fb_reset_clip();
}

static void render_agent_global_rail(uint32_t cur_w, uint32_t cur_h)
{
    (void)cur_w; (void)cur_h;
    her_refresh_all();
}

static void submit_global_agent_prompt(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    (void)win_x; (void)win_y; (void)win_w; (void)win_h;
    if (agent_global_len == 0) return;

    char mission_copy[SOMA_AGENT_CMD_MAX];
    uint32_t n = agent_global_len;
    if (n >= sizeof(mission_copy)) n = sizeof(mission_copy) - 1;
    for (uint32_t i = 0; i < n; ++i) mission_copy[i] = agent_global_cmd[i];
    mission_copy[n] = '\0';

    const uint32_t wrap = 76;
    log_add(agent_log, &agent_log_count, "user> ", mission_copy, COLOR_CYAN, wrap);
    her_begin_turn(mission_copy);

    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';
    agent_global_scroll = 0;
    her_prev_len = 0;
    her_pulse = 0;
    agent_last_response[0] = '\0';

    her_refresh_all();
    fb_present();

    her_anim_start();
    agent_run("soma", mission_copy);
    her_anim_stop();

    her_finish_turn(agent_last_response);

    if (agent_last_response[0] != '\0')
        log_add(agent_log, &agent_log_count, "[soma]: ", agent_last_response, 0x0034D399, wrap);
    else
        log_add(agent_log, &agent_log_count, "[soma]: ", "Mision ejecutada en hardware.", 0x0034D399, wrap);

    agent_global_scroll = 0;
    her_refresh_all();
    fb_present();
}

/* ====================================================================
 * SOMA_ORB_FX_PATCH (A): orbe estilo XMB/PS3, absorcion y expulsion
 * Todo en enteros. Mezcla ADITIVA directamente sobre el backbuffer.
 * ==================================================================== */
#define OFX_LEVEL 3
#if OFX_LEVEL >= 3
#define OFX_HALO_STEP 1
#define OFX_RIB_N     5
#define OFX_RIB_STEP  1
#define OFX_BURST_APP 170
#define OFX_BURST_ICO 150
#elif OFX_LEVEL == 2
#define OFX_HALO_STEP 1
#define OFX_RIB_N     5
#define OFX_RIB_STEP  2
#define OFX_BURST_APP 110
#define OFX_BURST_ICO 100
#else
#define OFX_HALO_STEP 2
#define OFX_RIB_N     3
#define OFX_RIB_STEP  3
#define OFX_BURST_APP 60
#define OFX_BURST_ICO 60
#endif

#define OFX_EJ_MAX   384
#define OFX_RING_MAX 8

struct ofx_ejecta {
    int32_t  x, y, vx, vy;          /* 24.8 */
    int16_t  life, maxlife;
    int8_t   turn;
    uint8_t  alive;
    uint32_t col;
};

struct ofx_ring_t {
    int32_t  r, speed;
    int16_t  life, maxlife;
    uint32_t col;
    uint8_t  alive;
};

struct ofx_rib_cfg {
    int16_t  rad, amp;              /* radio y amplitud (px a 1080p) */
    uint8_t  harm, sq;              /* armonicos de la onda, aplastamiento /256 */
    int16_t  tilt;                  /* inclinacion base (1024 = 360 grados) */
    uint8_t  wid, alpha;            /* semi-ancho maximo, opacidad */
    int32_t  spd;                   /* velocidad (con signo) */
    uint32_t col;
};

static const struct ofx_rib_cfg ofx_rib[5] = {
    { 150, 14, 3, 110,  -40,  9, 200,  300, 0x009FD8FFu },
    { 190, 20, 2,  90,   30, 12, 170, -420, 0x004F8CFFu },
    { 235, 26, 4,  80,  -15, 14, 150,  520, 0x008B5CF6u },
    { 175, 12, 5, 130,   70,  6, 150, -240, 0x0038BDF8u },
    { 280, 30, 3,  70,   10, 16, 120,  380, 0x00E0F2FEu },
};

static const uint32_t ofx_pal[10] = {
    0x0038BDF8u, 0x0010B981u, 0x00F59E0Bu, 0x0038BDF8u, 0x0010B981u,
    0x00F472B6u, 0x00A855F7u, 0x000EA5E9u, 0x0034D399u, 0x00F59E0Bu
};

static struct ofx_ejecta  ofx_ej[OFX_EJ_MAX];
static struct ofx_ring_t  ofx_rings[OFX_RING_MAX];
static int       ofx_ej_next = 0;
static int       ofx_ring_next = 0;
static uint32_t  ofx_flash = 0;
static uint32_t  ofx_absorb_n = 0;
static uint32_t  ofx_ph[5];
static uint64_t  ofx_phf[5];      /* fase con 16 bits fraccionarios (PARCHE V2) */
static int32_t   ofx_sp_f = 0;    /* velocidad suavizada x256 */
static uint32_t  ofx_last_ms = 0;
static uint32_t  ofx_acc = 0;
static int       ofx_prev_icons = 0;
static int       ofx_prev_app = -1;
static int       ofx_jet_left = 0;
static uint32_t  ofx_jet_col = 0;
static int32_t   ofx_jet_dx = 0, ofx_jet_dy = 0;
static uint32_t  ofx_halo[256];

static inline uint32_t ofx_mul(uint32_t c, uint32_t k)      /* k 0..256 */
{
    if (k >= 256u) return c & 0x00FFFFFFu;
    uint32_t r = (((c >> 16) & 255u) * k) >> 8;
    uint32_t g = (((c >> 8) & 255u) * k) >> 8;
    uint32_t b = ((c & 255u) * k) >> 8;
    return (r << 16) | (g << 8) | b;
}

static inline void ofx_px(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t x, int32_t y, uint32_t a)
{
    if (x < 0 || y < 0 || x >= w || y >= h || !a) return;
    volatile uint32_t *p = bb + (uint32_t)y * (uint32_t)w + (uint32_t)x;
    uint32_t d = *p;
    uint32_t r = ((d >> 16) & 255u) + ((a >> 16) & 255u);
    uint32_t g = ((d >> 8) & 255u) + ((a >> 8) & 255u);
    uint32_t b = (d & 255u) + (a & 255u);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    *p = (r << 16) | (g << 8) | b;
}

/* Linea aditiva con degradado de color c0 (inicio) -> c1 (fin) y borde suave */
static void ofx_line(volatile uint32_t *bb, int32_t w, int32_t h,
                     int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint32_t c0, uint32_t c1)
{
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int32_t n = adx > ady ? adx : ady;
    if (n > 420) return;
    if (n == 0) { ofx_px(bb, w, h, x0, y0, c1); return; }
    int horiz = adx > ady;
    for (int32_t i = 0; i <= n; ++i) {
        int32_t x = x0 + dx * i / n;
        int32_t y = y0 + dy * i / n;
        uint32_t c = her_mix256(c0, c1, (uint32_t)(256 * i / n));
        ofx_px(bb, w, h, x, y, c);
        uint32_t s = ofx_mul(c, 90u);
        if (horiz) ofx_px(bb, w, h, x, y + 1, s);
        else       ofx_px(bb, w, h, x + 1, y, s);
    }
}

static void ofx_draw_ring(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t cx, int32_t cy, int32_t r, uint32_t col)
{
    if (r < 2) return;
    uint32_t stp = (r < 60) ? 4u : ((r < 120) ? 2u : 1u);
    uint32_t soft = ofx_mul(col, 120u);
    for (uint32_t a = 0; a < 1024u; a += stp) {
        int32_t c = her_cs(a), s = her_sn(a);
        ofx_px(bb, w, h, cx + ((r * c) >> 10), cy + ((r * s) >> 10), col);
        ofx_px(bb, w, h, cx + (((r + 1) * c) >> 10), cy + (((r + 1) * s) >> 10), soft);
        ofx_px(bb, w, h, cx + (((r - 1) * c) >> 10), cy + (((r - 1) * s) >> 10), soft);
    }
}

static int32_t ofx_radius(int32_t h)
{
    int32_t R = h / 5;
    if (R < 100) R = 100;
    if (R > 300) R = 300;
    return R;
}

static uint32_t ofx_app_color(int app)
{
    switch (app) {
    case APP_TERMINAL_SHELL: return 0x0010B981u;
    case APP_AGENT_CHAT:     return 0x0038BDF8u;
    case APP_WEB_BROWSER:    return 0x00F59E0Bu;
    case APP_FILE_MANAGER:   return 0x0038BDF8u;
    case APP_IMAGE_VIEWER:   return 0x0010B981u;
    case APP_NOTEPAD:        return 0x00F472B6u;
    case APP_AGENT_MEMORY:   return 0x00A855F7u;
    case APP_LAYOUT:         return 0x000EA5E9u;
    case APP_AGENTS:         return 0x0034D399u;
    case APP_PAINT:          return 0x00F59E0Bu;
    default:                 return 0x007DD3FCu;
    }
}

/* ---------- generadores ---------- */
static void ofx_spawn_ring(int32_t r, int32_t speed, int life, uint32_t col)
{
    struct ofx_ring_t *g = &ofx_rings[ofx_ring_next];
    ofx_ring_next = (ofx_ring_next + 1) % OFX_RING_MAX;
    g->r = r; g->speed = speed;
    g->life = (int16_t)life; g->maxlife = (int16_t)life;
    g->col = col; g->alive = 1;
}

static void ofx_spawn_ej(int32_t cx, int32_t cy, int32_t dirx, int32_t diry,
                         uint32_t col, int radial, int32_t spmin, int32_t spvar)
{
    struct ofx_ejecta *e = &ofx_ej[ofx_ej_next];
    ofx_ej_next = (ofx_ej_next + 1) % OFX_EJ_MAX;

    int32_t r0 = 30 + (int32_t)(her_rand() & 7u);
    int32_t sp = spmin + (int32_t)(her_rand() % (uint32_t)spvar);
    int32_t sx, sy, ux, uy;

    if (radial) {
        uint32_t ang = her_rand() & 1023u;
        ux = her_cs(ang); uy = her_sn(ang);
        sx = cx + ((r0 * ux) >> 10);
        sy = cy + ((r0 * uy) >> 10);
    } else {
        int32_t s  = (int32_t)(her_rand() % 921u) - 460;     /* abanico */
        int32_t o  = (int32_t)(her_rand() % 61u) - 30;       /* origen en el borde */
        ux = dirx + ((-diry * s) >> 10);
        uy = diry + ((dirx * s) >> 10);
        sx = cx + ((r0 * dirx) >> 10) + ((-diry * o) >> 10);
        sy = cy + ((r0 * diry) >> 10) + ((dirx * o) >> 10);
    }
    e->x = sx << 8;  e->y = sy << 8;
    e->vx = (ux * sp) >> 2;
    e->vy = (uy * sp) >> 2;
    e->maxlife = (int16_t)(24 + (int32_t)(her_rand() % 20u));
    e->life = e->maxlife;
    e->turn = (her_rand() & 1u) ? 1 : -1;
    e->col = ((her_rand() & 3u) == 0u) ? her_mix256(col, 0x00FFFFFFu, 140u) : col;
    e->alive = 1;
}

static void ofx_trigger_app(int32_t cx, int32_t cy, int app)
{
    uint32_t col = ofx_app_color(app);
    int32_t tx = (int32_t)her_win_x + (int32_t)her_win_w / 2;
    int32_t ty = (int32_t)her_win_y + (int32_t)her_win_h / 2;
    int32_t dx = tx - cx, dy = ty - cy;
    uint32_t len = her_isqrt((uint32_t)(dx * dx + dy * dy));
    int radial_only = (len < 4u);
    if (!radial_only) {
        ofx_jet_dx = (dx * 1024) / (int32_t)len;
        ofx_jet_dy = (dy * 1024) / (int32_t)len;
    }
    ofx_jet_col = col;
    ofx_jet_left = radial_only ? 0 : 10;
    for (int i = 0; i < OFX_BURST_APP; ++i) {
        int rad = radial_only || (i % 5) == 0;
        ofx_spawn_ej(cx, cy, ofx_jet_dx, ofx_jet_dy, col, rad, 9, 14);
    }
    ofx_spawn_ring(30, 7, 22, col);
    ofx_spawn_ring(30, 4, 32, her_mix256(col, 0x00FFFFFFu, 120u));
    ofx_flash += 170u;
    if (ofx_flash > 255u) ofx_flash = 255u;
}

static void ofx_trigger_icons(int32_t cx, int32_t cy)
{
    for (int i = 0; i < OFX_BURST_ICO; ++i)
        ofx_spawn_ej(cx, cy, 0, 0, ofx_pal[i % 10], 1, 5, 10);
    ofx_spawn_ring(30, 6, 24, 0x0038BDF8u);
    ofx_spawn_ring(30, 4, 30, 0x00A855F7u);
    ofx_spawn_ring(30, 9, 18, 0x00F8FAFCu);
    ofx_flash += 130u;
    if (ofx_flash > 255u) ofx_flash = 255u;
}

/* Gancho llamado desde her_vortex_close_step cada vez que el horizonte absorbe */
static void ofx_on_absorb(uint32_t col)
{
    ofx_flash += 10u;
    if (ofx_flash > 255u) ofx_flash = 255u;
    if ((ofx_absorb_n++ & 15u) == 0u)
        ofx_spawn_ring(64, -3, 12, col & 0x00FFFFFFu);   /* onda que se contrae */
}

/* ---------- simulacion (30 Hz, independiente de cuantas veces se pinte) ---------- */
static void ofx_tick(int32_t cx, int32_t cy)
{
    if (ofx_jet_left > 0) {
        ofx_jet_left--;
        for (int i = 0; i < 9; ++i)
            ofx_spawn_ej(cx, cy, ofx_jet_dx, ofx_jet_dy, ofx_jet_col, 0, 10, 12);
    }
    for (int i = 0; i < OFX_EJ_MAX; ++i) {
        struct ofx_ejecta *e = &ofx_ej[i];
        if (!e->alive) continue;
        e->x += e->vx;
        e->y += e->vy;
        int32_t nvx = e->vx - (e->turn * (e->vy >> 5));
        int32_t nvy = e->vy + (e->turn * (e->vx >> 5));
        e->vx = (nvx * 246) >> 8;
        e->vy = (nvy * 246) >> 8;
        if (--e->life <= 0) e->alive = 0;
    }
    for (int i = 0; i < OFX_RING_MAX; ++i) {
        struct ofx_ring_t *g = &ofx_rings[i];
        if (!g->alive) continue;
        g->r += g->speed;
        if (--g->life <= 0 || g->r <= 26) g->alive = 0;
    }
}

static void ofx_frame_update(int32_t cx, int32_t cy)
{
    uint32_t now = (uint32_t)timer_get_uptime_ms();
    uint32_t dt = ofx_last_ms ? (now - ofx_last_ms) : 33u;
    ofx_last_ms = now;
    if (dt > 100u) dt = 100u;

    int sp = her_cur_speed;
    if (sp < 10) sp = 10;
    sp += soma_vortex_boost / 2;
    {
        int32_t tgt = (int32_t)sp * 256;
        if (ofx_sp_f == 0) ofx_sp_f = tgt;
        ofx_sp_f += ((tgt - ofx_sp_f) * (int32_t)dt) / 250;
        for (int k = 0; k < 5; ++k) {
            int64_t inc = (int64_t)dt * ofx_rib[k].spd * ofx_sp_f * 16;
            ofx_phf[k] += (uint64_t)inc;
            ofx_ph[k] = (uint32_t)(ofx_phf[k] >> 16);
        }
    }

    uint32_t dec = ofx_flash * dt * 3u / 1024u;
    if (ofx_flash && !dec) dec = 1u;
    ofx_flash = (dec >= ofx_flash) ? 0u : ofx_flash - dec;

    /* disparadores */
    int app = (int)current_app;
    if (ofx_prev_app >= 0 && app != ofx_prev_app && app != (int)APP_DESKTOP)
        ofx_trigger_app(cx, cy, app);
    ofx_prev_app = app;

    if (her_icons_visible && !ofx_prev_icons) ofx_trigger_icons(cx, cy);
    ofx_prev_icons = her_icons_visible;

    ofx_acc += dt;
    int ticks = 0;
    while (ofx_acc >= 33u && ticks < 3) { ofx_acc -= 33u; ticks++; ofx_tick(cx, cy); }
    if (ofx_acc > 99u) ofx_acc = 0u;
}

/* ---------- halo ---------- */
static void ofx_build_halo(uint32_t gain)               /* 256 = 1.0 */
{
    uint32_t c_in  = 0x00E8F6FFu;
    uint32_t c_mid = her_mix256(0x003B82F6u, her_col_cur & 0x00FFFFFFu, 70u);
    uint32_t c_out = 0x006D28D9u;
    for (uint32_t v = 0; v < 256u; ++v) {
        uint32_t col = (v < 36u) ? her_mix256(c_in, c_mid, v * 256u / 36u)
                                 : her_mix256(c_mid, c_out, (v - 36u) * 256u / 219u);
        uint32_t glow = 255u * 40u / (40u + v);
        glow = glow * (255u - v) / 255u;
        uint32_t k = glow * gain / 560u;
        if (k > 256u) k = 256u;
        ofx_halo[v] = ofx_mul(col, k);
    }
}

static void ofx_draw_halo(volatile uint32_t *bb, int32_t w, int32_t h,
                          int32_t cx, int32_t cy, int32_t R)
{
    int32_t R2 = R * R;
    uint32_t kmul = (uint32_t)(255 * 65536 / R2);
    for (int32_t y = cy - R; y < cy + R; y += OFX_HALO_STEP) {
        if (y < 0 || y >= h) continue;
        int32_t dy = y - cy, dy2 = dy * dy;
        for (int32_t x = cx - R; x < cx + R; x += OFX_HALO_STEP) {
            if (x < 0 || x >= w) continue;
            int32_t dx = x - cx;
            int32_t d2 = dx * dx + dy2;
            if (d2 >= R2 || d2 < 484) continue;
            uint32_t col = ofx_halo[(((uint32_t)d2 * kmul) >> 16) & 255u];
            if (!col) continue;
            for (int32_t sy = 0; sy < OFX_HALO_STEP; ++sy)
                for (int32_t sx = 0; sx < OFX_HALO_STEP; ++sx)
                    ofx_px(bb, w, h, x + sx, y + sy, col);
        }
    }
}

/* ---------- cintas de seda estilo XMB (PARCHE V2: continuidad subpixel) ---------- */
static int32_t ofx_sin16(uint32_t a)          /* a: 1/16384 de vuelta -> +-1024 */
{
    uint32_t i = (a >> 6) & 255u, f = a & 63u;
    int32_t s0 = HER_S[i], s1 = HER_S[(i + 1u) & 255u];
    return s0 + ((s1 - s0) * (int32_t)f) / 64;
}
static int32_t ofx_cos16(uint32_t a) { return ofx_sin16(a + 4096u); }

static uint32_t ofx_dbl(uint32_t c)
{
    uint32_t r = ((c >> 16) & 255u) * 2u, g = ((c >> 8) & 255u) * 2u, b = (c & 255u) * 2u;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return (r << 16) | (g << 8) | b;
}

/* Reparto bilineal de un punto en coordenadas x1024 */
static void ofx_splat(volatile uint32_t *bb, int32_t w, int32_t h,
                      int32_t x1024, int32_t y1024, uint32_t col)
{
    int32_t ix = x1024 >> 10, iy = y1024 >> 10;
    uint32_t fx = (uint32_t)(x1024 & 1023) >> 2;
    uint32_t fy = (uint32_t)(y1024 & 1023) >> 2;
    uint32_t w00 = ((256u - fx) * (256u - fy)) >> 8;
    uint32_t w10 = (fx * (256u - fy)) >> 8;
    uint32_t w01 = ((256u - fx) * fy) >> 8;
    uint32_t w11 = (fx * fy) >> 8;
    if (w00) ofx_px(bb, w, h, ix,     iy,     ofx_mul(col, w00));
    if (w10) ofx_px(bb, w, h, ix + 1, iy,     ofx_mul(col, w10));
    if (w01) ofx_px(bb, w, h, ix,     iy + 1, ofx_mul(col, w01));
    if (w11) ofx_px(bb, w, h, ix + 1, iy + 1, ofx_mul(col, w11));
}

static void ofx_draw_ribbons(volatile uint32_t *bb, int32_t w, int32_t h,
                             int32_t cx, int32_t cy, int32_t R, uint32_t gain)
{
    int32_t t0 = 90 * R / 216;
    for (int k = 0; k < OFX_RIB_N; ++k) {
        const struct ofx_rib_cfg *rc = &ofx_rib[k];
        int32_t base16 = (int32_t)rc->rad * R * 16 / 216;
        int32_t amp16  = (int32_t)rc->amp * R * 16 / 216;
        int32_t wid16  = (int32_t)rc->wid * R * 16 / 216;
        if (wid16 < 32) wid16 = 32;

        uint64_t ph = ofx_phf[k];
        uint32_t head16 = (uint32_t)(ph >> 24) & 16383u;
        uint32_t wph16  = (uint32_t)(ph >> 23) & 16383u;
        uint32_t tl16 = (uint32_t)((int32_t)rc->tilt * 16 +
                        ((ofx_sin16((uint32_t)(ph >> 26) & 16383u) * 480) >> 10)) & 16383u;
        int32_t ct = ofx_cos16(tl16), st = ofx_sin16(tl16);
        uint32_t rcol = her_mix256(rc->col, her_col_cur & 0x00FFFFFFu, 70u);
        const uint32_t lead16 = 384u;

        for (uint32_t a = 0; a < 1024u; a += OFX_RIB_STEP) {
            int32_t ca = her_cs(a), sa = her_sn(a);
            int32_t wave = ofx_sin16(((a * (uint32_t)rc->harm) << 4) + wph16);
            int32_t r0_16 = base16 + ((amp16 * wave) >> 10);
            int32_t pin = ofx_sin16((a << 5) + (wph16 >> 1));
            int32_t wv16 = 16 + ((wid16 * (1024 + pin)) >> 11);

            uint32_t tail16 = (head16 - (a << 4)) & 16383u;
            uint32_t inten;
            if (tail16 > 16384u - lead16) {            /* borde de entrada suave */
                uint32_t d = 16384u - tail16;
                inten = 28u + (228u * (lead16 - d)) / lead16;
            } else {
                inten = 28u + (228u * (16383u - tail16)) / 16383u;
            }
            uint32_t depth = 192u + (uint32_t)(((sa + 1024) * 64) >> 11);
            uint32_t kcol = (inten * depth) >> 8;
            kcol = (kcol * rc->alpha) >> 8;
            kcol = (kcol * gain) >> 8;
            if (kcol > 256u) kcol = 256u;
            uint32_t bc = ofx_mul(rcol, kcol);
            if (!bc) continue;

            int32_t smax = (wv16 >> 4) + 1;
            uint32_t den = (uint32_t)(wv16 + 16);
            for (int32_t s = -smax; s <= smax; ++s) {
                int32_t as = s < 0 ? -s : s;
                int32_t num = wv16 + 16 - as * 16;
                if (num <= 0) continue;
                uint32_t fq = ((uint32_t)num * 256u) / den;
                fq = (fq * fq) >> 8;
                int32_t rr16 = r0_16 + s * 16;
                int32_t lx = (rr16 * ca) >> 4;                       /* px*1024 */
                int32_t ly = (((rr16 * sa) >> 4) * (int32_t)rc->sq) >> 8;
                int32_t X = (lx * ct - ly * st) >> 10;
                int32_t Y = (lx * st + ly * ct) >> 10;
                int32_t ady = (Y < 0 ? -Y : Y) >> 10;
                uint32_t att = 256u;
                if (ady > t0) {
                    int32_t t = 256 - (ady - t0) * 3;
                    att = (t < 40) ? 40u : (uint32_t)t;
                }
                uint32_t add = ofx_mul(bc, (fq * att) >> 8);
                if (!add) continue;
                ofx_splat(bb, w, h, cx * 1024 + X, cy * 1024 + Y, ofx_dbl(add));
            }
        }
    }
}

/* ---------- pasada trasera: halo + cintas (antes de las particulas) ---------- */
static void ofx_orb_back(uint32_t ucx, uint32_t ucy)
{
    int32_t cx = (int32_t)ucx, cy = (int32_t)ucy;
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;

    ofx_frame_update(cx, cy);

    int32_t R = ofx_radius(h);
    uint32_t gain = 150u + (uint32_t)her_cur_glow + ofx_flash +
                    (uint32_t)her_pulse / 2u + (uint32_t)her_energy / 2u;
    if (gain > 600u) gain = 600u;
    {   /* PARCHE V2: ganancia suavizada, sin parpadeos bruscos */
        static int32_t gs = 0;
        if (gs == 0) gs = (int32_t)gain;
        gs += ((int32_t)gain - gs) / 3;
        gain = (uint32_t)gs;
    }

    fb_damage(cx - R * 3 / 2, cy - R * 3 / 2, R * 3, R * 3);
    ofx_build_halo(gain);
    ofx_draw_halo(bb, w, h, cx, cy, R);
    ofx_draw_ribbons(bb, w, h, cx, cy, R, gain);
}

/* ---------- pasada delantera: anillo fotonico, destello, ondas, expulsion ---------- */
static void ofx_orb_front(uint32_t ucx, uint32_t ucy)
{
    int32_t cx = (int32_t)ucx, cy = (int32_t)ucy;
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;

    int32_t R = ofx_radius(h);
    uint32_t fl = ofx_flash;

    /* anillo fotonico con efecto Doppler (un lado mas brillante, rotando) */
    {
        static const uint8_t wt[5] = { 90, 255, 190, 110, 50 };
        uint32_t head = (ofx_ph[0] >> 11) & 1023u;
        uint32_t pc = her_mix256(0x00FFFFFFu, her_col_cur & 0x00FFFFFFu, 90u);
        uint32_t ibase = 120u + fl / 2u + (uint32_t)her_pulse / 4u;
        for (uint32_t a = 0; a < 1024u; a += 2u) {
            int32_t c = her_cs(a), s = her_sn(a);
            uint32_t dop = 128u + (uint32_t)(her_cs((a - head) & 1023u) + 1024) / 16u;
            uint32_t k = (ibase * dop) >> 8;
            if (k > 256u) k = 256u;
            uint32_t c0 = ofx_mul(pc, k);
            for (int32_t rr = 29; rr <= 33; ++rr)
                ofx_px(bb, w, h, cx + ((rr * c) >> 10), cy + ((rr * s) >> 10),
                       ofx_mul(c0, wt[rr - 29]));
        }
    }

    /* destello anamorfico horizontal */
    {
        int32_t L = R * 13 / 10;
        uint32_t str = 70u + fl / 2u + (uint32_t)her_energy / 4u;
        uint32_t sc = her_mix256(0x007DD3FCu, her_col_cur & 0x00FFFFFFu, 100u);
        for (int32_t dx = 26; dx < L; ++dx) {
            uint32_t k = (uint32_t)(256 - (dx - 26) * 256 / (L - 26));
            k = (k * k) >> 8;
            k = (k * str) >> 8;
            if (k > 256u) k = 256u;
            uint32_t c1 = ofx_mul(sc, k);
            uint32_t c2 = ofx_mul(c1, 100u);
            ofx_px(bb, w, h, cx + dx, cy, c1);
            ofx_px(bb, w, h, cx - dx, cy, c1);
            ofx_px(bb, w, h, cx + dx, cy - 1, c2);
            ofx_px(bb, w, h, cx - dx, cy - 1, c2);
            ofx_px(bb, w, h, cx + dx, cy + 1, c2);
            ofx_px(bb, w, h, cx - dx, cy + 1, c2);
        }
    }

    int any = 0;

    /* ondas de choque */
    for (int i = 0; i < OFX_RING_MAX; ++i) {
        struct ofx_ring_t *g = &ofx_rings[i];
        if (!g->alive || g->maxlife <= 0) continue;
        any = 1;
        uint32_t f = (uint32_t)g->life * 256u / (uint32_t)g->maxlife;
        uint32_t c = ofx_mul(her_mix256(0x00FFFFFFu, g->col, 110u), (f * 3u) >> 2);
        ofx_draw_ring(bb, w, h, cx, cy, g->r, c);
    }

    /* particulas expulsadas con estela */
    for (int i = 0; i < OFX_EJ_MAX; ++i) {
        struct ofx_ejecta *e = &ofx_ej[i];
        if (!e->alive || e->maxlife <= 0) continue;
        any = 1;
        uint32_t f = (uint32_t)e->life * 256u / (uint32_t)e->maxlife;   /* 256 recien nacida */
        int32_t hx = e->x >> 8, hy = e->y >> 8;
        int32_t tx = (e->x - e->vx * 2) >> 8, ty = (e->y - e->vy * 2) >> 8;
        uint32_t hot = her_mix256(0x00FFFFFFu, e->col, 256u - f);
        uint32_t c_head = ofx_mul(hot, 128u + f / 2u);
        uint32_t c_tail = ofx_mul(e->col, f / 3u);
        ofx_line(bb, w, h, tx, ty, hx, hy, c_tail, c_head);
        if (f > 100u) {
            uint32_t g1 = ofx_mul(c_head, 90u);
            ofx_px(bb, w, h, hx + 1, hy, g1); ofx_px(bb, w, h, hx - 1, hy, g1);
            ofx_px(bb, w, h, hx, hy + 1, g1); ofx_px(bb, w, h, hx, hy - 1, g1);
        }
    }

    if (any) fb_damage(0, 0, w, h);
}
/* ================= fin SOMA_ORB_FX_PATCH (A) ================= */

static void render_desktop(void)
{
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    fb_clear(0x00000000);

    uint32_t orb_cx = cur_w / 2;
    uint32_t orb_cy = cur_h / 2;

    ofx_orb_back(orb_cx, orb_cy);
    her_parts_update_draw();
    her_draw_orb(orb_cx, orb_cy, her_angle);
    ofx_orb_front(orb_cx, orb_cy);
    her_draw_user_zone(cur_w, cur_h, orb_cy);
    her_draw_agent_zone(cur_w, cur_h, orb_cy);
    her_draw_dock(cur_w, cur_h);

    if (current_app != APP_DESKTOP)
        her_draw_window_chrome(cur_w, cur_h);
    her_draw_vortex_particles();
}

static void agents_render_window(uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int cursor_visible)
{
    (void)cursor_visible;
    if (ww < 180u || wh < 160u) return;
    fb_fill_rect(wx, wy, ww, wh, 0x000B1220);
    fb_fill_rounded_rect((int32_t)(wx + 12u), (int32_t)(wy + 10u), (int32_t)(ww - 24u), 30, 5, 0x00131E2A);
    font_draw_string(wx + 24u, wy + 20u, "AGENTES DISPONIBLES", 0x00F8FAFC, 1);
    font_draw_string(wx + 24u, wy + 48u, "Jerarquia de coordinacion de SOMA", 0x0064778A, 1);

    uint32_t root_x = wx + 24u, root_y = wy + 68u;
    uint32_t card_x = wx + 54u;
    uint32_t card_w = ww > 86u ? ww - 78u : ww - 62u;
    fb_fill_rounded_rect((int32_t)root_x, (int32_t)root_y, (int32_t)(ww - 48u), 48, 6, 0x00113343);
    fb_draw_rounded_rect((int32_t)root_x, (int32_t)root_y, (int32_t)(ww - 48u), 48, 6, COLOR_CYAN);
    font_draw_string(root_x + 12u, root_y + 10u, "SOMA  -  ORQUESTADOR PRINCIPAL", COLOR_CYAN, 1);
    font_draw_string(root_x + 12u, root_y + 28u, "Coordina especialistas y deriva operaciones de sistema.", 0x00CBD5E1, 1);

    uint32_t branch_x = wx + 38u;
    fb_fill_rect(branch_x, root_y + 48u, 2u, 230u, 0x00334155);

    uint32_t y1 = wy + 132u, y2 = wy + 220u, y3 = wy + 308u;
    uint32_t ys[3] = { y1, y2, y3 };
    const char *names[3] = { "Mayordomo  -  Operaciones del sistema", "Buscador  -  Web e investigacion", "Maquetador  -  Documentos y composicion" };
    const char *descs[3] = {
        "Ejecuta rapido RAM, disco, VFS, busquedas y gestion de archivos.",
        "Investiga informacion web, actualidad y consultas de conocimiento.",
        "Organiza contenido en documentos estructurados con layout SOMA."
    };
    const uint32_t colors[3] = { 0x0034D399, 0x00F59E0B, 0x000EA5E9 };
    for (int i = 0; i < 3; ++i) {
        fb_fill_rect(branch_x, ys[i] + 26u, 16u, 2u, 0x00334155);
        fb_fill_rounded_rect((int32_t)card_x, (int32_t)ys[i], (int32_t)card_w, 66, 6, 0x00101827);
        fb_draw_rounded_rect((int32_t)card_x, (int32_t)ys[i], (int32_t)card_w, 66, 6, colors[i]);
        font_draw_string(card_x + 12u, ys[i] + 12u, names[i], colors[i], 1);
        font_draw_string(card_x + 12u, ys[i] + 34u, descs[i], 0x00CBD5E1, 1);
    }
    font_draw_string(wx + 22u, wy + wh - 20u, "Tareas comunes: RAM | Disco | Buscar archivo | Crear carpeta", 0x0064778A, 1);
}

static void her_render_app_body(enum app_mode app, uint32_t wx, uint32_t wy, uint32_t ww, uint32_t wh, int is_focused)
{
    fb_set_clip((int32_t)wx, (int32_t)wy, (int32_t)ww, (int32_t)wh);
    switch (app) {
    case APP_TERMINAL_SHELL:
        render_real_terminal(wx, wy, ww, wh, cur_cmd, cur_cmd_len, cur_cursor_pos, is_focused);
        break;
    case APP_AGENT_CHAT:
        render_log_window_chat(agent_log, agent_log_count, wx, wy, ww,
                               wh > 56 ? wh - 56 : wh);
        render_input_bar_chat("soma> ", COLOR_CYAN, wx, wy + wh - 46, ww, 46,
                              agent_global_cmd, agent_global_len,
                              agent_global_cursor, is_focused && agent_global_focus);
        break;
    case APP_WEB_BROWSER:   browser_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_IMAGE_VIEWER:  imgview_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_FILE_MANAGER:  fm_render_window(wx, wy, ww, wh, is_focused);      break;
    case APP_NOTEPAD:       notepad_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_AGENT_MEMORY:  memview_render_window(wx, wy, ww, wh, is_focused); break;
    case APP_LAYOUT:        layout_render_window(wx, wy, ww, wh, is_focused);  break;
    case APP_PAINT:         paint_render_window(wx, wy, ww, wh, is_focused);   break;
    case APP_AGENTS:        agents_render_window(wx, wy, ww, wh, is_focused);  break;
    default: break;
    }
    fb_reset_clip();
}

static void her_refresh_all(void)
{
    render_desktop();
    if (her_num_windows == 0) return;

    for (int i = 0; i < her_num_windows; ++i) {
        struct her_win_node *win = &her_windows[i];
        her_draw_one_window_chrome(win, (i == her_num_windows - 1));

        uint32_t wx = win->x;
        uint32_t wy = win->y + HER_TITLE_H;
        uint32_t ww = win->w;
        uint32_t wh = (win->h > HER_TITLE_H) ? win->h - HER_TITLE_H : 1;

        her_render_app_body(win->app, wx, wy, ww, wh, (i == her_num_windows - 1));
    }
}

static void her_open_app(enum app_mode app, uint32_t cur_w, uint32_t cur_h)
{
    int existing = her_find_window(app);
    if (existing >= 0) {
        her_bring_to_front(existing);
        agent_global_focus = (app == APP_AGENT_CHAT) ? 1 : 0;
        return;
    }

    if (her_num_windows >= HER_MAX_WINDOWS) {
        for (int i = 0; i < HER_MAX_WINDOWS - 1; ++i) her_windows[i] = her_windows[i + 1];
        her_num_windows = HER_MAX_WINDOWS - 1;
    }

    int idx = her_num_windows++;
    her_windows[idx].app = app;
    her_windows[idx].dragging = 0;
    her_windows[idx].resizing = 0;

    her_fit_window_to_orbit(cur_w, cur_h);
    her_windows[idx].w = her_win_w;
    her_windows[idx].h = her_win_h;
    {   /* SOMA_PARCHE_V2_GUI: tamano minimo por aplicacion al abrir */
        uint32_t mw = her_app_min_w(app), mh = HER_MIN_H + 60u;
        if (cur_w > 64u && mw > cur_w - 32u) mw = cur_w - 32u;
        if (her_windows[idx].w < mw) her_windows[idx].w = mw;
        if (cur_h > 96u && mh <= cur_h - 48u && her_windows[idx].h < mh) her_windows[idx].h = mh;
    }

    uint32_t cascade_off = (uint32_t)(idx * 28);
    int corner = (int)app;
    uint32_t base_x = ((corner & 1) == 0 || cur_w <= her_windows[idx].w + 32u) ? 20u : (cur_w - her_windows[idx].w - 20u);
    uint32_t base_y = ((corner & 2) == 0 || cur_h <= her_windows[idx].h + 48u) ? 36u : (cur_h - her_windows[idx].h - 20u);

    if ((corner & 1) == 0) base_x += cascade_off; else base_x = (base_x > cascade_off) ? base_x - cascade_off : 10u;
    if ((corner & 2) == 0) base_y += cascade_off; else base_y = (base_y > cascade_off) ? base_y - cascade_off : 30u;

    if (base_x + her_windows[idx].w > cur_w) base_x = cur_w - her_windows[idx].w - 10u;
    if (base_y + her_windows[idx].h > cur_h) base_y = cur_h - her_windows[idx].h - 10u;

    her_windows[idx].x = base_x;
    her_windows[idx].y = base_y;

    her_win_x = her_windows[idx].x;
    her_win_y = her_windows[idx].y;
    her_win_w = her_windows[idx].w;
    her_win_h = her_windows[idx].h;

    her_active_win_idx = idx;
    current_app = app;
    agent_global_focus = (app == APP_AGENT_CHAT) ? 1 : 0;

    switch (app) {
    case APP_WEB_BROWSER:  browser_init();  break;
    case APP_FILE_MANAGER: fm_init();       break;
    case APP_IMAGE_VIEWER: imgview_init();  break;
    case APP_NOTEPAD:      notepad_init();  break;
    case APP_AGENT_MEMORY: memview_init();  break;
    case APP_LAYOUT:       layout_init();   break;
    case APP_PAINT:        paint_init();    break;
    default: break;
    }
}

/* ====================================================================
 * SOMA ENHANCED VORTEX & ACCRETION DISK ENGINE (Fixed-Point 16.8)
 * ==================================================================== */
#define HER_VORTEX_MAX 512

struct her_vortex_p {
    int32_t  x, y;           /* Posición fija 16.8 */
    int32_t  vx, vy;         /* Velocidad fija 16.8 */
    int32_t  prev_x, prev_y; /* Estela luminosa */
    uint32_t color;
    uint32_t edge_color;
    uint8_t  absorbed;
    uint8_t  size;
    int16_t  w_size, h_size;
};

static struct her_vortex_p her_vortex_parts[HER_VORTEX_MAX];

/* static int her_vortex_closing = 0; (declarada arriba) */
static int her_vortex_frame = 0;
static int her_vortex_total_absorbed = 0;
/* static int her_dock_pending_open = 0; (declarada arriba) */
/* static int soma_text_vortex = 0; (declarada arriba) */

static void her_close_target_window_vortex(int win_idx)
{
    if (win_idx < 0 || win_idx >= her_num_windows) return;
    struct her_win_node closing_win = her_windows[win_idx];
    enum app_mode closing_app = closing_win.app;

    for (int i = win_idx; i < her_num_windows - 1; ++i) {
        her_windows[i] = her_windows[i + 1];
    }
    her_num_windows--;

    if (her_num_windows > 0) {
        her_active_win_idx = her_num_windows - 1;
        current_app = her_windows[her_active_win_idx].app;
        her_win_x = her_windows[her_active_win_idx].x;
        her_win_y = her_windows[her_active_win_idx].y;
        her_win_w = her_windows[her_active_win_idx].w;
        her_win_h = her_windows[her_active_win_idx].h;
        agent_global_focus = (current_app == APP_AGENT_CHAT) ? 1 : 0;
    } else {
        her_active_win_idx = -1;
        current_app = APP_DESKTOP;
        agent_global_focus = 1;
    }

    soma_text_vortex = 0;
    her_dock_pending_open = 0;

    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (cur_w == 0 || cur_h == 0) {
        if (!her_cursor_should_show()) mouse_erase_cursor();
        return;
    }

    uint32_t app_col = 0x0038BDF8u;
    switch (closing_app) {
    case APP_TERMINAL_SHELL: app_col = 0x0010B981u; break;
    case APP_AGENT_CHAT:     app_col = 0x0038BDF8u; break;
    case APP_WEB_BROWSER:    app_col = 0x00F59E0Bu; break;
    case APP_FILE_MANAGER:   app_col = 0x0038BDF8u; break;
    case APP_IMAGE_VIEWER:   app_col = 0x0010B981u; break;
    case APP_NOTEPAD:        app_col = 0x00F472B6u; break;
    case APP_AGENT_MEMORY:   app_col = 0x00A855F7u; break;
    case APP_LAYOUT:         app_col = 0x000EA5E9u; break;
    case APP_AGENTS:         app_col = 0x0034D399u; break;
    case APP_PAINT:          app_col = 0x00F59E0Bu; break;
    default:                 app_col = 0x007DD3FCu; break;
    }

    int32_t wx = (int32_t)closing_win.x;
    int32_t wy = (int32_t)closing_win.y;
    int32_t ww = (int32_t)closing_win.w;
    int32_t wh = (int32_t)closing_win.h;
    if (ww < 100) ww = 400;
    if (wh < 100) wh = 300;
    int32_t wcx = wx + ww / 2;
    int32_t wcy = wy + wh / 2;

    int32_t tile_w = ww / 16;
    int32_t tile_h = wh / 16;
    if (tile_w < 4) tile_w = 4;
    if (tile_h < 4) tile_h = 4;

    volatile uint32_t *target_bb = fb_get_buffer();
    int p_idx = 0;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            if (p_idx >= HER_VORTEX_MAX) break;
            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            int32_t orig_x = wx + c * tile_w;
            int32_t orig_y = wy + r * tile_h;
            if (orig_x < 0) orig_x = 0;
            if (orig_x >= (int32_t)cur_w) orig_x = (int32_t)cur_w - 1;
            if (orig_y < 0) orig_y = 0;
            if (orig_y >= (int32_t)cur_h) orig_y = (int32_t)cur_h - 1;

            p->x = orig_x << 8;
            p->y = orig_y << 8;
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = (int16_t)tile_w;
            p->h_size = (int16_t)tile_h;
            p->size = ((p_idx & 7) == 0) ? 2 : 1;

            uint32_t samp_col = 0;
            if (target_bb) {
                int32_t mid_x = orig_x + tile_w / 2;
                int32_t mid_y = orig_y + tile_h / 2;
                if (mid_x >= (int32_t)cur_w) mid_x = (int32_t)cur_w - 1;
                if (mid_y >= (int32_t)cur_h) mid_y = (int32_t)cur_h - 1;
                samp_col = target_bb[mid_y * cur_w + mid_x];
            }
            int is_border = (r == 0 || r == 15 || c == 0 || c == 15);
            if ((samp_col & 0x00FFFFFFu) < 0x00151515u || is_border)
                p->color = her_mix256(app_col, 0x00FFFFFFu, is_border ? 60u : 0u);
            else
                p->color = her_mix256(samp_col, app_col, 80u);
            p->edge_color = her_mix256(app_col, 0x00FFFFFFu, 180u);

            int32_t ox = (orig_x + tile_w / 2) - wcx;
            int32_t oy = (orig_y + tile_h / 2) - wcy;
            int32_t rnd_x = (int32_t)(her_rand() & 15u) - 7;
            int32_t rnd_y = (int32_t)((her_rand() >> 4) & 15u) - 7;
            p->vx = (ox * 70) / (ww > 0 ? ww : 1) + rnd_x * 16;
            p->vy = (oy * 70) / (wh > 0 ? wh : 1) + rnd_y * 16;
        }
    }

    her_dragging = 0;
    her_resizing = 0;
    her_vortex_total_absorbed = 0;
    her_vortex_frame = 0;
    her_vortex_closing = 1;
    soma_vortex_boost = 10;
    if (!her_cursor_should_show()) mouse_erase_cursor();
}

/* Enrutador canónico para cerrar la ventana activa desde cualquier app */
static void her_close_current_app_vortex(void)
{
    if (her_num_windows <= 0) return;
    her_close_target_window_vortex(her_num_windows - 1);
}

/* Integración física en el disco de acreción hasta el horizonte de sucesos */
static void her_vortex_close_step(void)
{
    if (!her_vortex_closing) return;
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    uint32_t cx = cur_w / 2u, cy = cur_h / 2u;
    if (cur_w == 0u || cur_h == 0u) {
        her_vortex_closing = 0;
        return;
    }

    her_vortex_frame++;
    int active_in_flight = 0;
    int frame = her_vortex_frame;
    uint32_t horizon_r = 27u; /* Radio del horizonte de sucesos */

    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t cur_px = p->x >> 8;
        int32_t cur_py = p->y >> 8;
        int32_t dx = (int32_t)cx - cur_px;
        int32_t dy = (int32_t)cy - cur_py;
        uint32_t dist = her_isqrt((uint32_t)(dx * dx + dy * dy));

        /* Absorción al tocar o penetrar el horizonte de sucesos */
        if (dist <= horizon_r || dist == 0u) {
            p->x = (int32_t)(cx << 8);
            p->y = (int32_t)(cy << 8);
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 1;
            her_vortex_total_absorbed++;
            ofx_on_absorb(p->color); /* SOMA_ORB_FX_PATCH */
            /* La particula desaparece al tocar el horizonte: sin energia,
             * sin pulso, sin cambio de color y sin reinyeccion en la rosquilla. */
            continue;
        }
        active_in_flight++;
        p->prev_x = p->x;
        p->prev_y = p->y;

        int max_burst = soma_text_vortex ? 4 : 2;
        if (frame < max_burst) {
            /* Desprendimiento de aplicaciones reducido a solo 2 frames */
            p->x += p->vx;
            p->y += p->vy;
            p->vx = (p->vx * 192) >> 8;
            p->vy = (p->vy * 192) >> 8;
            int32_t shrink_w = (int32_t)p->w_size * (max_burst - frame) / max_burst;
            int32_t shrink_h = (int32_t)p->h_size * (max_burst - frame) / max_burst;
            if (shrink_w < 1) shrink_w = 1;
            if (shrink_h < 1) shrink_h = 1;
            p->w_size = (int16_t)shrink_w;
            p->h_size = (int16_t)shrink_h;
        } else {
            /* Fase 2: Integración al doble de velocidad en disco y horizonte */
            /* SOMA FAST ABSORB v1 */
            int32_t pull_spd, swirl_spd;
            if (soma_text_vortex) {
                /* Arrastre de letras e iconos */
                if (dist > 180u)      { pull_spd = 36; swirl_spd = 12; }
                else if (dist > 90u)  { pull_spd = 28; swirl_spd = 18; }
                else if (dist > 45u)  { pull_spd = 18; swirl_spd = 24; }
                else                  { pull_spd = 12; swirl_spd = 28; }
            } else {
                /* Aplicaciones: el doble de rapido (absorcion a la mitad de tiempo) */
                if (dist > 300u)      { pull_spd = 96; swirl_spd = 20; }
                else if (dist > 160u) { pull_spd = 72; swirl_spd = 24; }
                else if (dist > 85u)  { pull_spd = 56; swirl_spd = 26; }
                else if (dist > 45u)  { pull_spd = 32; swirl_spd = 18; }
                else                  { pull_spd = 22; swirl_spd = 10; }
            }

            /* El giro tangencial aleja la particula del centro (~s^2 / 2d). Se
             * compensa en el empuje radial para que SIEMPRE se acerque al horizonte
             * y no se quede orbitando alrededor del agujero negro. */
            int32_t d32 = (int32_t)dist;
            if (swirl_spd > d32 / 3) swirl_spd = d32 / 3;
            pull_spd += (swirl_spd * swirl_spd) / (2 * d32);
            if (pull_spd > d32 - 20) pull_spd = d32 - 20;
            if (pull_spd < 4) pull_spd = 4;

            int32_t step_x = (int32_t)(((int64_t)dx * pull_spd +
                                         (int64_t)dy * swirl_spd) * 256 / (int32_t)dist);
            int32_t step_y = (int32_t)(((int64_t)dy * pull_spd -
                                         (int64_t)dx * swirl_spd) * 256 / (int32_t)dist);
            p->vx = step_x;
            p->vy = step_y;
            p->x += p->vx;
            p->y += p->vy;
        }
    }

    soma_vortex_boost = (active_in_flight > 0) ? (soma_text_vortex ? 12 : 8) : 0;
    if (active_in_flight == 0) {
        her_vortex_closing = 0;
        soma_vortex_boost = 0;
        her_energy = 0u;
        her_pulse = 0u;
        /* Al ser absorbida la última partícula de texto, surgen los iconos del dock */
        if (her_dock_pending_open) {
            her_dock_pending_open = 0;
            soma_text_vortex = 0;
            her_icons_visible = 1;
            her_icons_anim_phase = 0;
            her_icons_burst_opening = 1;
            her_icons_burst_at = timer_get_uptime_ms();
        }
    }
}

/* ================= SOMA_ORB_FX_PATCH (B): estelas cometarias ================= */
static void ofx_vortex_trails(void)
{
    int32_t w = (int32_t)fb_get_width(), h = (int32_t)fb_get_height();
    volatile uint32_t *bb = fb_get_buffer();
    if (!bb || w <= 0 || h <= 0) return;
    int32_t cx = w / 2, cy = h / 2;

    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t x = p->x >> 8, y = p->y >> 8;
        if (x < -64 || y < -64 || x > w + 64 || y > h + 64) continue;

        int32_t dx = cx - x, dy = cy - y;
        uint32_t dist = her_isqrt((uint32_t)(dx * dx + dy * dy));
        /* 0 lejos ... 256 pegada al horizonte: se vuelve blanca y se estira */
        uint32_t hot = (dist >= 220u) ? 0u : (220u - dist) * 256u / 220u;
        uint32_t base = p->color & 0x00FFFFFFu;
        uint32_t c_head = ofx_mul(her_mix256(base, 0x00FFFFFFu, hot), 215u);
        uint32_t c_tail = ofx_mul(base, 34u);

        int32_t stretch = 200 + (int32_t)hot;                  /* x0.8 .. x1.8 */
        int32_t tx = (p->x - ((p->vx * stretch) >> 8)) >> 8;
        int32_t ty = (p->y - ((p->vy * stretch) >> 8)) >> 8;
        ofx_line(bb, w, h, tx, ty, x, y, c_tail, c_head);

        uint32_t g = ofx_mul(c_head, 80u);
        ofx_px(bb, w, h, x + 1, y, g); ofx_px(bb, w, h, x - 1, y, g);
        ofx_px(bb, w, h, x, y + 1, g); ofx_px(bb, w, h, x, y - 1, g);
    }
}
/* ================= fin SOMA_ORB_FX_PATCH (B) ================= */

static void her_draw_vortex_particles(void)
{
    if (!her_vortex_closing) return;
    ofx_vortex_trails();
    uint32_t w = fb_get_width(), h = fb_get_height();
    for (int i = 0; i < HER_VORTEX_MAX; ++i) {
        struct her_vortex_p *p = &her_vortex_parts[i];
        if (p->absorbed) continue;
        int32_t x = p->x >> 8, y = p->y >> 8;
        int32_t px = p->prev_x >> 8, py = p->prev_y >> 8;
        if (x < 0 || y < 0 || x >= (int32_t)w || y >= (int32_t)h) continue;
        if (px >= 0 && py >= 0 && px < (int32_t)w && py < (int32_t)h &&
            (px != x || py != y))
            fb_draw_line(px, py, x, y, her_scale(p->color, 150u));
        if (her_vortex_frame < (soma_text_vortex ? 4 : 2) && p->w_size > 2 && p->h_size > 2) {
            int32_t dw = p->w_size, dh = p->h_size;
            if (x + dw > (int32_t)w) dw = (int32_t)w - x;
            if (y + dh > (int32_t)h) dh = (int32_t)h - y;
            if (dw > 2 && dh > 2) {
                fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)dw,
                             (uint32_t)dh, p->color);
                fb_draw_rect((uint32_t)x, (uint32_t)y, (uint32_t)dw,
                             (uint32_t)dh, p->edge_color);
            } else {
                fb_fill_circle(x, y, 2, p->color);
            }
            continue;
        }
        if (p->size > 1) {
            fb_fill_circle(x, y, (int32_t)p->size, p->color);
            fb_draw_pixel((uint32_t)x, (uint32_t)y, 0x00FFFFFFu);
        } else {
            fb_draw_pixel((uint32_t)x, (uint32_t)y, p->color);
        }
    }
}

/* Destrucción integral en partículas de los diálogos de usuario y LLM */
static void her_start_text_destruction(uint32_t cur_w, uint32_t cur_h)
{
    her_text_suppressed_by_dock = 1;
    if (cur_w == 0u || cur_h == 0u) {
        her_last_q[0] = '\0';
        her_ans_len = her_ans_reveal = 0;
        her_thk_len = her_thk_reveal = 0;
        agent_global_len = agent_global_cursor = 0;
        agent_global_cmd[0] = '\0';
        her_energy = 0;
        her_dock_pending_open = 1;
        return;
    }

    volatile uint32_t *fb = fb_get_buffer();
    if (!fb) {
        her_last_q[0] = '\0';
        her_ans_len = her_ans_reveal = 0;
        her_thk_len = her_thk_reveal = 0;
        agent_global_len = agent_global_cursor = 0;
        agent_global_cmd[0] = '\0';
        her_energy = 0;
        her_dock_pending_open = 1;
        return;
    }

    uint32_t cx = cur_w / 2u, cy = cur_h / 2u;
    uint32_t top0 = (cy > 170u) ? cy - 170u : 12u;
    uint32_t top1 = (cy > 170u) ? cy - 105u : 64u;
    uint32_t bottom0 = cy + 105u;
    uint32_t bottom1 = (cur_h > 35u) ? cur_h - 35u : cur_h;
    if (top0 > cur_h) top0 = cur_h;
    if (top1 > cur_h) top1 = cur_h;
    if (bottom0 > cur_h) bottom0 = cur_h;
    if (bottom1 > cur_h) bottom1 = cur_h;

    int p_idx = 0;

    /* Captura completa de todos los píxeles iluminados de las letras en pantalla */
    for (uint32_t y = 0; y < cur_h && p_idx < HER_VORTEX_MAX; ++y) {
        if (!((y >= top0 && y < top1) || (y >= bottom0 && y < bottom1))) continue;
        uint32_t row = y * cur_w;
        for (uint32_t x = 30; x < cur_w - 30 && p_idx < HER_VORTEX_MAX; x += 2) {
            uint32_t sample = fb[row + x] & 0x00FFFFFFu;
            uint32_t r = (sample >> 16) & 0xFFu;
            uint32_t g = (sample >> 8)  & 0xFFu;
            uint32_t b = sample & 0xFFu;
            if (r + g + b < 40u) continue; /* Fondo cósmico oscuro */

            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            p->x = (int32_t)(x << 8);
            p->y = (int32_t)(y << 8);
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = 3;
            p->h_size = 3;
            p->size = ((p_idx & 3) == 0) ? 2 : 1;
            p->color = sample;
            p->edge_color = her_mix256(sample, 0x00FFFFFFu, 180u);

            /* Impulso de fragmentación */
            int32_t ox = (int32_t)x - (int32_t)cx;
            int32_t oy = (int32_t)y - (int32_t)cy;
            int32_t rnd_x = (int32_t)(her_rand() & 31u) - 15;
            int32_t rnd_y = (int32_t)((her_rand() >> 5) & 31u) - 15;
            p->vx = (ox * 60) / (int32_t)cur_w + rnd_x * 12;
            p->vy = (oy * 60) / (int32_t)cur_h + rnd_y * 12;
        }
    }

    /* Limpieza total de los textos para dar paso exclusivamente a las partículas */
    her_last_q[0] = '\0';
    her_ans_len = 0;
    her_ans_reveal = 0;
    her_thk_len = 0;
    her_thk_reveal = 0;
    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';
    her_energy = 0;

    her_vortex_frame = 0;
    her_vortex_total_absorbed = 0;

    if (p_idx > 0) {
        her_vortex_closing = 1;
        soma_vortex_boost = 14;
        soma_text_vortex = 1;
        her_dock_pending_open = 1;
    } else {
        her_vortex_closing = 0;
        soma_text_vortex = 0;
        her_dock_pending_open = 0;
        her_icons_visible = 1;
        her_icons_burst_opening = 1;
        her_icons_burst_at = timer_get_uptime_ms();
    }
}


/* Absorción de los iconos orbitales por el disco de acreción del agujero negro */
static void her_close_icons_vortex(uint32_t cur_w, uint32_t cur_h)
{
    if (cur_w == 0u || cur_h == 0u) {
        her_icons_visible = 0;
        her_icons_anim_phase = 0;
        her_dock_pending_open = 0;
        her_text_suppressed_by_dock = 0;
        agent_global_focus = 1;
        return;
    }

    uint64_t now_ms = timer_get_uptime_ms();
    int p_idx = 0;

    for (int i = 0; i < HER_DOCK_ITEM_COUNT; ++i) {
        int32_t target_x, target_y;
        her_icon_orbit_target(i, cur_w, cur_h, now_ms, &target_x, &target_y);
        uint32_t col = her_dock_items[i].color;

        /* Generar 20 partículas por cada icono orbital */
        for (int k = 0; k < 20 && p_idx < HER_VORTEX_MAX; ++k) {
            struct her_vortex_p *p = &her_vortex_parts[p_idx++];
            int32_t ox = (int32_t)(her_rand() % 56u) - 28;
            int32_t oy = (int32_t)(her_rand() % 56u) - 28;
            int32_t px = target_x + ox;
            int32_t py = target_y + oy;
            if (px < 0) px = 0;
            if (px >= (int32_t)cur_w) px = (int32_t)cur_w - 1;
            if (py < 0) py = 0;
            if (py >= (int32_t)cur_h) py = (int32_t)cur_h - 1;

            p->x = px << 8;
            p->y = py << 8;
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->absorbed = 0;
            p->w_size = 4;
            p->h_size = 4;
            p->size = ((k & 3) == 0) ? 2 : 1;
            p->color = (k & 1) ? col : 0x00FFFFFFu;
            p->edge_color = her_mix256(col, 0x00FFFFFFu, 180u);

            int32_t rnd_x = (int32_t)(her_rand() & 15u) - 7;
            int32_t rnd_y = (int32_t)((her_rand() >> 4) & 15u) - 7;
            p->vx = rnd_x * 16;
            p->vy = rnd_y * 16;
        }
    }

    /* Ocultar iconos de inmediato y habilitar entrada al agente */
    her_icons_visible = 0;
    her_icons_anim_phase = 0;
    her_icons_burst_at = 0;
    her_dock_pending_open = 0;
    her_text_suppressed_by_dock = 0;
    agent_global_focus = 1;
    agent_global_len = 0;
    agent_global_cursor = 0;
    agent_global_cmd[0] = '\0';

    her_vortex_frame = 0;
    her_vortex_total_absorbed = 0;
    her_vortex_closing = (p_idx > 0) ? 1 : 0;
    soma_text_vortex = 1; /* Arrastre dinámico hacia el disco de acreción */
    soma_vortex_boost = 14;
    mouse_erase_cursor();
}

static uint32_t g_live_win_x = 0, g_live_win_y = 0, g_live_win_w = 0, g_live_win_h = 0;
static int g_live_ready = 0;
static int g_live_busy = 0;

static int live_has(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return 0;
    for (; *hay; ++hay) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

static int live_path_allowed(const char *name)
{
    static const char *const deny[] = {
        "/sys/", "/agent/", "/proc/", "/dev/", "_stm.txt", "/etc/history.txt", 0
    };
    for (int i = 0; deny[i]; ++i) {
        if (live_has(name, deny[i])) return 0;
    }
    return 1;
}

static int live_data_is_text(const char *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)d[i];
        if (c < 9 || (c > 13 && c < 32)) return 0;
    }
    return 1;
}

/* ====================================================================
 * SOMA_PARCHE_V2_GUI: hilo de fondo (animacion + raton) y hook en vivo
 * Las herramientas del agente ya NO detienen la animacion.
 * ==================================================================== */
static int32_t g_mouse_x_s = 0, g_mouse_y_s = 0;

static void gui_live_paint(void)
{
    if (!fb_is_active()) return;
    if (her_anim_alive) return;      /* el hilo de fondo repinta a 30 FPS */
    her_refresh_all();
    fb_present();
}

static void gui_live_file_hook(const char *name, const char *data, uint32_t len)
{
    if (!g_live_ready || g_live_busy || !name || !name[0]) return;
    if (!fb_is_active()) return;

    struct tcb *self = thread_current();
    if (!self || self->tid != 0 || !self->agent_mode) return;

    if (!live_path_allowed(name)) return;
    if (!data) { data = ""; len = 0; }
    if (!live_data_is_text(data, len)) return;

    g_live_busy = 1;
    uint32_t cw = fb_get_width(), ch = fb_get_height();

    if (live_has(data, "#SOMA-LAYOUT") || live_has(name, "_layout.txt") ||
        live_has(name, "layout.txt")) {
        her_open_app(APP_LAYOUT, cw, ch);
        layout_set_source(data, len);
        gui_live_paint();
        g_live_busy = 0;
        return;
    }

    her_open_app(APP_NOTEPAD, cw, ch);
    notepad_live_begin(name, data, len);
    gui_live_paint();

    uint32_t chunk = 4u + len / 80u;
    if (chunk > 200u) chunk = 200u;

    while (notepad_live_step(chunk) > 0) {
        gui_live_paint();
        thread_sleep(20);
    }

    notepad_live_end();
    gui_live_paint();
    g_live_busy = 0;
}

static int gui_bg_mouse_poll(void)
{
    uint32_t w = fb_get_width(), h = fb_get_height();
    int moved = 0;
    if (w == 0u || h == 0u) return 0;
    for (int i = 0; i < 8; ++i) {
        int32_t dx = 0, dy = 0; int clk = 0;
        if (!mouse_poll(&dx, &dy, &clk)) break;
        if (dx || dy) { g_mouse_x_s += dx; g_mouse_y_s -= dy; moved = 1; }
    }
    if (g_mouse_x_s < 0) g_mouse_x_s = 0;
    if (g_mouse_x_s >= (int32_t)w) g_mouse_x_s = (int32_t)w - 1;
    if (g_mouse_y_s < 0) g_mouse_y_s = 0;
    if (g_mouse_y_s >= (int32_t)h) g_mouse_y_s = (int32_t)h - 1;
    return moved;
}

static void gui_bg_cursor(void)
{
    if (her_cursor_should_show()) mouse_draw_cursor(g_mouse_x_s, g_mouse_y_s);
    else mouse_erase_cursor();
}

static void her_anim_worker(void *arg)
{
    (void)arg;
    uint64_t last = 0;
    while (her_agent_busy) {
        if (fb_is_active()) {
            int moved = gui_bg_mouse_poll();
            uint64_t now = timer_get_uptime_ms();
            if (now - last >= 33u) {
                last = now;
                uint32_t w = fb_get_width(), h = fb_get_height();
                her_angle = (her_angle + 3) & 255;
                if (her_icons_visible) {
                    if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
                        her_icons_anim_phase += 36u;
                        if (her_icons_anim_phase > HER_ICON_ANIM_MAX)
                            her_icons_anim_phase = HER_ICON_ANIM_MAX;
                    }
                } else if (her_icons_anim_phase > 0u) {
                    her_icons_anim_phase = (her_icons_anim_phase > 36u)
                                         ? her_icons_anim_phase - 36u : 0u;
                }
                her_reveal_step();
                her_parts_spawn(w / 2, h / 2, her_angle);
                her_vortex_close_step();
                her_refresh_all();
                gui_bg_cursor();
                fb_present();
            } else if (moved) {
                gui_bg_cursor();
                fb_present();
            }
        }
        thread_sleep(4);
    }
    her_anim_alive = 0;
}

static void her_anim_start(void)
{
    her_agent_busy = 1;
    if (her_anim_alive) return;
    her_anim_alive = 1;
    if (!thread_create("her_anim", her_anim_worker, 0)) {
        her_anim_alive = 0;
        her_agent_busy = 0;
    }
}

/* Parada real: solo la llama quien termina la mision del agente */
static void her_anim_stop(void)
{
    her_agent_busy = 0;
    for (int i = 0; i < 400 && her_anim_alive; ++i)
        thread_sleep(5);
}

/* Obsoleta: antes las herramientas paraban la animacion y congelaban la GUI */
static void her_anim_halt(void)
{
}

static void ensure_window_geom(void)
{
    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();
    if (cur_w == 0 || cur_h == 0) return;
    her_place_window_corner(cur_w, cur_h, 0);
    g_live_win_x = her_win_x;
    g_live_win_y = her_win_y + HER_TITLE_H;
    g_live_win_w = her_win_w;
    g_live_win_h = (her_win_h > HER_TITLE_H) ? her_win_h - HER_TITLE_H : 1;

}

/* SOMA_PARCHE_V2_GUI: las herramientas abren ventanas reales del sistema multiventana */
static int gui_win_open(enum app_mode app)
{
    if (!fb_is_active()) return 0;
    her_open_app(app, fb_get_width(), fb_get_height());
    gui_live_paint();
    return 1;
}

void gui_open_browser(const char *url)
{
    if (!url || !*url) return;
    char local_url[160];
    if (url[0] != '/' && memcmp(url, "file://", 7) != 0 && memcmp(url, "http://", 7) != 0 && memcmp(url, "https://", 8) != 0) {
        local_url[0] = '/';
        uint32_t p = 0;
        while (url[p] && p < sizeof(local_url) - 2) {
            local_url[p + 1] = url[p]; p++;
        }
        local_url[p + 1] = '\0'; url = local_url;
    }
    if (gui_win_open(APP_WEB_BROWSER)) {
        /* browser_init() lanza la demo en segundo plano: esperar antes de cargar la URL */
        for (int i = 0; i < 300 && browser_is_loading(); ++i) thread_sleep(10);
        browser_load_url(url);
        for (int i = 0; i < 30 && browser_is_loading(); ++i) thread_sleep(10);
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_WEB_BROWSER;
    agent_global_focus = 0;
    browser_init();
    browser_load_url(url);
    for (int i = 0; i < 30 && browser_is_loading(); ++i) thread_sleep(10);
    cmd_vsoma("", 0, 0);
}

void gui_open_file_manager(const char *folder)
{
    if (gui_win_open(APP_FILE_MANAGER)) {
        if (folder && *folder) fm_open_folder(folder);
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_FILE_MANAGER;
    agent_global_focus = 0;
    if (folder && *folder) fm_open_folder(folder); else fm_init();
    cmd_vsoma("", 0, 0);
}

void gui_open_memview(void)
{
    if (gui_win_open(APP_AGENT_MEMORY)) return;
    ensure_window_geom();
    current_app = APP_AGENT_MEMORY;
    agent_global_focus = 0;
    memview_init();
    cmd_vsoma("", 0, 0);
}

void gui_open_layout(void)
{
    if (gui_win_open(APP_LAYOUT)) return;
    ensure_window_geom();
    current_app = APP_LAYOUT;
    agent_global_focus = 0;
    layout_init();
    cmd_vsoma("", 0, 0);
}

void gui_open_paint(const char *path)
{
    if (gui_win_open(APP_PAINT)) {
        if (path && *path) paint_open_file(path);
        gui_live_paint();
        return;
    }
    ensure_window_geom();
    current_app = APP_PAINT;
    agent_global_focus = 0;
    paint_init();
    if (path && *path) paint_open_file(path);
    cmd_vsoma("", 0, 0);
}

void gui_open_agents(void)
{
    if (gui_win_open(APP_AGENTS)) return;
    ensure_window_geom();
    current_app = APP_AGENTS;
    agent_global_focus = 0;
    cmd_vsoma("", 0, 0);
}

/* El estado del raton es compartido con el hilo de fondo */
#define mouse_x g_mouse_x_s
#define mouse_y g_mouse_y_s

static int g_gui_dead_accent = 0;
int cmd_vsoma(const char *args, char *out_buf, uint32_t max_out)
{
    (void)out_buf; (void)max_out;
    while (*args == ' ') args++;

    uint32_t target_w = FB_RES_FHD_W;
    uint32_t target_h = FB_RES_FHD_H;

    if (args[0] == '8' || (args[0] == 's' && args[1] == 'v')) {
        target_w = FB_RES_SVGA_W; target_h = FB_RES_SVGA_H;
    } else if (args[0] == '7' || (args[0] == '1' && args[1] == '2')) {
        target_w = FB_RES_HD_W; target_h = FB_RES_HD_H;
    }

    if (fb_set_mode(target_w, target_h) != 0) {
        kprint("[VSOMA ERROR] No se pudo activar modo grafico.\n");
        return 1;
    }

    uint32_t cur_w = fb_get_width();
    uint32_t cur_h = fb_get_height();

    /* uint32_t side_w = 0; */
    uint32_t win_x = her_win_x;
    uint32_t win_y = her_win_y + HER_TITLE_H;
    uint32_t win_w = her_win_w;
    uint32_t win_h = (her_win_h > HER_TITLE_H) ? her_win_h - HER_TITLE_H : 1;

    uint32_t max_visible_terminal = (win_h - 60) / 15;

    g_live_win_x = win_x; g_live_win_y = win_y;
    g_live_win_w = win_w; g_live_win_h = win_h;
    g_live_ready = 1;
    vfs_live_hook = gui_live_file_hook;

    agent_log_count = 0;
    agent_global_scroll = 0;
    agent_global_focus = 1;
    log_add(agent_log, &agent_log_count, 0, "[SOMA AI AGENT]: Interfaz conversacional activa.", COLOR_CYAN, (win_w - 32)/8);

    term_init_banner(max_visible_terminal);
    browser_init();
    fm_init();
    imgview_init();
    notepad_init();
    memview_init();

    current_app = APP_DESKTOP;
    her_icons_visible = 0;
    her_icons_anim_phase = 0;
    her_icons_burst_at = 0;
    her_icons_burst_opening = 1;
    her_has_user_text = 0;
    her_has_agent_text = 0;
    her_angle = 0;
    her_dragging = 0; her_resizing = 0;
    her_last_anim = timer_get_uptime_ms();
    {
        uint32_t _w = fb_get_width(), _h = fb_get_height();
        her_win_w = _w > 800 ? 720 : (_w > 400 ? _w - 80 : _w);
        her_win_h = _h > 600 ? 480 : (_h > 300 ? _h - 120 : _h);
        her_place_window_corner(_w, _h, 0);
    }
    HER_SYNC_WIN();
    agent_global_focus = 1;
    render_desktop();
    fb_present_full();

    mouse_x = (int32_t)cur_w / 2;
    mouse_y = (int32_t)cur_h / 2;
    mouse_init();
    mouse_erase_cursor();
    fb_present();

    cur_cmd_len = 0;
    cur_cursor_pos = 0;
    for (uint32_t i = 0; i < sizeof(cur_cmd); ++i) cur_cmd[i] = '\0';

    int cursor_visible = 1;
    uint64_t last_cursor_time = timer_get_uptime_ms();
    uint64_t last_clock_time = timer_get_uptime_ms();

    while (1) {
        uint64_t now = timer_get_uptime_ms();

        /* --- LATIDO PERPETUO: El agujero negro palpita a 30 FPS en todo momento --- */
        if (now - her_last_anim >= 33) {
            her_last_anim = now;
            her_angle = (her_angle + 3) & 255;
            /* Transicion suave en pasos pequenos a 30 FPS (~0,5 s). */
            if (her_icons_visible) {
                if (her_icons_anim_phase < HER_ICON_ANIM_MAX) {
                    her_icons_anim_phase += 36u; /* Velocidad 2x */
                    if (her_icons_anim_phase > HER_ICON_ANIM_MAX)
                        her_icons_anim_phase = HER_ICON_ANIM_MAX;
                }
            } else if (her_icons_anim_phase > 0u) {
                her_icons_anim_phase = (her_icons_anim_phase > 36u)
                                     ? her_icons_anim_phase - 36u : 0u;
            }
            {
                uint32_t ocx = cur_w / 2, ocy = cur_h / 2;
                her_reveal_step();
                her_parts_spawn(ocx, ocy, her_angle);
            }
            her_vortex_close_step();
            her_refresh_all();
            if (her_cursor_should_show())
                mouse_draw_cursor(mouse_x, mouse_y);
            else
                mouse_erase_cursor();
            fb_present();
        }

        if (now - last_clock_time >= 1000) {
            last_clock_time = now;
            struct rtc_time t; rtc_get_datetime(&t);
            char cstr[16]; uint32_t cp = 0;
            cstr[cp++] = (char)('0' + (t.hour / 10)); cstr[cp++] = (char)('0' + (t.hour % 10)); cstr[cp++] = ':';
            cstr[cp++] = (char)('0' + (t.min / 10)); cstr[cp++] = (char)('0' + (t.min % 10)); cstr[cp++] = ':';
            cstr[cp++] = (char)('0' + (t.sec / 10)); cstr[cp++] = (char)('0' + (t.sec % 10)); cstr[cp] = '\0';
            uint32_t system_y = cur_h > SOMA_SYSTEM_BAR_H ? cur_h - SOMA_SYSTEM_BAR_H : 0;
            fb_fill_rect(140, system_y + 10, 120, 20, 0x000B0F19);
            font_draw_string(140, system_y + 14, cstr, 0x00F8FAFC, 1);
        }

        if (now - last_cursor_time >= 400) {
            cursor_visible = !cursor_visible;
            last_cursor_time = now;
        }

        int32_t m_dx = 0, m_dy = 0;
        int m_left_click = 0;

        if (mouse_poll(&m_dx, &m_dy, &m_left_click)) {
            if (!mouse_left_held()) {
                if (her_dragging || her_resizing) {
                    her_dragging = 0;
                    her_resizing = 0;
                    mouse_set_cursor(CURSOR_ARROW);
                }
                if (current_app == APP_PAINT) {
                    paint_handle_mouse(mouse_x, mouse_y, 0, win_x, win_y, win_w, win_h);
                }
            }

            if (m_dx != 0 || m_dy != 0) {
                mouse_x += m_dx; mouse_y -= m_dy;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x >= (int32_t)cur_w) mouse_x = (int32_t)cur_w - 1;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y >= (int32_t)cur_h) mouse_y = (int32_t)cur_h - 1;

                if (her_dragging || her_resizing) {
                    her_win_apply_motion(mouse_x, mouse_y, cur_w, cur_h);
                    HER_SYNC_WIN();
                    if (her_cursor_should_show())
                        mouse_draw_cursor(mouse_x, mouse_y);
                    fb_present();
                } else if (!mouse_left_held() && her_num_windows > 0) {
                    /* Hover de bordes: cambiar cursor a CROSSHAIR al posarse en un marco */
                    int over_edge = 0;
                    for (int wi = her_num_windows - 1; wi >= 0; --wi) {
                        int edge = 0;
                        int h = her_win_hit_target(&her_windows[wi], mouse_x, mouse_y, &edge);
                        if (h == 2) { over_edge = 1; break; }
                        else if (h > 0) { break; }
                    }
                    if (over_edge) {
                        mouse_set_cursor(CURSOR_CROSSHAIR);
                    } else if (mouse_get_current_cursor() == CURSOR_CROSSHAIR) {
                        mouse_set_cursor(CURSOR_ARROW);
                    }
                }

                if (current_app == APP_PAINT && !her_dragging && !her_resizing && mouse_left_held()) {
                    paint_handle_mouse(mouse_x, mouse_y, 1, win_x, win_y, win_w, win_h);
                    paint_render_window(win_x, win_y, win_w, win_h, 1);
                }

                if (her_cursor_should_show())
                    mouse_draw_cursor(mouse_x, mouse_y);
                else
                    mouse_erase_cursor();
            }

            if (m_left_click) {
                int win_clicked = -1;
                int win_hit_code = 0;
                int win_edge_code = 0;

                /* Buscar de arriba a abajo en orden Z */
                for (int wi = her_num_windows - 1; wi >= 0; --wi) {
                    int edge = 0;
                    int h = her_win_hit_target(&her_windows[wi], mouse_x, mouse_y, &edge);
                    if (h > 0) {
                        win_clicked = wi;
                        win_hit_code = h;
                        win_edge_code = edge;
                        break;
                    }
                }

                if (win_clicked >= 0) {
                    if (win_clicked != her_num_windows - 1) {
                        her_bring_to_front(win_clicked);
                        win_clicked = her_num_windows - 1;
                    }

                    struct her_win_node *top_win = &her_windows[win_clicked];
                    her_win_x = top_win->x; her_win_y = top_win->y;
                    her_win_w = top_win->w; her_win_h = top_win->h;
                    HER_SYNC_WIN();

                    if (win_hit_code == 3) {
                        her_close_target_window_vortex(win_clicked);
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                    if (win_hit_code == 1) {
                        her_dragging = 1; her_resizing = 0;
                        her_drag_ox = mouse_x - (int32_t)top_win->x;
                        her_drag_oy = mouse_y - (int32_t)top_win->y;
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                    if (win_hit_code == 2) {
                        her_resizing = 1; her_dragging = 0;
                        her_resize_edge = win_edge_code;
                        her_resize_start_mx = mouse_x; her_resize_start_my = mouse_y;
                        her_resize_start_x = (int32_t)top_win->x; her_resize_start_y = (int32_t)top_win->y;
                        her_resize_start_w = (int32_t)top_win->w; her_resize_start_h = (int32_t)top_win->h;
                        mouse_set_cursor(CURSOR_CROSSHAIR);
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                }

                int her_inside = (win_clicked >= 0);

                if (!her_inside) {
                    int dhit = her_dock_hit(mouse_x, mouse_y, cur_w, cur_h);
                    if (dhit >= 0) {
                        her_open_app((enum app_mode)dhit, cur_w, cur_h);
                        HER_SYNC_WIN();
                        her_refresh_all();
                        if (her_cursor_should_show())
                            mouse_draw_cursor(mouse_x, mouse_y);
                        else
                            mouse_erase_cursor();
                        fb_present();
                        continue;
                    }
                    if (her_icons_visible && current_app == APP_DESKTOP) {
                        /* Clic en el agujero negro o fondo mientras orbitan: absorber iconos */
                        her_close_icons_vortex(cur_w, cur_h);
                        her_refresh_all();
                        fb_present();
                        continue;
                    }
                    agent_global_focus = 1;
                    her_refresh_all();
                    fb_present();
                    continue;
                }

                if (current_app != APP_DESKTOP) {
                    agent_global_focus = (current_app == APP_AGENT_CHAT) ? 1 : 0;
                    if (current_app == APP_WEB_BROWSER) {
                        int act = browser_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        browser_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_IMAGE_VIEWER) {
                        int act = imgview_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        imgview_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_PAINT) {
                        int act = paint_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        paint_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_LAYOUT) {
                        int act = layout_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        else { layout_render_window(win_x, win_y, win_w, win_h, 1); }
                    } else if (current_app == APP_AGENT_MEMORY) {
                        int act = memview_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        if (act == 1) { her_close_current_app_vortex(); continue; }
                        else if (act == 4) {
                            const char *sel_file = memview_get_selected_path();
                            if (sel_file && *sel_file) {
                                current_app = APP_NOTEPAD;
                                render_desktop();
                                notepad_init();
                                notepad_load_file(sel_file);
                                notepad_render_window(win_x, win_y, win_w, win_h, 1);
                            }
                        } else {
                            memview_render_window(win_x, win_y, win_w, win_h, 1);
                        }
                    } else if (current_app == APP_NOTEPAD) {
                        notepad_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h);
                        notepad_render_window(win_x, win_y, win_w, win_h, 1);
                    } else if (current_app == APP_FILE_MANAGER) {
                        char target_file[160]; target_file[0] = '\0';
                        int action = fm_handle_click(mouse_x, mouse_y, win_x, win_y, win_w, win_h, target_file, sizeof(target_file));
                        if (action == 2) {
                            current_app = APP_IMAGE_VIEWER; render_desktop(); imgview_init(); imgview_load_file(target_file); imgview_render_window(win_x, win_y, win_w, win_h, 1);
                        } else if (action == 3) {
                            current_app = APP_WEB_BROWSER; render_desktop(); browser_load_url(target_file); browser_render_window(win_x, win_y, win_w, win_h, 1);
                        } else if (action == 4) {
                            current_app = APP_NOTEPAD; render_desktop(); notepad_init(); notepad_load_file(target_file); notepad_render_window(win_x, win_y, win_w, win_h, 1);
                        } else {
                            fm_render_window(win_x, win_y, win_w, win_h, 1);
                        }
                    } else if (current_app == APP_AGENT_CHAT) {
                        if (cur_cmd_len > 0 && point_in_rect(mouse_x, mouse_y, win_x + win_w - 110, win_y + win_h - 46, 96, 46)) goto exec_agent;
                    }
                }
            }
            fb_present();
        }

        if (kgetc_ready()) {
            char c = kgetc();
            if (c == 27) { break; }

            if (current_app == APP_DESKTOP) agent_global_focus = 1;
            if ((c == 'x' || c == 'X') && agent_global_len == 0 &&
                (current_app == APP_DESKTOP || agent_global_focus)) {
                if (current_app != APP_DESKTOP) {
                    her_close_current_app_vortex();
                } else if (her_icons_visible) {
                    /* Absorción orbital de los iconos por el agujero negro */
                    her_close_icons_vortex(cur_w, cur_h);
                } else {
                    /* Mostrar iconos: destruir diálogos previos en partículas antes de abrir */
                    int has_text = (her_last_q[0] != '\0' || her_ans_len > 0 || her_thk_len > 0);
                    if (has_text) {
                        render_desktop();
                        her_start_text_destruction(cur_w, cur_h);
                    } else {
                        her_text_suppressed_by_dock = 0;
                        her_icons_visible = 1;
                        her_icons_anim_phase = 0;
                        her_icons_burst_opening = 1;
                        her_icons_burst_at = timer_get_uptime_ms();
                    }
                }
                render_desktop();
                if (her_cursor_should_show())
                    mouse_draw_cursor(mouse_x, mouse_y);
                else
                    mouse_erase_cursor();
                fb_present();
                continue;
            }

            if (agent_global_focus) {
                if (c == '\t') {
                    agent_global_focus = 0;
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == 16) {
                    uint32_t visible = SOMA_AGENT_RESP_H / SOMA_AGENT_LINE_H;
                    if (visible == 0) visible = 1;
                    if ((uint32_t)agent_log_count > visible) {
                        uint32_t max_scroll = (uint32_t)agent_log_count - visible;
                        if (agent_global_scroll < max_scroll) agent_global_scroll++;
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == 14) {
                    if (agent_global_scroll > 0) agent_global_scroll--;
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == 2) { /* Flecha izquierda con soporte UTF-8 */
                    if (agent_global_cursor > 0) {
                        agent_global_cursor--;
                        while (agent_global_cursor > 0 && ((uint8_t)agent_global_cmd[agent_global_cursor] & 0xC0) == 0x80)
                            agent_global_cursor--;
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == 6) { /* Flecha derecha con soporte UTF-8 */
                    if (agent_global_cursor < agent_global_len) {
                        agent_global_cursor++;
                        while (agent_global_cursor < agent_global_len && ((uint8_t)agent_global_cmd[agent_global_cursor] & 0xC0) == 0x80)
                            agent_global_cursor++;
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == '\b' || c == 127) {
                    g_gui_dead_accent = 0;
                    if (agent_global_cursor > 0) {
                        uint32_t del = 1;
                        if (((uint8_t)agent_global_cmd[agent_global_cursor - 1] & 0xC0) == 0x80) {
                            while (agent_global_cursor > del && ((uint8_t)agent_global_cmd[agent_global_cursor - del] & 0xC0) == 0x80)
                                del++;
                        }
                        for (uint32_t i = agent_global_cursor - del; i + del <= agent_global_len; ++i)
                            agent_global_cmd[i] = agent_global_cmd[i + del];
                        agent_global_len -= del;
                        agent_global_cursor -= del;
                        agent_global_cmd[agent_global_len] = '\0';
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                if (c == '\r' || c == '\n') {
                    submit_global_agent_prompt(win_x, win_y, win_w, win_h - 56);
                    continue;
                }
                /* Tecla de acento como dead-key en GUI */
                if (c == 0x27 || (uint8_t)c == 0xB4 || c == 0x60) {
                    g_gui_dead_accent = 1;
                    continue;
                }
                if (c == 0x22) {
                    g_gui_dead_accent = 2;
                    continue;
                }

                if ((uint8_t)c >= 32 && (uint8_t)c != 127) {
                    if (g_gui_dead_accent != 0) {
                        int dk = g_gui_dead_accent;
                        g_gui_dead_accent = 0;
                        if (dk == 1) {
                            switch (c) {
                            case 'a': agent_global_insert_str("\xC3\xA1"); break;
                            case 'e': agent_global_insert_str("\xC3\xA9"); break;
                            case 'i': agent_global_insert_str("\xC3\xAD"); break;
                            case 'o': agent_global_insert_str("\xC3\xB3"); break;
                            case 'u': agent_global_insert_str("\xC3\xBA"); break;
                            case 'A': agent_global_insert_str("\xC3\x81"); break;
                            case 'E': agent_global_insert_str("\xC3\x89"); break;
                            case 'I': agent_global_insert_str("\xC3\x8D"); break;
                            case 'O': agent_global_insert_str("\xC3\x93"); break;
                            case 'U': agent_global_insert_str("\xC3\x9A"); break;
                            case ' ': agent_global_insert_str("'"); break;
                            default: {
                                char s_tmp[3] = { (char)0x27, c, 0 };
                                agent_global_insert_str(s_tmp);
                                break;
                            }
                            }
                        } else if (dk == 2) {
                            switch (c) {
                            case 'u': agent_global_insert_str("\xC3\xBC"); break;
                            case 'U': agent_global_insert_str("\xC3\x9C"); break;
                            case ' ': agent_global_insert_str("\""); break;
                            default: {
                                char s_tmp[3] = { (char)0x22, c, 0 };
                                agent_global_insert_str(s_tmp);
                                break;
                            }
                            }
                        }
                    } else {
                        char one[2] = { c, 0 };
                        agent_global_insert_str(one);
                    }
                    render_agent_global_rail(cur_w, cur_h);
                    fb_present();
                    continue;
                }
                continue;
            }

            if (current_app == APP_DESKTOP) continue;
            if (current_app == APP_AGENTS) {
                agents_render_window(win_x, win_y, win_w, win_h, 1);
                fb_present();
                continue;
            }

            if (current_app == APP_WEB_BROWSER) {
                browser_handle_key(c); browser_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_IMAGE_VIEWER) {
                imgview_handle_key(c); imgview_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_FILE_MANAGER) {
                fm_handle_key(c); fm_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_NOTEPAD) {
                notepad_handle_key(c); notepad_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_AGENT_MEMORY) {
                memview_handle_key(c); memview_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_PAINT) {
                if (c == 27) { her_close_current_app_vortex(); continue; }
                paint_handle_key(c); paint_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }
            if (current_app == APP_LAYOUT) {
                if (c == 27) {
                    her_close_current_app_vortex();
                    continue;
                }
                layout_handle_key(c); layout_render_window(win_x, win_y, win_w, win_h, 1); fb_present(); continue;
            }

            if (c == 2) {
                if (cur_cursor_pos > 0) {
                    cur_cursor_pos--;
                    if (current_app == APP_TERMINAL_SHELL) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    else if (current_app == APP_AGENT_CHAT) render_input_bar_chat("soma> ", COLOR_CYAN, win_x, win_y + win_h - 46, win_w, 46, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }
            if (c == 6) {
                if (cur_cursor_pos < cur_cmd_len) {
                    cur_cursor_pos++;
                    if (current_app == APP_TERMINAL_SHELL) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    else if (current_app == APP_AGENT_CHAT) render_input_bar_chat("soma> ", COLOR_CYAN, win_x, win_y + win_h - 46, win_w, 46, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }
            if (c == 16 && current_app == APP_TERMINAL_SHELL) {
                if (term_hist_count > 0) {
                    if (term_hist_idx == -1) term_hist_idx = term_hist_count - 1;
                    else if (term_hist_idx > 0) term_hist_idx--;
                    cur_cmd_len = 0;
                    while (term_hist[term_hist_idx][cur_cmd_len] && cur_cmd_len < sizeof(cur_cmd)-1) {
                        cur_cmd[cur_cmd_len] = term_hist[term_hist_idx][cur_cmd_len];
                        cur_cmd_len++;
                    }
                    cur_cmd[cur_cmd_len] = '\0';
                    cur_cursor_pos = cur_cmd_len;
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }
            if (c == 14 && current_app == APP_TERMINAL_SHELL) {
                if (term_hist_idx != -1) {
                    if (term_hist_idx < term_hist_count - 1) {
                        term_hist_idx++;
                        cur_cmd_len = 0;
                        while (term_hist[term_hist_idx][cur_cmd_len] && cur_cmd_len < sizeof(cur_cmd)-1) {
                            cur_cmd[cur_cmd_len] = term_hist[term_hist_idx][cur_cmd_len];
                            cur_cmd_len++;
                        }
                        cur_cmd[cur_cmd_len] = '\0';
                        cur_cursor_pos = cur_cmd_len;
                    } else {
                        term_hist_idx = -1;
                        cur_cmd_len = 0; cur_cmd[0] = '\0'; cur_cursor_pos = 0;
                    }
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }
            if (c == '\t' && current_app == APP_TERMINAL_SHELL) {
                const char *matches[16];
                int n_matches = shell_autocomplete(cur_cmd, cur_cursor_pos, matches, 16);
                if (n_matches == 1) {
                    const char *m = matches[0];
                    cur_cmd_len = 0;
                    while (m[cur_cmd_len] && cur_cmd_len < sizeof(cur_cmd)-2) { cur_cmd[cur_cmd_len] = m[cur_cmd_len]; cur_cmd_len++; }
                    cur_cmd[cur_cmd_len++] = ' '; cur_cmd[cur_cmd_len] = '\0';
                    cur_cursor_pos = cur_cmd_len;
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }

            if (c == '\b' || c == 127) {
                if (cur_cursor_pos > 0) {
                    for (uint32_t i = cur_cursor_pos - 1; i < cur_cmd_len; ++i) cur_cmd[i] = cur_cmd[i + 1];
                    cur_cmd_len--; cur_cursor_pos--; cur_cmd[cur_cmd_len] = '\0';
                    if (current_app == APP_TERMINAL_SHELL) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    else render_input_bar_chat("soma> ", COLOR_CYAN, win_x, win_y + win_h - 46, win_w, 46, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
                continue;
            }

            if (c == '\r' || c == '\n') {
                if (current_app == APP_TERMINAL_SHELL) {
                    if (cur_cmd_len == 0) {
                        term_add_line("\033[1;36msoma\033[1;32m>\033[0m", max_visible_terminal);
                        render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                        fb_present(); continue;
                    }
                    char echo_line[TERM_LINE_LEN]; uint32_t ep = 0;
                    const char *prm = "\033[1;36msoma\033[1;32m>\033[0m ";
                    while (*prm) echo_line[ep++] = *prm++;
                    for (uint32_t i = 0; i < cur_cmd_len && ep < sizeof(echo_line) - 1; ++i) echo_line[ep++] = cur_cmd[i];
                    echo_line[ep] = '\0'; term_add_line(echo_line, max_visible_terminal);

                    if (term_hist_count < 16) {
                        uint32_t hp = 0; while (cur_cmd[hp]) { term_hist[term_hist_count][hp] = cur_cmd[hp]; hp++; }
                        term_hist[term_hist_count][hp] = '\0';
                        term_hist_count++;
                    } else {
                        for (int h = 0; h < 15; ++h) {
                            for (int k = 0; k < 128; ++k) term_hist[h][k] = term_hist[h + 1][k];
                        }
                        uint32_t hp = 0; while (cur_cmd[hp]) { term_hist[15][hp] = cur_cmd[hp]; hp++; }
                        term_hist[15][hp] = '\0';
                    }
                    term_hist_idx = -1;

                    char cmd_run[128]; for (uint32_t i = 0; i <= cur_cmd_len; ++i) cmd_run[i] = cur_cmd[i];
                    cur_cmd_len = 0; cur_cursor_pos = 0; cur_cmd[0] = '\0';

                    if (cmd_run[0] == 'c' && cmd_run[1] == 'l' && cmd_run[2] == 'e') term_num_lines = 0;
                    else {
                        static char cmd_out[4096]; cmd_out[0] = '\0';
                        int ret = dispatch_command(cmd_run, cmd_out, sizeof(cmd_out));
                        if (cmd_out[0] != '\0') {
                            const char *scan = cmd_out;
                            while (*scan) {
                                char line_single[TERM_LINE_LEN]; uint32_t lp = 0;
                                while (*scan && *scan != '\n' && lp < sizeof(line_single) - 1) { if (*scan != '\r') line_single[lp++] = *scan; scan++; }
                                line_single[lp] = '\0'; if (*scan == '\n') scan++;
                                if (lp > 0) term_add_line(line_single, max_visible_terminal);
                            }
                        } else if (!ret) term_add_line("Comando no reconocido.", max_visible_terminal);
                    }
                    render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1); fb_present(); continue;

                } else if (current_app == APP_AGENT_CHAT) {
                    if (cur_cmd_len == 0) continue;
                exec_agent:
                    agent_global_len = cur_cmd_len < sizeof(agent_global_cmd) - 1
                                     ? cur_cmd_len : sizeof(agent_global_cmd) - 1;
                    for (uint32_t i = 0; i < agent_global_len; ++i) agent_global_cmd[i] = cur_cmd[i];
                    agent_global_cmd[agent_global_len] = '\0';
                    agent_global_cursor = agent_global_len;
                    agent_global_focus = 1;
                    submit_global_agent_prompt(win_x, win_y, win_w, win_h - 56);
                    cur_cmd_len = 0; cur_cursor_pos = 0; cur_cmd[0] = '\0';
                    continue;
                }
            }

            if ((uint8_t)c >= 32 && (uint8_t)c != 127) {
                if (cur_cmd_len < sizeof(cur_cmd) - 2) {
                    for (uint32_t i = cur_cmd_len; i > cur_cursor_pos; --i) cur_cmd[i] = cur_cmd[i - 1];
                    cur_cmd[cur_cursor_pos++] = c; cur_cmd_len++; cur_cmd[cur_cmd_len] = '\0';
                    if (current_app == APP_TERMINAL_SHELL) render_real_terminal(win_x, win_y, win_w, win_h, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    else render_input_bar_chat("soma> ", COLOR_CYAN, win_x, win_y + win_h - 46, win_w, 46, cur_cmd, cur_cmd_len, cur_cursor_pos, 1);
                    fb_present();
                }
            }
        }
        thread_yield(); arch_pause();
    }
    g_live_ready = 0; vfs_live_hook = 0;
    mouse_erase_cursor(); mouse_shutdown(); fb_restore_text_mode(); keyboard_reset_state(); return 1;
}
