#include <stdint.h>

#include "console.h"
#include "mem.h"
#include "srcfs.h"
#include "virtio_blk.h"

/*
 * Formato (lo escribe ejecutar.py::pack_sources):
 *   LBA 256..263  indice, entradas de 64 bytes. La entrada 0 es la cabecera:
 *                 "SRCFSv01" + u32 count (little-endian).
 *                 Entradas 1..count: char name[48]; u32 lba; u32 size; pad[8].
 *   LBA 264..     datos, cada archivo alineado a sector.
 */
#define SRCFS_LBA            8192
#define SRCFS_INDEX_SECTORS  8
#define SRCFS_ENTRY_SIZE     64
#define SRCFS_NAME_MAX       48
#define SRCFS_PAGE           768
#define SRCFS_LINE_MAX       160

static uint8_t srcfs_index[SRCFS_INDEX_SECTORS * 512];
static uint8_t srcfs_sec[512];


static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


static void out_char(char *out, uint32_t max, uint32_t *pos, char c)
{
    if (*pos + 1 < max) {
        out[*pos] = c;
        out[*pos + 1] = '\0';
        (*pos)++;
    }
}


static void out_str(char *out, uint32_t max, uint32_t *pos, const char *s)
{
    while (*s) {
        out_char(out, max, pos, *s++);
    }
}


static void out_dec(char *out, uint32_t max, uint32_t *pos, uint32_t v)
{
    char tmp[10];
    int n = 0;

    if (v == 0) {
        out_char(out, max, pos, '0');
        return;
    }

    while (v) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }

    while (n) {
        out_char(out, max, pos, tmp[--n]);
    }
}


static void console_dump(const char *s)
{
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;

        if (c == '\n' || (c >= 32 && c < 127)) {
            kputc((char)c);
        } else if (c != '\r') {
            kputc('.');
        }
    }
}


static int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }

    return *a == *b;
}


static const char *strip_src(const char *n)
{
    if (n[0] == '/') n++;
    if (n[0] == 's' && n[1] == 'r' && n[2] == 'c' && n[3] == '/') n += 4;
    return n;
}


static int word_is(const char *s, const char *w)
{
    while (*w) {
        if (*s != *w) return 0;
        s++;
        w++;
    }

    return *s == '\0' || *s == ' ';
}


/* Carga el indice. Devuelve el numero de archivos o <0 si no hay. */
static int srcfs_load_index(void)
{
    for (int i = 0; i < SRCFS_INDEX_SECTORS; ++i) {
        if (virtio_blk_read((uint64_t)(SRCFS_LBA + i), srcfs_index + i * 512) != 0) {
            return -1;
        }
    }

    if (memcmp(srcfs_index, "SRCFSv01", 8) != 0) {
        return -2;
    }

    uint32_t count = le32(srcfs_index + 8);
    uint32_t cap = SRCFS_INDEX_SECTORS * 512 / SRCFS_ENTRY_SIZE - 1;

    return (int)(count > cap ? cap : count);
}


static const uint8_t *find_entry(int count, const char *name)
{
    name = strip_src(name);
    uint32_t qlen = 0;
    while (name[qlen]) qlen++;

    for (int i = 1; i <= count; ++i) {
        const uint8_t *e = srcfs_index + i * SRCFS_ENTRY_SIZE;
        const char *ename = (const char *)e;
        if (name_eq(ename, name)) return e;

        uint32_t elen = 0;
        while (ename[elen]) elen++;
        if (elen > qlen && ename[elen - qlen - 1] == '/') {
            if (name_eq(ename + (elen - qlen), name)) return e;
        }
    }

    return 0;
}


static int no_index(char *out, uint32_t max, int count)
{
    uint32_t pos = 0;

    out_str(out, max, &pos,
            count == -1 ? "Error de I/O leyendo el indice de fuentes."
                        : "Error: no hay fuentes en el disco (arranca con ejecutar.py).");
    return -1;
}


