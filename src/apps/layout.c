#include "layout.h"
#include "fb.h"
#include "gui_font.h"
#include "console.h"
#include "mem.h"
#include "agent.h"
#include "fs.h"

/* ------------------------------------------------------------------ */
/* Formato #SOMA-LAYOUT v1 (pensado para que el LLM / maquetador emita) */
/*
 * #SOMA-LAYOUT v1
 * TITLE: Título del documento
 * SUBTITLE: Subtítulo opcional
 * FONT: classic|modern|bold|...   (nombre de fuente SOMA)
 * ALIGN: left|center
 * MARGIN: 20
 * ---
 * H1: Sección principal
 * H2: Subsección
 * H3: Apartado
 * P: Párrafo de texto normal. Puede ser largo.
 * QUOTE: Cita o destacado
 * HR
 * FOOTER: pie de página
 * LIST: elemento de lista
 */
/* ------------------------------------------------------------------ */

#define LY_SRC_MAX   12288
#define LY_PROMPT_MAX  256
#define LY_LINE_MAX    160
#define LY_MAX_BLOCKS  96

enum ly_block_kind {
    LY_H1 = 0,
    LY_H2,
    LY_H3,
    LY_P,
    LY_QUOTE,
    LY_HR,
    LY_LIST,
    LY_FOOTER
};

struct ly_block {
    enum ly_block_kind kind;
    char text[LY_LINE_MAX];
};

static char ly_src[LY_SRC_MAX];
static uint32_t ly_src_len = 0;

static char ly_title[96];
static char ly_subtitle[96];
static char ly_font_name[32];
static int  ly_align_center = 0;
static int  ly_margin = 20;
static int  ly_font_id = 1; /* modern por defecto */

static struct ly_block ly_blocks[LY_MAX_BLOCKS];
static int ly_block_count = 0;

static char ly_prompt[LY_PROMPT_MAX];
static uint32_t ly_prompt_len = 0;
static int ly_prompt_focus = 1;
static int ly_scroll = 0;
static char ly_status[80];

static const char SAMPLE_DOC[] =
    "#SOMA-LAYOUT v1\n"
    "TITLE: La Monarquia Espanola\n"
    "SUBTITLE: Documento profesional de ejemplo · SOMA OS\n"
    "FONT: modern\n"
    "ALIGN: left\n"
    "MARGIN: 24\n"
    "---\n"
    "H1: Introduccion\n"
    "P: La monarquia espanola es una institucion historica con raices en la Edad Media. "
    "Hoy funciona como monarquia parlamentaria: el Rey es jefe de Estado y simboliza la unidad de la nacion.\n"
    "H2: Marco constitucional\n"
    "P: La Constitucion de 1978 define el papel de la Corona, la sucesion y las funciones "
    "representativas. El poder ejecutivo reside en el Gobierno elegido democraticamente.\n"
    "H2: Funciones de la Corona\n"
    "LIST: Representacion del Estado en actos oficiales y diplomacia\n"
    "LIST: Sancion y promulgacion de las leyes\n"
    "LIST: Propuesta del candidato a Presidente del Gobierno\n"
    "LIST: Mando supremo de las Fuerzas Armadas (simbolico)\n"
    "H3: Nota historica\n"
    "QUOTE: La transicion democratica consolidó una Corona integrada en el Estado de Derecho.\n"
    "HR\n"
    "P: Este documento es una demostracion del motor de maquetacion de SOMA. "
    "El subagente 'maquetador' generara estructuras similares a partir de informacion del buscador.\n"
    "FOOTER: SOMA OS · Maquetador de documentos · v0.1\n";

/* ---- utilidades ---- */

