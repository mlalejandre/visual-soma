#ifndef SOMA_VFONT_H
#define SOMA_VFONT_H
/* SOMA_VECTOR_FONTS_PATCH
 * Lector TrueType minimo (tablas glyf, loca, head, hhea, hmtx, cmap fmt 4, maxp).
 * Soporta glifos simples y compuestos. Sin hinting. Sin heap: cache en arena estatica.
 */
#include "vraster.h"

#define VF_FN static __attribute__((unused))
#define VF_MAX_PTS    1600
#define VF_MAX_CONT   128
#define VF_CACHE_N    512
#define VF_MAX_EM     120

struct vf_font {
    const uint8_t *d;
    uint32_t len;
    uint32_t glyf, loca, head, hhea, hmtx, cmap, maxp;
    uint32_t cmap_off;
    uint32_t upem, nglyphs, nhm;
    int loc_fmt;
    int ready;
};

static struct vf_font vf;
static uint16_t vf_ascii[256];
static int vf_pct = 100;        /* tamano relativo (%) */
static int vf_contrast = 70;    /* contraste 0..100 */

static int32_t  vf_px[VF_MAX_PTS];
static int32_t  vf_py[VF_MAX_PTS];
static uint8_t  vf_on[VF_MAX_PTS];
static int      vf_np = 0;
static uint16_t vf_end[VF_MAX_CONT];
static int      vf_nc = 0;

VF_FN uint32_t vf_u8(uint32_t o)  { return o < vf.len ? vf.d[o] : 0u; }
VF_FN uint32_t vf_u16(uint32_t o) { return (vf_u8(o) << 8) | vf_u8(o + 1); }
VF_FN int32_t  vf_s16(uint32_t o) { return (int32_t)(int16_t)vf_u16(o); }
VF_FN uint32_t vf_u32(uint32_t o) { return (vf_u16(o) << 16) | vf_u16(o + 2); }

/* ---- cmap formato 4 ---- */
VF_FN uint32_t vf_lookup(uint32_t cp)
{
    if (cp > 0xFFFFu) return 0;
    uint32_t base = vf.cmap_off;
    uint32_t segx2 = vf_u16(base + 6);
    uint32_t seg = segx2 / 2;
    uint32_t endc = base + 14;
    uint32_t startc = base + 16 + segx2;
    uint32_t delta = base + 16 + 2 * segx2;
    uint32_t rng = base + 16 + 3 * segx2;
    uint32_t lo = 0, hi = seg;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (vf_u16(endc + mid * 2) < cp) lo = mid + 1; else hi = mid;
    }
    if (lo >= seg) return 0;
    uint32_t st = vf_u16(startc + lo * 2);
    if (cp < st) return 0;
    uint32_t ro = vf_u16(rng + lo * 2);
    uint32_t dl = vf_u16(delta + lo * 2);
    if (ro == 0) return (cp + dl) & 0xFFFFu;
    uint32_t g = vf_u16(rng + lo * 2 + ro + (cp - st) * 2);
    if (g == 0) return 0;
    return (g + dl) & 0xFFFFu;
}

