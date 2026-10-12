#ifndef SOMA_VICON_H
#define SOMA_VICON_H
/* SOMA_VECTOR_FONTS_PATCH
 * Iconos vectoriales definidos en una rejilla de 64x64. Se rasterizan al
 * tamano pedido (8..192 px) con antialiasing y se cachean como mascara alfa.
 * Unidades de las primitivas: 1/16 de celda de rejilla (macro VU(n)).
 */
#include "vraster.h"

#define VI_FN static __attribute__((unused))
#define VU(n) ((n) * 16)
#define VI_TILE_ID VICON_COUNT

static int64_t vp_k = 0;                    /* tamano_px * 256 */
static int32_t vp_cx = 0, vp_cy = 0, vp_s0x = 0, vp_s0y = 0;

VI_FN int32_t vp_c(int v) { return (int32_t)(((int64_t)v * vp_k) / 1024); }
VI_FN void vp_m(int x, int y) { vp_cx = vp_c(x); vp_cy = vp_c(y); vp_s0x = vp_cx; vp_s0y = vp_cy; }
VI_FN void vp_l(int x, int y)
{
    int32_t nx = vp_c(x), ny = vp_c(y);
    vr_line(vp_cx, vp_cy, nx, ny);
    vp_cx = nx; vp_cy = ny;
}
VI_FN void vp_q(int cx, int cy, int x, int y)
{
    int32_t kx = vp_c(cx), ky = vp_c(cy), nx = vp_c(x), ny = vp_c(y);
    vr_quad(vp_cx, vp_cy, kx, ky, nx, ny);
    vp_cx = nx; vp_cy = ny;
}
VI_FN void vp_z(void)
{
    vr_line(vp_cx, vp_cy, vp_s0x, vp_s0y);
    vp_cx = vp_s0x; vp_cy = vp_s0y;
}

/* Octantes de circunferencia: puntos P[i] y controles C[i] (x1024) */
static const int vp_ux[8] = { 1024, 724, 0, -724, -1024, -724, 0, 724 };
static const int vp_uy[8] = { 0, 724, 1024, 724, 0, -724, -1024, -724 };
static const int vp_kx[8] = { 1024, 424, -424, -1024, -1024, -424, 424, 1024 };
static const int vp_ky[8] = { 424, 1024, 1024, 424, -424, -1024, -1024, -424 };

VI_FN void vp_arc_fwd(int i, int cx, int cy, int rx, int ry)   /* P[i] -> P[i+1] */
{
    int j = (i + 1) & 7;
    vp_q(cx + rx * vp_kx[i] / 1024, cy + ry * vp_ky[i] / 1024,
         cx + rx * vp_ux[j] / 1024, cy + ry * vp_uy[j] / 1024);
}
VI_FN void vp_arc_bwd(int i, int cx, int cy, int rx, int ry)   /* P[i+1] -> P[i] */
{
    vp_q(cx + rx * vp_kx[i] / 1024, cy + ry * vp_ky[i] / 1024,
         cx + rx * vp_ux[i] / 1024, cy + ry * vp_uy[i] / 1024);
}

/* ccw=0: relleno; ccw=1: agujero (orientacion opuesta) */
VI_FN void vp_ellipse(int cx, int cy, int rx, int ry, int ccw)
{
    vp_m(cx + rx, cy);
    if (!ccw) { for (int i = 0; i < 8; ++i) vp_arc_fwd(i, cx, cy, rx, ry); }
    else      { for (int i = 7; i >= 0; --i) vp_arc_bwd(i, cx, cy, rx, ry); }
    vp_z();
}

VI_FN void vp_rrect(int x, int y, int w, int h, int r, int ccw)
{
    vp_m(x + r, y);
    if (!ccw) {
        vp_l(x + w - r, y);
        vp_arc_fwd(6, x + w - r, y + r, r, r);     vp_arc_fwd(7, x + w - r, y + r, r, r);
        vp_l(x + w, y + h - r);
        vp_arc_fwd(0, x + w - r, y + h - r, r, r); vp_arc_fwd(1, x + w - r, y + h - r, r, r);
        vp_l(x + r, y + h);
        vp_arc_fwd(2, x + r, y + h - r, r, r);     vp_arc_fwd(3, x + r, y + h - r, r, r);
        vp_l(x, y + r);
        vp_arc_fwd(4, x + r, y + r, r, r);         vp_arc_fwd(5, x + r, y + r, r, r);
    } else {
        vp_arc_bwd(5, x + r, y + r, r, r);         vp_arc_bwd(4, x + r, y + r, r, r);
        vp_l(x, y + h - r);
        vp_arc_bwd(3, x + r, y + h - r, r, r);     vp_arc_bwd(2, x + r, y + h - r, r, r);
        vp_l(x + w - r, y + h);
        vp_arc_bwd(1, x + w - r, y + h - r, r, r); vp_arc_bwd(0, x + w - r, y + h - r, r, r);
        vp_l(x + w, y + r);
        vp_arc_bwd(7, x + w - r, y + r, r, r);     vp_arc_bwd(6, x + w - r, y + r, r, r);
    }
    vp_z();
}