static void ly_strcpy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    if (!src) { dst[0] = '\0'; return; }
    while (src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int ly_startswith(const char *s, const char *pfx)
{
    while (*pfx) {
        if (*s != *pfx) return 0;
        s++; pfx++;
    }
    return 1;
}

static void ly_trim_inplace(char *s)
{
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) {
        char *d = s;
        while (*start) *d++ = *start++;
        *d = '\0';
    }
    uint32_t n = 0;
    while (s[n]) n++;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static int ly_streq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* ---- parser del formato ---- */

/* Paso 3 fix: prototipos antes de ly_parse */
static void ly_utf8_to_ascii_inplace(char *s);
static void ly_strip_list_prefix(char *s);

static void ly_parse(void)
{
    ly_block_count = 0;
    ly_title[0] = '\0';
    ly_subtitle[0] = '\0';
    ly_strcpy(ly_font_name, "modern", sizeof(ly_font_name));
    ly_align_center = 0;
    ly_margin = 20;
    ly_font_id = 1;

    const char *p = ly_src;
    char line[LY_LINE_MAX];
    int in_body = 0;

    while (*p && ly_block_count < LY_MAX_BLOCKS) {
        uint32_t li = 0;
        while (*p && *p != '\n' && li + 1 < LY_LINE_MAX) {
            line[li++] = *p++;
        }
        if (*p == '\n') p++;
        line[li] = '\0';
        ly_trim_inplace(line);
        if (!line[0]) continue;
        if (ly_startswith(line, "#")) continue; /* comentarios / cabecera magica */

        if (!in_body) {
            if (ly_startswith(line, "---")) {
                in_body = 1;
                continue;
            }
            if (ly_startswith(line, "TITLE:")) {
                ly_strcpy(ly_title, line + 6, sizeof(ly_title));
                ly_trim_inplace(ly_title);
                ly_utf8_to_ascii_inplace(ly_title);
                continue;
            }
            if (ly_startswith(line, "SUBTITLE:")) {
                ly_strcpy(ly_subtitle, line + 9, sizeof(ly_subtitle));
                ly_trim_inplace(ly_subtitle);
                ly_utf8_to_ascii_inplace(ly_subtitle);
                continue;
            }
            if (ly_startswith(line, "FONT:")) {
                ly_strcpy(ly_font_name, line + 5, sizeof(ly_font_name));
                ly_trim_inplace(ly_font_name);
                if (!font_set_by_name(ly_font_name))
                    font_set(FONT_MODERN);
                ly_font_id = font_get_current();
                continue;
            }
            if (ly_startswith(line, "ALIGN:")) {
                const char *v = line + 6;
                while (*v == ' ') v++;
                ly_align_center = ly_startswith(v, "center") || ly_startswith(v, "centro");
                continue;
            }
            if (ly_startswith(line, "MARGIN:")) {
                int v = 0;
                const char *c = line + 7;
                while (*c == ' ') c++;
                while (*c >= '0' && *c <= '9') { v = v * 10 + (*c - '0'); c++; }
                if (v >= 8 && v <= 64) ly_margin = v;
                continue;
            }
            /* Si no es meta, tratar como cuerpo */
            in_body = 1;
        }

        struct ly_block *b = &ly_blocks[ly_block_count];
        b->text[0] = '\0';

        if (ly_startswith(line, "H1:")) {
            b->kind = LY_H1;
            ly_strcpy(b->text, line + 3, sizeof(b->text));
        } else if (ly_startswith(line, "H2:")) {
            b->kind = LY_H2;
            ly_strcpy(b->text, line + 3, sizeof(b->text));
        } else if (ly_startswith(line, "H3:")) {
            b->kind = LY_H3;
            ly_strcpy(b->text, line + 3, sizeof(b->text));
        } else if (ly_startswith(line, "P:")) {
            b->kind = LY_P;
            ly_strcpy(b->text, line + 2, sizeof(b->text));
        } else if (ly_startswith(line, "QUOTE:")) {
            b->kind = LY_QUOTE;
            ly_strcpy(b->text, line + 6, sizeof(b->text));
        } else if (ly_startswith(line, "LIST:")) {
            b->kind = LY_LIST;
            ly_strcpy(b->text, line + 5, sizeof(b->text));
        } else if (ly_startswith(line, "FOOTER:")) {
            b->kind = LY_FOOTER;
            ly_strcpy(b->text, line + 7, sizeof(b->text));
        } else if (ly_streq_ci(line, "HR") || ly_startswith(line, "HR")) {
            b->kind = LY_HR;
            b->text[0] = '\0';
        } else {
            b->kind = LY_P;
            ly_strcpy(b->text, line, sizeof(b->text));
        }
        ly_trim_inplace(b->text);
        ly_utf8_to_ascii_inplace(b->text);
        if (b->kind == LY_LIST) ly_strip_list_prefix(b->text);
        ly_block_count++;
    }

    font_set(ly_font_id);
}



/* ---- Paso 3: normalización ASCII + limpieza de LIST ---- */

static void ly_utf8_to_ascii_inplace(char *s)
{
    if (!s) return;
    char *d = s;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p < 0x80) {
            *d++ = (char)*p++;
            continue;
        }
        /* Secuencias UTF-8 comunes en español → ASCII */
        if (p[0] == 0xC3 && p[1]) {
            unsigned char c = p[1];
            char rep = '?';
            if (c == 0xA1 || c == 0x81) rep = 'a';      /* á Á */
            else if (c == 0xA9 || c == 0x89) rep = 'e'; /* é É */
            else if (c == 0xAD || c == 0x8D) rep = 'i'; /* í Í */
            else if (c == 0xB3 || c == 0x93) rep = 'o'; /* ó Ó */
            else if (c == 0xBA || c == 0x9A) rep = 'u'; /* ú Ú */
            else if (c == 0xB1 || c == 0x91) rep = 'n'; /* ñ Ñ */
            else if (c == 0xBC || c == 0x9C) rep = 'u'; /* ü Ü */
            else if (c == 0x87 || c == 0xA7) rep = 'c'; /* Ç ç */
            *d++ = rep;
            p += 2;
            continue;
        }
        if (p[0] == 0xE2 && p[1] == 0x80 && p[2]) {
            /* comillas tipográficas / guiones */
            unsigned char c = p[2];
            if (c == 0x9C || c == 0x9D || c == 0x98 || c == 0x99) *d++ = '"';
            else if (c == 0x93 || c == 0x94) *d++ = '-';
            else if (c == 0xA6) { *d++ = '.'; *d++ = '.'; *d++ = '.'; }
            else *d++ = ' ';
            p += 3;
            continue;
        }
        /* Multibyte desconocido: saltar 1-4 bytes */
        if ((*p & 0xF8) == 0xF0) p += 4;
        else if ((*p & 0xF0) == 0xE0) p += 3;
        else if ((*p & 0xE0) == 0xC0) p += 2;
        else p++;
        *d++ = '?';
    }
    *d = '\0';
}