VF_FN int vfont_init(const uint8_t *data, uint32_t len)
{
    uint8_t *z = (uint8_t *)&vf;
    for (uint32_t i = 0; i < sizeof(vf); ++i) z[i] = 0;
    vf.d = data;
    vf.len = len;

    uint32_t ver = vf_u32(0);
    if (ver != 0x00010000u && ver != 0x74727565u) return 0;

    uint32_t nt = vf_u16(4);
    for (uint32_t i = 0; i < nt; ++i) {
        uint32_t rec = 12 + i * 16;
        uint32_t tag = vf_u32(rec);
        uint32_t off = vf_u32(rec + 8);
        if      (tag == 0x676C7966u) vf.glyf = off;   /* glyf */
        else if (tag == 0x6C6F6361u) vf.loca = off;   /* loca */
        else if (tag == 0x68656164u) vf.head = off;   /* head */
        else if (tag == 0x68686561u) vf.hhea = off;   /* hhea */
        else if (tag == 0x686D7478u) vf.hmtx = off;   /* hmtx */
        else if (tag == 0x636D6170u) vf.cmap = off;   /* cmap */
        else if (tag == 0x6D617870u) vf.maxp = off;   /* maxp */
    }
    if (!vf.glyf || !vf.loca || !vf.head || !vf.hhea || !vf.hmtx || !vf.cmap || !vf.maxp) return 0;

    vf.upem    = vf_u16(vf.head + 18);
    vf.loc_fmt = (int)vf_s16(vf.head + 50);
    vf.nglyphs = vf_u16(vf.maxp + 4);
    vf.nhm     = vf_u16(vf.hhea + 34);
    if (vf.upem < 16 || vf.nglyphs == 0 || vf.nhm == 0) return 0;

    uint32_t ncm = vf_u16(vf.cmap + 2);
    uint32_t best = 0;
    int best_score = -1;
    for (uint32_t i = 0; i < ncm; ++i) {
        uint32_t rec = vf.cmap + 4 + i * 8;
        uint32_t pid = vf_u16(rec), eid = vf_u16(rec + 2);
        uint32_t sub = vf.cmap + vf_u32(rec + 4);
        if (vf_u16(sub) != 4) continue;
        int score = (pid == 3 && eid == 1) ? 3 : (pid == 0 ? 2 : 1);
        if (score > best_score) { best = sub; best_score = score; }
    }
    if (!best) return 0;
    vf.cmap_off = best;
    vf.ready = 1;
    for (uint32_t c = 0; c < 256; ++c) vf_ascii[c] = (uint16_t)vf_lookup(c);
    vr_set_contrast(vf_contrast);
    return 1;
}

VF_FN uint32_t vf_adv(uint32_t gid)
{
    uint32_t i = gid < vf.nhm ? gid : vf.nhm - 1;
    return vf_u16(vf.hmtx + i * 4);
}

/* ---- carga de contornos (simples y compuestos) ---- */
VF_FN int vf_load(uint32_t gid, int depth)
{
    if (gid >= vf.nglyphs || depth > 4) return 0;
    uint32_t o0, o1;
    if (vf.loc_fmt == 0) {
        o0 = vf_u16(vf.loca + gid * 2) * 2u;
        o1 = vf_u16(vf.loca + gid * 2 + 2) * 2u;
    } else {
        o0 = vf_u32(vf.loca + gid * 4);
        o1 = vf_u32(vf.loca + gid * 4 + 4);
    }
    if (o1 <= o0) return 1;                    /* glifo vacio (espacio) */
    uint32_t g = vf.glyf + o0;
    int nc = (int)vf_s16(g);

    if (nc >= 0) {
        if (nc == 0) return 1;
        if (vf_nc + nc > VF_MAX_CONT) return 0;
        uint32_t p = g + 10;
        int base = vf_np, npt = 0;
        for (int i = 0; i < nc; ++i) {
            int e = (int)vf_u16(p + (uint32_t)i * 2);
            vf_end[vf_nc + i] = (uint16_t)(base + e);
            if (e + 1 > npt) npt = e + 1;
        }
        p += (uint32_t)nc * 2;
        if (base + npt > VF_MAX_PTS) return 0;
        p += 2 + vf_u16(p);                    /* saltar instrucciones */

        static uint8_t flags[VF_MAX_PTS];
        for (int i = 0; i < npt; ) {
            uint8_t f = (uint8_t)vf_u8(p++);
            flags[i++] = f;
            if (f & 8) {
                int rep = (int)vf_u8(p++);
                while (rep-- > 0 && i < npt) flags[i++] = f;
            }
        }
        int32_t v = 0;
        for (int i = 0; i < npt; ++i) {
            uint8_t f = flags[i];
            if (f & 2) { int32_t d = (int32_t)vf_u8(p++); v += (f & 16) ? d : -d; }
            else if (!(f & 16)) { v += vf_s16(p); p += 2; }
            vf_px[base + i] = v;
            vf_on[base + i] = (uint8_t)(f & 1);
        }
        v = 0;
        for (int i = 0; i < npt; ++i) {
            uint8_t f = flags[i];
            if (f & 4) { int32_t d = (int32_t)vf_u8(p++); v += (f & 32) ? d : -d; }
            else if (!(f & 32)) { v += vf_s16(p); p += 2; }
            vf_py[base + i] = v;
        }
        vf_nc += nc;
        vf_np += npt;
        return 1;
    }

    /* glifo compuesto */
    uint32_t p = g + 10;
    for (int comp = 0; comp < 16; ++comp) {
        uint32_t fl = vf_u16(p);
        uint32_t gi = vf_u16(p + 2);
        p += 4;
        int32_t dx, dy;
        if (fl & 1) { dx = vf_s16(p); dy = vf_s16(p + 2); p += 4; }
        else { dx = (int8_t)vf_u8(p); dy = (int8_t)vf_u8(p + 1); p += 2; }
        if (!(fl & 2)) { dx = 0; dy = 0; }
        int32_t a = 16384, b = 0, c = 0, d = 16384;
        if (fl & 8)         { a = d = vf_s16(p); p += 2; }
        else if (fl & 0x40) { a = vf_s16(p); d = vf_s16(p + 2); p += 4; }
        else if (fl & 0x80) { a = vf_s16(p); b = vf_s16(p + 2); c = vf_s16(p + 4); d = vf_s16(p + 6); p += 8; }
        int start = vf_np;
        if (!vf_load(gi, depth + 1)) return 0;
        for (int i = start; i < vf_np; ++i) {
            int32_t x = vf_px[i], y = vf_py[i];
            vf_px[i] = (x * a + y * c) / 16384 + dx;
            vf_py[i] = (x * b + y * d) / 16384 + dy;
        }
        if (!(fl & 0x20)) break;
    }
    return 1;
}

