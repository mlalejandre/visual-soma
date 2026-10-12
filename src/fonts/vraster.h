#ifndef SOMA_VRASTER_H
#define SOMA_VRASTER_H
/* SOMA_VECTOR_FONTS_PATCH
 * Rasterizador de contornos rellenos con antialiasing.
 * Todo en aritmetica entera. Coordenadas en punto fijo 24.8 (1/256 px).
 * Regla de relleno: winding distinto de cero.
 * 8 sub-muestras verticales por pixel + cobertura horizontal exacta.
 */
#include <stdint.h>
#include <stddef.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#define VR_FN static __attribute__((unused))
#define VR_MAX_EDGES   6144
#define VR_MAX_DIM     192
#define VR_SUBSAMPLES  8
#define VR_MAX_XINGS   512

struct vr_edge { int32_t x0, y0, x1, y1; };

static struct vr_edge vr_edges[VR_MAX_EDGES];
static int     vr_edge_count = 0;
static uint8_t vr_curve[256];

VR_FN void vr_reset(void) { vr_edge_count = 0; }

VR_FN void vr_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (y0 == y1 || vr_edge_count >= VR_MAX_EDGES) return;
    struct vr_edge *e = &vr_edges[vr_edge_count++];
    e->x0 = x0; e->y0 = y0; e->x1 = x1; e->y1 = y1;
}

VR_FN uint32_t vr_isqrt(uint32_t num)
{
    uint32_t res = 0, bit = 1u << 30;
    while (bit > num) bit >>= 2;
    while (bit) {
        if (num >= res + bit) { num -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}

/* Curva de Bezier cuadratica -> segmentos (subdivision adaptativa) */
VR_FN void vr_quad(int32_t x0, int32_t y0, int32_t cx, int32_t cy, int32_t x1, int32_t y1)
{
    int32_t ax = x0 - 2 * cx + x1;
    int32_t ay = y0 - 2 * cy + y1;
    if (ax < 0) ax = -ax;
    if (ay < 0) ay = -ay;
    int n = 1 + (int)vr_isqrt((uint32_t)(ax + ay) / 48u);
    if (n > 24) n = 24;
    int64_t nn = (int64_t)n * n;
    int32_t px = x0, py = y0;
    for (int i = 1; i <= n; ++i) {
        int32_t nx, ny;
        if (i == n) { nx = x1; ny = y1; }
        else {
            int64_t a = (int64_t)(n - i) * (n - i);
            int64_t b = 2LL * i * (n - i);
            int64_t c = (int64_t)i * i;
            nx = (int32_t)((a * x0 + b * cx + c * x1) / nn);
            ny = (int32_t)((a * y0 + b * cy + c * y1) / nn);
        }
        vr_line(px, py, nx, ny);
        px = nx; py = ny;
    }
}

/* Anade cobertura horizontal exacta de [xa,xb) (1/256 px) a la fila acumulada */
VR_FN void vr_span(uint32_t *acc, int w, int32_t xa, int32_t xb)
{
    int32_t lim = w * 256;
    if (xa < 0) xa = 0;
    if (xb > lim) xb = lim;
    if (xb <= xa) return;
    int pa = xa >> 8, pb = xb >> 8;
    if (pa == pb) { acc[pa] += (uint32_t)(xb - xa); return; }
    acc[pa] += (uint32_t)(256 - (xa & 255));
    for (int p = pa + 1; p < pb; ++p) acc[p] += 256u;
    if (pb < w) acc[pb] += (uint32_t)(xb & 255);
}

/* Rellena los bordes acumulados en un mapa de cobertura w*h de 8 bits */
VR_FN void vr_fill(uint8_t *out, int w, int h)
{
    static uint32_t acc[VR_MAX_DIM];
    static int32_t  xs[VR_MAX_XINGS];
    static int8_t   ds[VR_MAX_XINGS];
    const uint32_t full = 256u * VR_SUBSAMPLES;

    for (int row = 0; row < h; ++row) {
        for (int i = 0; i < w; ++i) acc[i] = 0;
        for (int k = 0; k < VR_SUBSAMPLES; ++k) {
            int32_t ys = row * 256 + (k * 256 + 128) / VR_SUBSAMPLES;
            int nx = 0;
            for (int ei = 0; ei < vr_edge_count; ++ei) {
                const struct vr_edge *e = &vr_edges[ei];
                int32_t xa = e->x0, ya = e->y0, xb = e->x1, yb = e->y1;
                int8_t dir = 1;
                if (ya > yb) {
                    int32_t t;
                    t = xa; xa = xb; xb = t;
                    t = ya; ya = yb; yb = t;
                    dir = -1;
                }
                if (ys < ya || ys >= yb) continue;
                if (nx >= VR_MAX_XINGS) continue;
                int32_t x = xa + (int32_t)(((int64_t)(ys - ya) * (xb - xa)) / (yb - ya));
                int j = nx++;
                while (j > 0 && xs[j - 1] > x) { xs[j] = xs[j - 1]; ds[j] = ds[j - 1]; --j; }
                xs[j] = x; ds[j] = dir;
            }
            int wind = 0;
            int32_t sx = 0;
            for (int j = 0; j < nx; ++j) {
                int prev = wind;
                wind += ds[j];
                if (prev == 0 && wind != 0) sx = xs[j];
                else if (prev != 0 && wind == 0) vr_span(acc, w, sx, xs[j]);
            }
        }
        for (int i = 0; i < w; ++i) {
            uint32_t v = (acc[i] * 255u + full / 2u) / full;
            out[row * w + i] = (uint8_t)(v > 255u ? 255u : v);
        }
    }
}

/* Curva de contraste: aclara las coberturas parciales para que el texto
 * fino no se vea palido sobre fondos oscuros. pct 0 = lineal, 100 = maximo. */
VR_FN void vr_set_contrast(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    for (int i = 0; i < 256; ++i) {
        int inv = 255 - i;
        int boosted = 255 - (inv * inv + 127) / 255;
        vr_curve[i] = (uint8_t)(i + ((boosted - i) * pct) / 100);
    }
}

#endif