static void ly_strip_list_prefix(char *s)
{
    if (!s) return;
    while (*s == ' ' || *s == '\t') {
        char *d = s;
        while (d[1]) { *d = d[1]; d++; }
        *d = '\0';
    }
    if (s[0] == '-' && (s[1] == ' ' || s[1] == '\t')) {
        char *d = s;
        const char *p = s + 2;
        while (*p) *d++ = *p++;
        *d = '\0';
    }
}


/* ---- Paso 2: invocación del subagente maquetador ---- */

static int ly_find_substr(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return -1;
    for (int i = 0; hay[i]; ++i) {
        int j = 0;
        while (needle[j] && hay[i + j] && hay[i + j] == needle[j]) j++;
        if (!needle[j]) return i;
    }
    return -1;
}

static int ly_extract_layout(const char *src, char *dst, uint32_t max)
{
    if (!src || !dst || max < 16) return 0;
    dst[0] = '\0';

    int start = ly_find_substr(src, "#SOMA-LAYOUT");
    if (start < 0) start = ly_find_substr(src, "TITLE:");
    if (start < 0) start = ly_find_substr(src, "H1:");
    if (start < 0) start = ly_find_substr(src, "H2:");
    if (start < 0) return 0;

    const char *p = src + start;
    uint32_t n = 0;
    while (*p && n + 1 < max) {
        if (p[0] == '`' && p[1] == '`' && p[2] == '`') break;
        dst[n++] = *p++;
    }
    dst[n] = '\0';
    if (n < 20) return 0;
    return 1;
}

static void ly_append_blackboard(char *mission, uint32_t max, uint32_t *pos)
{
    char bb[1024];
    int n = vfs_read("/agent/blackboard.txt", bb, sizeof(bb) - 1);
    if (n <= 0) return;
    bb[n] = '\0';
    if (n > 400) { bb[400] = '\0'; n = 400; }
    const char *hdr = "\n\nNotas de investigacion (blackboard):\n";
    for (const char *h = hdr; *h && *pos + 1 < max; ++h) mission[(*pos)++] = *h;
    for (int i = 0; i < n && *pos + 1 < max; ++i) mission[(*pos)++] = bb[i];
    mission[*pos] = '\0';
}