VI_FN void vp_poly(const int *p, int n, int ccw)
{
    vp_m(p[0], p[1]);
    if (!ccw) { for (int i = 1; i < n; ++i) vp_l(p[2 * i], p[2 * i + 1]); }
    else      { for (int i = n - 1; i >= 1; --i) vp_l(p[2 * i], p[2 * i + 1]); }
    vp_z();
}

VI_FN void vp_rect(int x, int y, int w, int h, int ccw)
{
    int p[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    vp_poly(p, 4, ccw);
}

/* Trazo con grosor wd; round=1 -> extremos redondeados */
VI_FN void vp_stroke(int x0, int y0, int x1, int y1, int wd, int round)
{
    int dx = x1 - x0, dy = y1 - y0;
    int len = (int)vr_isqrt((uint32_t)(dx * dx + dy * dy));
    if (len == 0) len = 1;
    int hw = wd / 2;
    int nx = dy * hw / len, ny = -dx * hw / len;
    int p[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
    vp_poly(p, 4, 0);
    if (round) {
        vp_ellipse(x0, y0, hw, hw, 0);
        vp_ellipse(x1, y1, hw, hw, 0);
    }
}

/* ------------------------------ ICONOS ------------------------------ */
VI_FN void vi_agent(void)       /* destello de IA */
{
    vp_m(VU(28), VU(8));
    vp_q(VU(28), VU(28), VU(50), VU(28));
    vp_q(VU(28), VU(28), VU(28), VU(48));
    vp_q(VU(28), VU(28), VU(6),  VU(28));
    vp_q(VU(28), VU(28), VU(28), VU(8));
    vp_z();
    vp_m(VU(48), VU(8));
    vp_q(VU(48), VU(14), VU(55), VU(14));
    vp_q(VU(48), VU(14), VU(48), VU(21));
    vp_q(VU(48), VU(14), VU(41), VU(14));
    vp_q(VU(48), VU(14), VU(48), VU(8));
    vp_z();
    vp_m(VU(46), VU(42));
    vp_q(VU(46), VU(48), VU(52), VU(48));
    vp_q(VU(46), VU(48), VU(46), VU(54));
    vp_q(VU(46), VU(48), VU(40), VU(48));
    vp_q(VU(46), VU(48), VU(46), VU(42));
    vp_z();
}

VI_FN void vi_agents(void)      /* grupo de agentes */
{
    vp_ellipse(VU(32), VU(21), VU(8), VU(8), 0);
    vp_m(VU(20), VU(52)); vp_q(VU(20), VU(33), VU(32), VU(33));
    vp_q(VU(44), VU(33), VU(44), VU(52)); vp_z();
    vp_ellipse(VU(14), VU(28), VU(5), VU(5), 0);
    vp_m(VU(5), VU(50)); vp_q(VU(5), VU(38), VU(14), VU(38));
    vp_q(VU(23), VU(38), VU(23), VU(50)); vp_z();
    vp_ellipse(VU(50), VU(28), VU(5), VU(5), 0);
    vp_m(VU(41), VU(50)); vp_q(VU(41), VU(38), VU(50), VU(38));
    vp_q(VU(59), VU(38), VU(59), VU(50)); vp_z();
}

VI_FN void vi_terminal(void)
{
    vp_rrect(VU(7), VU(13), VU(50), VU(38), VU(7), 0);
    vp_rrect(VU(11), VU(17), VU(42), VU(30), VU(4), 1);
    vp_stroke(VU(19), VU(24), VU(27), VU(32), VU(4), 1);
    vp_stroke(VU(27), VU(32), VU(19), VU(40), VU(4), 1);
    vp_stroke(VU(32), VU(40), VU(44), VU(40), VU(4), 1);
}

VI_FN void vi_browser(void)     /* globo terraqueo */
{
    vp_ellipse(VU(32), VU(32), VU(23), VU(23), 0);
    vp_ellipse(VU(32), VU(32), VU(20), VU(20), 1);
    vp_ellipse(VU(32), VU(32), VU(11), VU(20), 0);
    vp_ellipse(VU(32), VU(32), VU(8),  VU(20), 1);
    vp_stroke(VU(12), VU(32), VU(52), VU(32), VU(3), 0);
    vp_stroke(VU(15), VU(23), VU(49), VU(23), VU(3), 0);
    vp_stroke(VU(15), VU(41), VU(49), VU(41), VU(3), 0);
}

VI_FN void vi_files(void)       /* carpeta */
{
    vp_m(VU(12), VU(14)); vp_l(VU(26), VU(14)); vp_l(VU(31), VU(20)); vp_l(VU(52), VU(20));
    vp_q(VU(57), VU(20), VU(57), VU(25)); vp_l(VU(57), VU(47));
    vp_q(VU(57), VU(52), VU(52), VU(52)); vp_l(VU(12), VU(52));
    vp_q(VU(7), VU(52), VU(7), VU(47));   vp_l(VU(7), VU(19));
    vp_q(VU(7), VU(14), VU(12), VU(14));  vp_z();
    vp_rect(VU(12), VU(26), VU(40), VU(3), 1);
}

VI_FN void vi_image(void)
{
    vp_rrect(VU(7), VU(11), VU(50), VU(42), VU(6), 0);
    vp_rrect(VU(11), VU(15), VU(42), VU(34), VU(3), 1);
    vp_ellipse(VU(22), VU(26), VU(4), VU(4), 0);
    int m[10] = { VU(13), VU(46), VU(26), VU(32), VU(34), VU(41), VU(40), VU(35), VU(51), VU(46) };
    vp_poly(m, 5, 0);
}

VI_FN void vi_notes(void)
{
    vp_rrect(VU(13), VU(7), VU(38), VU(50), VU(5), 0);
    vp_rect(VU(20), VU(18), VU(24), VU(3), 1);
    vp_rect(VU(20), VU(26), VU(24), VU(3), 1);
    vp_rect(VU(20), VU(34), VU(24), VU(3), 1);
    vp_rect(VU(20), VU(42), VU(14), VU(3), 1);
}

VI_FN void vi_paint(void)       /* paleta */
{
    vp_ellipse(VU(32), VU(33), VU(25), VU(22), 0);
    vp_ellipse(VU(18), VU(33), VU(4), VU(4), 1);
    vp_ellipse(VU(25), VU(22), VU(4), VU(4), 1);
    vp_ellipse(VU(38), VU(20), VU(4), VU(4), 1);
    vp_ellipse(VU(48), VU(29), VU(4), VU(4), 1);
    vp_ellipse(VU(40), VU(43), VU(6), VU(5), 1);
}

VI_FN void vi_layout(void)
{
    vp_rrect(VU(9),  VU(9),  VU(46), VU(15), VU(4), 0);
    vp_rrect(VU(9),  VU(28), VU(21), VU(27), VU(4), 0);
    vp_rrect(VU(34), VU(28), VU(21), VU(27), VU(4), 0);
}

VI_FN void vi_memory(void)      /* chip */
{
    vp_rrect(VU(17), VU(17), VU(30), VU(30), VU(4), 0);
    vp_rrect(VU(23), VU(23), VU(18), VU(18), VU(2), 1);
    vp_rrect(VU(27), VU(27), VU(10), VU(10), VU(2), 0);
    for (int i = 0; i < 3; ++i) {
        int c = VU(24 + 8 * i);
        vp_stroke(c, VU(7),  c, VU(17), VU(3), 0);
        vp_stroke(c, VU(47), c, VU(57), VU(3), 0);
        vp_stroke(VU(7),  c, VU(17), c, VU(3), 0);
        vp_stroke(VU(47), c, VU(57), c, VU(3), 0);
    }
}

VI_FN void vi_tile(void)        /* baldosa redondeada de fondo */
{
    vp_rrect(0, 0, VU(64), VU(64), VU(14), 0);
}

static void (*const vi_fns[VICON_COUNT + 1])(void) = {
    [VICON_AGENT]    = vi_agent,
    [VICON_AGENTS]   = vi_agents,
    [VICON_TERMINAL] = vi_terminal,
    [VICON_BROWSER]  = vi_browser,
    [VICON_FILES]    = vi_files,
    [VICON_IMAGE]    = vi_image,
    [VICON_NOTES]    = vi_notes,
    [VICON_PAINT]    = vi_paint,
    [VICON_LAYOUT]   = vi_layout,
    [VICON_MEMORY]   = vi_memory,
    [VICON_COUNT]    = vi_tile,
};

/* Cache de mascaras de iconos */
#define VI_CACHE_N 24
struct vi_ce { int16_t id; int16_t size; uint8_t *m; };
static struct vi_ce vi_cache[VI_CACHE_N];
static int vi_ncache = 0;
static uint8_t vi_arena[196608];
static uint32_t vi_used = 0;

VI_FN const uint8_t *vi_mask(int id, int size)
{
    for (int i = 0; i < vi_ncache; ++i)
        if (vi_cache[i].id == id && vi_cache[i].size == size) return vi_cache[i].m;

    uint32_t need = (uint32_t)size * (uint32_t)size;
    if (need > sizeof(vi_arena)) return 0;
    if (vi_ncache >= VI_CACHE_N || vi_used + need > sizeof(vi_arena)) { vi_ncache = 0; vi_used = 0; }

    uint8_t *m = vi_arena + vi_used;
    vi_used += need;
    vr_reset();
    vp_k = (int64_t)size * 256;
    vi_fns[id]();
    vr_fill(m, size, size);
    vi_cache[vi_ncache].id = (int16_t)id;
    vi_cache[vi_ncache].size = (int16_t)size;
    vi_cache[vi_ncache].m = m;
    vi_ncache++;
    return m;
}

#pragma GCC diagnostic pop
#endif