int srcfs_ls(char *out, uint32_t max)
{
    uint32_t pos = 0;

    if (max) out[0] = '\0';

    int count = srcfs_load_index();

    if (count < 0) {
        return no_index(out, max, count);
    }

    out_str(out, max, &pos, "Archivos fuente (nombre bytes):\n");

    for (int i = 1; i <= count; ++i) {
        const uint8_t *e = srcfs_index + i * SRCFS_ENTRY_SIZE;

        out_str(out, max, &pos, (const char *)e);
        out_char(out, max, &pos, ' ');
        out_dec(out, max, &pos, le32(e + 52));
        out_char(out, max, &pos, '\n');
    }

    return count;
}


int srcfs_cat(const char *name, uint32_t offset, char *out, uint32_t max)
{
    uint32_t pos = 0;

    if (max) out[0] = '\0';

    int count = srcfs_load_index();

    if (count < 0) {
        return no_index(out, max, count);
    }

    const uint8_t *e = find_entry(count, name);

    if (!e) {
        out_str(out, max, &pos, "Error: archivo fuente no encontrado: ");
        out_str(out, max, &pos, strip_src(name));
        out_str(out, max, &pos, " (usa src_ls)");
        return -1;
    }

    uint32_t lba = le32(e + 48);
    uint32_t size = le32(e + 52);

    if (offset >= size) {
        out_str(out, max, &pos, "Fin del archivo (tamano ");
        out_dec(out, max, &pos, size);
        out_str(out, max, &pos, " bytes).");
        return 0;
    }

    uint32_t n = size - offset;

    if (n > SRCFS_PAGE) n = SRCFS_PAGE;
    if (max > 160 && n > max - 160) n = max - 160;

    uint32_t done = 0;

    while (done < n) {
        uint32_t p = offset + done;

        if (virtio_blk_read((uint64_t)(lba + p / 512), srcfs_sec) != 0) {
            out_str(out, max, &pos, "\nError de I/O leyendo el disco.");
            return -1;
        }

        uint32_t within = p % 512;
        uint32_t take = 512 - within;

        if (take > n - done) take = n - done;

        for (uint32_t k = 0; k < take; ++k) {
            char c = (char)srcfs_sec[within + k];

            if (c != '\r') {
                out_char(out, max, &pos, c);
            }
        }

        done += take;
    }

    if (offset + n < size) {
        out_str(out, max, &pos, "\n--- siguiente OFFSET: ");
        out_dec(out, max, &pos, offset + n);
        out_str(out, max, &pos, " (total ");
        out_dec(out, max, &pos, size);
        out_str(out, max, &pos, " bytes) ---");
    } else {
        out_str(out, max, &pos, "\n--- fin del archivo (total ");
        out_dec(out, max, &pos, size);
        out_str(out, max, &pos, " bytes) ---");
    }

    return (int)n;
}


static int contains(const char *hay, const char *needle)
{
    if (!*needle) return 1;

    for (; *hay; ++hay) {
        const char *h = hay;
        const char *n = needle;

        while (*h && *n && *h == *n) {
            h++;
            n++;
        }

        if (!*n) return 1;
    }

    return 0;
}