static void ly_do_generate(void)
{
    if (ly_prompt_len == 0) {
        ly_strcpy(ly_status, "Escribe un tema en el campo superior", sizeof(ly_status));
        return;
    }

    ly_strcpy(ly_status, "Generando documento... (LLM en curso, espere)", sizeof(ly_status));

    char mission[768];
    uint32_t mp = 0;
    const char *pre =
        "Maquetar un documento profesional sobre el siguiente tema. "
        "Responde UNICAMENTE con el documento en formato #SOMA-LAYOUT v1 "
        "(empieza por la linea #SOMA-LAYOUT v1, meta TITLE/SUBTITLE/FONT/ALIGN/MARGIN, "
        "separador ---, y bloques H1/H2/H3/P/QUOTE/LIST/HR/FOOTER). "
        "Tema: ";
    while (*pre && mp + 1 < sizeof(mission)) mission[mp++] = *pre++;
    for (uint32_t i = 0; i < ly_prompt_len && mp + 1 < sizeof(mission); ++i)
        mission[mp++] = ly_prompt[i];
    mission[mp] = '\0';

    ly_append_blackboard(mission, sizeof(mission), &mp);

    agent_run("maquetador", mission);

    if (agent_last_response[0] == '\0') {
        ly_strcpy(ly_status, "Sin respuesta del maquetador (LLM vacio o error)", sizeof(ly_status));
        return;
    }

    /* Paso 3: post-process respuesta + posible archivo escrito por el agente */
    char extracted[LY_SRC_MAX];
    int got = ly_extract_layout(agent_last_response, extracted, sizeof(extracted));

    if (!got) {
        /* Si el agente hizo write a un fichero layout del workspace, intentar cargarlo */
        static const char *cands[] = {
            "/workspace/avances_ai_layout.txt",
            "/workspace/layout.txt",
            "/workspace/documento_layout.txt",
            0
        };
        for (int ci = 0; cands[ci]; ++ci) {
            int n = vfs_read(cands[ci], extracted, sizeof(extracted) - 1);
            if (n > 20) {
                extracted[n] = '\0';
                if (ly_find_substr(extracted, "#SOMA-LAYOUT") >= 0 ||
                    ly_find_substr(extracted, "TITLE:") >= 0) {
                    got = 1;
                    break;
                }
            }
        }
    }

    if (got) {
        ly_utf8_to_ascii_inplace(extracted);
        layout_set_source(extracted, 0);
        ly_strcpy(ly_status, "Documento maquetado en el lienzo", sizeof(ly_status));
    } else {
        char fallback[LY_SRC_MAX];
        uint32_t fp = 0;
        const char *hdr =
            "#SOMA-LAYOUT v1\n"
            "TITLE: Resultado del maquetador\n"
            "SUBTITLE: Respuesta sin formato estructurado (fallback)\n"
            "FONT: modern\n"
            "ALIGN: left\n"
            "MARGIN: 20\n"
            "---\n"
            "H1: Contenido recibido\n";
        while (*hdr && fp + 1 < sizeof(fallback)) fallback[fp++] = *hdr++;

        const char *r = agent_last_response;
        while (*r && fp + 8 < sizeof(fallback)) {
            fallback[fp++] = 'P'; fallback[fp++] = ':'; fallback[fp++] = ' ';
            int col = 0;
            while (*r && *r != '\n' && col < 120 && fp + 2 < sizeof(fallback)) {
                fallback[fp++] = *r++;
                col++;
            }
            while (*r == '\n' || *r == '\r') r++;
            fallback[fp++] = '\n';
        }
        const char *ft = "FOOTER: SOMA OS · fallback de maquetacion\n";
        while (*ft && fp + 1 < sizeof(fallback)) fallback[fp++] = *ft++;
        fallback[fp] = '\0';

        layout_set_source(fallback, 0);
        ly_strcpy(ly_status, "Respuesta sin #SOMA-LAYOUT; mostrado en fallback", sizeof(ly_status));
    }
}


void layout_set_source(const char *src, uint32_t len)
{
    if (!src) src = "";
    if (len == 0) {
        while (src[len]) len++;
    }
    if (len >= LY_SRC_MAX) len = LY_SRC_MAX - 1;
    for (uint32_t i = 0; i < len; ++i) ly_src[i] = src[i];
    ly_src[len] = '\0';
    ly_src_len = len;
    ly_utf8_to_ascii_inplace(ly_src);
    ly_scroll = 0;
    ly_parse();
    ly_strcpy(ly_status, "Documento cargado", sizeof(ly_status));
}