/* ---- contorno TrueType -> segmentos ---- */
VF_FN void vf_contour(int s, int e)
{
    int n = e - s + 1;
    if (n < 2) return;
    const int32_t *X = vf_px + s;
    const int32_t *Y = vf_py + s;
    const uint8_t *O = vf_on + s;
    int32_t sx, sy;
    int i0, i1;
    if (O[0])          { sx = X[0];     sy = Y[0];     i0 = 1; i1 = n; }
    else if (O[n - 1]) { sx = X[n - 1]; sy = Y[n - 1]; i0 = 0; i1 = n - 1; }
    else               { sx = (X[0] + X[n - 1]) / 2; sy = (Y[0] + Y[n - 1]) / 2; i0 = 0; i1 = n; }

    int32_t cx = sx, cy = sy, kx = 0, ky = 0;
    int have = 0;
    for (int i = i0; i < i1; ++i) {
        if (O[i]) {
            if (have) { vr_quad(cx, cy, kx, ky, X[i], Y[i]); have = 0; }
            else vr_line(cx, cy, X[i], Y[i]);
            cx = X[i]; cy = Y[i];
        } else {
            if (have) {
                int32_t mx = (kx + X[i]) / 2, my = (ky + Y[i]) / 2;
                vr_quad(cx, cy, kx, ky, mx, my);
                cx = mx; cy = my;
            }
            kx = X[i]; ky = Y[i];
            have = 1;
        }
    }
    if (have) vr_quad(cx, cy, kx, ky, sx, sy);
    else vr_line(cx, cy, sx, sy);
}

/* ---- cache de glifos (arena estatica, sin kmalloc) ---- */
struct vf_ce { uint32_t key; int16_t bx, by; uint8_t w, h; uint8_t *cov; };
static struct vf_ce vf_cache[VF_CACHE_N];
static uint8_t  vf_arena[196608];
static uint32_t vf_used = 0;

VF_FN void vf_cache_flush(void)
{
    for (int i = 0; i < VF_CACHE_N; ++i) {
        vf_cache[i].key = 0; vf_cache[i].cov = 0;
        vf_cache[i].w = 0;   vf_cache[i].h = 0;
        vf_cache[i].bx = 0;  vf_cache[i].by = 0;
    }
    vf_used = 0;
}