int srcfs_grep(const char *name, const char *pat, char *out, uint32_t max)
{
    uint32_t pos = 0;

    if (max) out[0] = '\0';

    int count = srcfs_load_index();

    if (count < 0) {
        return no_index(out, max, count);
    }

    const uint8_t *e = find_entry(count, name);

    if (!e) {
        out_str(out, max, &pos, "Error: archivo fuente no encontrado: ");
        out_str(out, max, &pos, strip_src(name));
        return -1;
    }

    /* Limpiar espacios iniciales y comillas envolventes (' o ") */
    while (*pat == ' ' || *pat == '\t') pat++;

    char clean_pat[SRCFS_LINE_MAX + 1];
    uint32_t pat_len = 0;
    char quote = 0;
    if (*pat == '\'' || *pat == '"') {
        quote = *pat++;
    }

    while (*pat && pat_len < sizeof(clean_pat) - 1) {
        if (quote && *pat == quote) {
            break;
        }
        clean_pat[pat_len++] = *pat++;
    }
    clean_pat[pat_len] = '\0';

    if (!quote) {
        while (pat_len > 0 && (clean_pat[pat_len - 1] == ' ' || clean_pat[pat_len - 1] == '\t' ||
                               clean_pat[pat_len - 1] == '\r' || clean_pat[pat_len - 1] == '\n')) {
            clean_pat[--pat_len] = '\0';
        }
    }
    pat = clean_pat;

    if (!*pat) {
        out_str(out, max, &pos, "Uso: src_grep archivo.c texto");
        return -1;
    }

    uint32_t lba = le32(e + 48);
    uint32_t size = le32(e + 52);

    char line[SRCFS_LINE_MAX + 1];
    uint32_t ll = 0;
    uint32_t line_start = 0;
    int hits = 0;
    int truncated = 0;

    for (uint32_t p = 0; p < size; ++p) {

        if (p % 512 == 0) {
            if (virtio_blk_read((uint64_t)(lba + p / 512), srcfs_sec) != 0) {
                out_str(out, max, &pos, "\nError de I/O leyendo el disco.");
                return -1;
            }
        }

        char c = (char)srcfs_sec[p % 512];

        if (c != '\n' && c != '\r' && ll < SRCFS_LINE_MAX) {
            line[ll++] = c;
        }

        if (c == '\n' || p + 1 == size) {

            line[ll] = '\0';

            if (contains(line, pat)) {

                if (max > 100 && pos + ll + 16 < max - 100) {
                    out_dec(out, max, &pos, line_start);
                    out_str(out, max, &pos, ": ");
                    out_str(out, max, &pos, line);
                    out_char(out, max, &pos, '\n');
                    hits++;
                } else {
                    truncated = 1;
                }
            }

            ll = 0;
            line_start = p + 1;
        }
    }

    if (hits == 0 && !truncated) {
        out_str(out, max, &pos, "Sin coincidencias.");
    }

    if (truncated) {
        out_str(out, max, &pos, "... (hay mas coincidencias; usa un texto mas especifico)");
    }

    return hits;
}


int src_tool(const char *cmd, char *out, uint32_t max)
{
    while (*cmd == ' ') cmd++;

    if (word_is(cmd, "src_ls")) {
        srcfs_ls(out, max);
        console_dump(out);
        kputc('\n');
        return 1;
    }

    if (word_is(cmd, "src_cat") || word_is(cmd, "src_grep")) {

        int is_cat = word_is(cmd, "src_cat");
        const char *p = cmd + (is_cat ? 7 : 8);

        while (*p == ' ') p++;

        char name[SRCFS_NAME_MAX];
        uint32_t n = 0;

        if (*p == '\'' || *p == '"') {
            char q = *p++;
            while (*p && *p != q && n < sizeof(name) - 1) {
                name[n++] = *p++;
            }
            if (*p == q) p++;
        } else {
            while (*p && *p != ' ' && n < sizeof(name) - 1) {
                name[n++] = *p++;
            }
        }

        name[n] = '\0';

        while (*p == ' ') p++;

        if (n == 0) {
            uint32_t pos = 0;

            if (max) out[0] = '\0';
            out_str(out, max, &pos,
                    is_cat ? "Uso: src_cat archivo.c [offset]"
                           : "Uso: src_grep archivo.c texto");

        } else if (is_cat) {

            uint32_t off = 0;

            while (*p >= '0' && *p <= '9') {
                off = off * 10 + (uint32_t)(*p - '0');
                p++;
            }

            srcfs_cat(name, off, out, max);

        } else {
            srcfs_grep(name, p, out, max);
        }

        console_dump(out);
        kputc('\n');
        return 1;
    }

    return 0;
}