void layout_init(void)
{
    ly_prompt[0] = '\0';
    ly_prompt_len = 0;
    ly_prompt_focus = 1;
    ly_scroll = 0;
    ly_strcpy(ly_status, "Listo · escribe un tema y pulsa Generar (Paso 2: agente)", sizeof(ly_status));
    layout_set_source(SAMPLE_DOC, 0);
}

const char *layout_get_prompt(void)
{
    return ly_prompt;
}

/* ---- word-wrap simple (8px por glifo * scale) ---- */

static uint32_t ly_draw_wrapped(
    uint32_t x, uint32_t y, uint32_t max_w,
    const char *text, uint32_t color, int scale, int center)
{
    if (!text || !*text) return y;
    int cw = 8 * scale;
    int lh = 10 * scale;
    uint32_t line_start = 0;
    uint32_t i = 0;
    uint32_t last_space = 0;
    uint32_t col = 0;
    char buf[LY_LINE_MAX];

    while (text[i]) {
        char ch = text[i];
        if (ch == ' ') last_space = i;
        if (ch == '\n' || (col + 1) * (uint32_t)cw > max_w) {
            uint32_t end = (ch == '\n') ? i : ((last_space > line_start) ? last_space : i);
            uint32_t n = 0;
            for (uint32_t k = line_start; k < end && n + 1 < LY_LINE_MAX; ++k)
                buf[n++] = text[k];
            buf[n] = '\0';
            uint32_t tw = n * (uint32_t)cw;
            uint32_t dx = center && tw < max_w ? x + (max_w - tw) / 2 : x;
            font_draw_string(dx, y, buf, color, scale);
            y += (uint32_t)lh + 2;
            if (ch == '\n') {
                line_start = i + 1;
            } else if (last_space > line_start) {
                line_start = last_space + 1;
            } else {
                line_start = i;
            }
            last_space = line_start;
            col = 0;
            i = line_start;
            continue;
        }
        col++;
        i++;
    }
    if (line_start < i) {
        uint32_t n = 0;
        for (uint32_t k = line_start; k < i && n + 1 < LY_LINE_MAX; ++k)
            buf[n++] = text[k];
        buf[n] = '\0';
        uint32_t tw = n * (uint32_t)cw;
        uint32_t dx = center && tw < max_w ? x + (max_w - tw) / 2 : x;
        font_draw_string(dx, y, buf, color, scale);
        y += (uint32_t)lh + 2;
    }
    return y;
}

/* ---- render ---- */