VF_FN uint8_t *vf_alloc(uint32_t n)
{
    if (n > sizeof(vf_arena)) return 0;
    if (vf_used + n > sizeof(vf_arena)) vf_cache_flush();
    uint8_t *p = vf_arena + vf_used;
    vf_used += n;
    return p;
}

VF_FN struct vf_ce *vf_get(uint32_t gid, int px)
{
    uint32_t key = (gid << 8) | (uint32_t)px;
    struct vf_ce *ce = &vf_cache[(key * 2654435761u) >> 23];
    if (ce->key == key) return ce;
    ce->key = key; ce->cov = 0; ce->w = 0; ce->h = 0; ce->bx = 0; ce->by = 0;

    vf_np = 0; vf_nc = 0;
    if (!vf_load(gid, 0) || vf_np == 0 || vf_nc == 0) return ce;

    int64_t s256 = (int64_t)px * 256;
    int32_t minx = 0x7FFFFFFF, miny = 0x7FFFFFFF, maxx = -0x7FFFFFFF, maxy = -0x7FFFFFFF;
    for (int i = 0; i < vf_np; ++i) {
        int32_t X = (int32_t)(((int64_t)vf_px[i] * s256) / (int64_t)vf.upem);
        int32_t Y = -(int32_t)(((int64_t)vf_py[i] * s256) / (int64_t)vf.upem);
        vf_px[i] = X; vf_py[i] = Y;
        if (X < minx) minx = X;
        if (X > maxx) maxx = X;
        if (Y < miny) miny = Y;
        if (Y > maxy) maxy = Y;
    }
    int32_t x0 = minx >> 8, y0 = miny >> 8;
    int32_t x1 = (maxx + 255) >> 8, y1 = (maxy + 255) >> 8;
    int w = x1 - x0, h = y1 - y0;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > VR_MAX_DIM || h > VR_MAX_DIM) return ce;

    for (int i = 0; i < vf_np; ++i) { vf_px[i] -= x0 * 256; vf_py[i] -= y0 * 256; }

    vr_reset();
    int s = 0;
    for (int c = 0; c < vf_nc; ++c) {
        int e = (int)vf_end[c];
        if (e >= s && e < vf_np) vf_contour(s, e);
        s = e + 1;
    }

    uint8_t *cov = vf_alloc((uint32_t)(w * h));
    if (!cov) return ce;
    ce->key = key;                 /* vf_alloc pudo vaciar la cache */
    vr_fill(cov, w, h);
    for (int i = 0; i < w * h; ++i) cov[i] = vr_curve[cov[i]];
    ce->cov = cov; ce->w = (uint8_t)w; ce->h = (uint8_t)h;
    ce->bx = (int16_t)x0; ce->by = (int16_t)y0;
    return ce;
}

/* Dibuja un codepoint en una celda de 8*scale px de ancho (rejilla del SO).
 * La linea base cae en y + 8*scale. */
VF_FN void vfont_draw_cp(int32_t x, int32_t y, uint32_t cp, uint32_t fg, int scale)
{
    if (!vf.ready || cp < 32) return;
    uint32_t gid = (cp < 256) ? vf_ascii[cp] : vf_lookup(cp);
    if (gid == 0) gid = vf_ascii[(uint8_t)'?'];
    if (gid == 0) return;

    int em = (12 * scale * vf_pct + 50) / 100;
    if (em < 4) em = 4;
    if (em > VF_MAX_EM) em = VF_MAX_EM;

    struct vf_ce *ce = vf_get(gid, em);
    if (!ce || !ce->cov) return;

    int32_t adv = (int32_t)((vf_adv(gid) * (uint32_t)em + vf.upem / 2) / vf.upem);
    int32_t ox = (8 * scale - adv) / 2;

    uint32_t a = fg >> 24;
    if (a == 0) a = 255;
    fb_draw_mask8(x + ox + ce->bx, y + 8 * scale + ce->by, ce->cov, ce->w, ce->h,
                  fg & 0x00FFFFFFu, a);
}

#endif