void layout_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor)
{
    (void)show_cursor;
    if (win_w < 200 || win_h < 120) return;

    /* Marco ventana */
    fb_fill_rect(win_x, win_y, win_w, win_h, 0x000F172A);
    fb_draw_rect(win_x, win_y, win_w, win_h, 0x0038BDF8);

    /* Barra título */
    fb_fill_rect(win_x + 1, win_y + 1, win_w - 2, 28, 0x001E293B);
    font_draw_string(win_x + 12, win_y + 8, "MAQUETADOR DE DOCUMENTOS", 0x0038BDF8, 1);
    font_draw_string(win_x + win_w - 90, win_y + 8, "[ Esc ]", 0x0094A3B8, 1);

    /* Barra de prompt */
    uint32_t py = win_y + 32;
    fb_fill_rect(win_x + 1, py, win_w - 2, 36, 0x00161F30);
    font_draw_string(win_x + 10, py + 12, "Tema:", 0x0094A3B8, 1);

    uint32_t field_x = win_x + 56;
    uint32_t field_w = win_w - 180;
    if (field_w < 80) field_w = 80;
    fb_fill_rounded_rect(field_x, py + 6, field_w, 24, 4, 0x000F172A);
    fb_draw_rounded_rect(field_x, py + 6, field_w, 24, 4,
                         ly_prompt_focus ? 0x0038BDF8 : 0x00334155);

    char vis[96];
    uint32_t max_chars = field_w / 8;
    if (max_chars > 90) max_chars = 90;
    uint32_t start = 0;
    if (ly_prompt_len > max_chars) start = ly_prompt_len - max_chars;
    uint32_t vi = 0;
    for (uint32_t i = start; i < ly_prompt_len && vi + 1 < sizeof(vis); ++i)
        vis[vi++] = ly_prompt[i];
    vis[vi] = '\0';
    font_draw_string(field_x + 6, py + 12, vis[0] ? vis : "Describe el documento...", 
                     vis[0] ? 0x00F8FAFC : 0x0064748B, 1);

    /* Botón Generar (placeholder Paso 2) */
    uint32_t bx = win_x + win_w - 110;
    fb_fill_rounded_rect(bx, py + 6, 96, 24, 4, 0x000EA5E9);
    font_draw_string(bx + 16, py + 12, "Generar", 0x000F172A, 1);

    /* Lienzo del documento (papel) */
    uint32_t paper_x = win_x + 8;
    uint32_t paper_y = win_y + 72;
    uint32_t paper_w = win_w - 16;
    uint32_t paper_h = win_h - 72 - 26;
    if ((int32_t)paper_h < 40) return;

    fb_fill_rect(paper_x, paper_y, paper_w, paper_h, 0x00F8FAFC); /* papel claro */
    fb_draw_rect(paper_x, paper_y, paper_w, paper_h, 0x00CBD5E1);

    uint32_t content_x = paper_x + (uint32_t)ly_margin;
    uint32_t content_w = paper_w - 2 * (uint32_t)ly_margin;
    uint32_t cy = paper_y + 16 - (uint32_t)ly_scroll;
    uint32_t bottom = paper_y + paper_h - 8;

    font_set(ly_font_id);

    /* Título */
    if (ly_title[0] && cy + 20 > paper_y && cy < bottom) {
        uint32_t tw = 0;
        for (const char *t = ly_title; *t; ++t) tw += 16;
        uint32_t dx = ly_align_center && tw < content_w
                    ? content_x + (content_w - tw) / 2 : content_x;
        if (cy >= paper_y)
            font_draw_string(dx, cy, ly_title, 0x000F172A, 2);
        cy += 28;
    }
    if (ly_subtitle[0] && cy + 12 > paper_y && cy < bottom) {
        if (cy >= paper_y)
            cy = ly_draw_wrapped(content_x, cy, content_w, ly_subtitle, 0x00475569, 1, ly_align_center);
        else
            cy += 14;
        cy += 6;
        /* línea bajo subtítulo */
        if (cy >= paper_y && cy < bottom)
            fb_fill_rect(content_x, cy, content_w, 1, 0x00E2E8F0);
        cy += 12;
    }

    for (int i = 0; i < ly_block_count; ++i) {
        if (cy > bottom + 40) break;
        struct ly_block *b = &ly_blocks[i];
        int scale = 1;
        uint32_t color = 0x001E293B;
        uint32_t gap_before = 6;
        uint32_t gap_after = 4;
        int center = ly_align_center;

        switch (b->kind) {
        case LY_H1:
            scale = 2; color = 0x000F172A; gap_before = 16; gap_after = 8; break;
        case LY_H2:
            scale = 1; color = 0x001E3A5F; gap_before = 14; gap_after = 6; break;
        case LY_H3:
            scale = 1; color = 0x00334155; gap_before = 10; gap_after = 4; break;
        case LY_QUOTE:
            color = 0x00475569; gap_before = 10; gap_after = 8; break;
        case LY_LIST:
            color = 0x001E293B; gap_before = 4; gap_after = 2; break;
        case LY_HR:
            gap_before = 10; gap_after = 10; break;
        case LY_FOOTER:
            color = 0x0094A3B8; gap_before = 16; gap_after = 4; center = 1; break;
        default:
            break;
        }

        cy += gap_before;
        if (cy > bottom) break;

        if (b->kind == LY_HR) {
            if (cy >= paper_y && cy < bottom)
                fb_fill_rect(content_x, cy, content_w, 1, 0x00CBD5E1);
            cy += gap_after;
            continue;
        }

        if (b->kind == LY_QUOTE) {
            if (cy >= paper_y && cy < bottom)
                fb_fill_rect(content_x, cy, 3, 36, 0x0038BDF8);
            if (cy >= paper_y)
                cy = ly_draw_wrapped(content_x + 12, cy, content_w - 12, b->text, color, 1, 0);
            else
                cy += 14;
            cy += gap_after;
            continue;
        }

        if (b->kind == LY_LIST) {
            char line[LY_LINE_MAX];
            line[0] = '-'; line[1] = ' ';
            ly_strcpy(line + 2, b->text, sizeof(line) - 2);
            if (cy >= paper_y)
                cy = ly_draw_wrapped(content_x, cy, content_w, line, color, 1, 0);
            else
                cy += 12;
            cy += gap_after;
            continue;
        }

        if (cy >= paper_y)
            cy = ly_draw_wrapped(content_x, cy, content_w, b->text, color, scale, center);
        else
            cy += (uint32_t)(10 * scale + 2);
        cy += gap_after;
    }

    /* Status bar */
    fb_fill_rect(win_x + 1, win_y + win_h - 24, win_w - 2, 23, 0x001E293B);
    font_draw_string(win_x + 10, win_y + win_h - 16, ly_status, 0x0094A3B8, 1);

    char meta[48];
    int mi = 0;
    const char *ms = "bloques:";
    while (*ms) meta[mi++] = *ms++;
    int n = ly_block_count;
    char tb[8]; int tn = 0;
    if (n == 0) tb[tn++] = '0';
    else { int t = n; while (t) { tb[tn++] = '0' + (t % 10); t /= 10; } }
    while (tn) meta[mi++] = tb[--tn];
    meta[mi] = '\0';
    font_draw_string(win_x + win_w - 100, win_y + win_h - 16, meta, 0x0064748B, 1);
}

int layout_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h)
{
    /* Cerrar si clic en zona Esc (esquina superior derecha) */
    if (mx >= (int32_t)(win_x + win_w - 90) && mx <= (int32_t)(win_x + win_w - 8) &&
        my >= (int32_t)(win_y + 2) && my <= (int32_t)(win_y + 28)) {
        return 1; /* pedir cierre */
    }

    /* Focus en campo prompt */
    uint32_t py = win_y + 32;
    uint32_t field_x = win_x + 56;
    uint32_t field_w = win_w - 180;
    if (mx >= (int32_t)field_x && mx <= (int32_t)(field_x + field_w) &&
        my >= (int32_t)(py + 6) && my <= (int32_t)(py + 30)) {
        ly_prompt_focus = 1;
        return 0;
    }

    /* Botón Generar — invoca subagente maquetador (Paso 2) */
    uint32_t bx = win_x + win_w - 110;
    if (mx >= (int32_t)bx && mx <= (int32_t)(bx + 96) &&
        my >= (int32_t)(py + 6) && my <= (int32_t)(py + 30)) {
        ly_prompt_focus = 0;
        ly_do_generate();
        return 0;
    }

    /* Scroll rueda simulada: clic en mitad superior/inferior del papel */
    uint32_t paper_y = win_y + 72;
    uint32_t paper_h = win_h - 72 - 26;
    if (mx >= (int32_t)(win_x + 8) && mx <= (int32_t)(win_x + win_w - 8) &&
        my >= (int32_t)paper_y && my <= (int32_t)(paper_y + paper_h)) {
        if (my < (int32_t)(paper_y + paper_h / 2)) {
            if (ly_scroll > 24) ly_scroll -= 24;
            else ly_scroll = 0;
        } else {
            ly_scroll += 24;
        }
        return 0;
    }

    return 0;
}

void layout_handle_key(char c)
{
    if (c == 27) { /* Esc — el GUI puede cerrar; aquí solo quitamos focus */
        ly_prompt_focus = 0;
        return;
    }

    if (!ly_prompt_focus) {
        /* Scroll con teclas */
        if (c == 16 /* Ctrl+P */ || c == 'k') {
            if (ly_scroll > 16) ly_scroll -= 16; else ly_scroll = 0;
        } else if (c == 14 /* Ctrl+N */ || c == 'j') {
            ly_scroll += 16;
        }
        return;
    }

    if (c == '\b' || c == 127) {
        if (ly_prompt_len > 0) {
            ly_prompt[--ly_prompt_len] = '\0';
        }
        return;
    }

    if (c == '\r' || c == '\n') {
        ly_prompt_focus = 0;
        ly_do_generate();
        return;
    }

    if ((uint8_t)c >= 32 && (uint8_t)c < 127) {
        if (ly_prompt_len + 1 < LY_PROMPT_MAX) {
            ly_prompt[ly_prompt_len++] = c;
            ly_prompt[ly_prompt_len] = '\0';
        }
    }
}
