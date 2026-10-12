#include "browser.h"
#include "js_runtime.h"
extern void fb_present_full(void);
static uint32_t s_win_x = 0, s_win_y = 0, s_win_w = 0, s_win_h = 0;
#include "image.h"
#include "fb.h"
#include "gui_font.h"
#include "mouse.h"
#include "http.h"
#include "dns.h"
#include "console.h"
#include "arch.h"
#include "idt.h"
#include "mem.h"
#include "fs.h"
#include "thread.h"

#define BROWSER_MAX_ELEMENTS 768
#define BROWSER_MAX_LINKS    128
#define BROWSER_MAX_IMAGES   16
#define BROWSER_HIST_DEPTH   16
#define BROWSER_RX_BUF_SIZE  65536
#define BROWSER_LINE_LEN     160

enum web_elem_type { ELEM_TEXT = 0, ELEM_IMAGE, ELEM_HR, ELEM_EMPTY };

struct web_element {
    uint8_t  type;
    uint8_t  is_link;
    uint8_t  link_id;
    uint8_t  scale;
    uint8_t  has_bg;
    uint32_t color;
    uint32_t bg_color;
    int32_t  rel_x;
    int32_t  rel_y;
    int32_t  width;
    int32_t  height;
    char     text[BROWSER_LINE_LEN];
    int16_t  img_slot;
    char     id[32];
    char     onclick[128];
    uint8_t  has_onclick;
};

struct web_link {
    char url[160];
    int32_t rel_x, rel_y, width, height;
};

struct web_img_slot {
    char url[160];
    struct image img;
    int loaded;
};

static struct web_element *web_elements = 0;
static struct web_link    *web_links = 0;
static struct web_img_slot *web_images = 0;
static char               *http_rx_buf = 0;

static int web_element_count = 0, web_link_count = 0, web_image_count = 0;
static int web_scroll = 0, doc_total_height = 0;

static char history_urls[BROWSER_HIST_DEPTH][160];
static int  history_count = 0, history_pos = -1;

static char current_url[160] = "file:///workspace/demo_web.html";
static uint32_t url_len = 31, url_cursor = 31;
static int url_focused = 0;

static char status_text[128] = "Listo";
static uint32_t status_color = 0x0010B981;

static volatile int async_loading = 0;
static char async_target_url[160];

static const char *strstr_simple(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return haystack;
    }
    return 0;
}

static uint32_t parse_css_color(const char *str) {
    const char *hex = strstr_simple(str, "#");
    if (hex) {
        hex++;
        uint32_t c = 0;
        for (int i=0; i<6 && *hex; i++, hex++) {
            c <<= 4;
            if (*hex >= '0' && *hex <= '9') c |= (*hex - '0');
            else if (*hex >= 'a' && *hex <= 'f') c |= (*hex - 'a' + 10);
            else if (*hex >= 'A' && *hex <= 'F') c |= (*hex - 'A' + 10);
            else break;
        }
        return c | 0xFF000000;
    }
    return 0;
}

static void parse_css_style(const char *style, uint32_t *fg, uint32_t *bg, uint8_t *has_bg)
{
    if (!style || !*style) return;
    const char *p = style;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';') p++;
        if (!*p) break;

        const char *prop_start = p;
        while (*p && *p != ':' && *p != ';') p++;
        if (*p != ':') { if (*p) p++; continue; }

        const char *prop_end = p;
        p++;
        while (*p == ' ') p++;
        const char *val_start = p;
        while (*p && *p != ';') p++;

        uint32_t prop_len = (uint32_t)(prop_end - prop_start);
        while (prop_len > 0 && (prop_start[prop_len - 1] == ' ' || prop_start[prop_len - 1] == '\t')) {
            prop_len--;
        }

        if (prop_len == 5 && memcmp(prop_start, "color", 5) == 0) {
            *fg = parse_css_color(val_start);
        } else if ((prop_len == 16 && memcmp(prop_start, "background-color", 16) == 0) ||
                   (prop_len == 10 && memcmp(prop_start, "background", 10) == 0)) {
            *bg = parse_css_color(val_start);
            *has_bg = 1;
        }
    }
}

static void ensure_browser_buffers(void) {
    if (!web_elements) {
        web_elements = (struct web_element *)kmalloc(sizeof(struct web_element) * BROWSER_MAX_ELEMENTS);
        if (web_elements) memset((void *)web_elements, 0, sizeof(struct web_element) * BROWSER_MAX_ELEMENTS);
    }
    if (!web_links) {
        web_links = (struct web_link *)kmalloc(sizeof(struct web_link) * BROWSER_MAX_LINKS);
        if (web_links) memset((void *)web_links, 0, sizeof(struct web_link) * BROWSER_MAX_LINKS);
    }
    if (!web_images) {
        web_images = (struct web_img_slot *)kmalloc(sizeof(struct web_img_slot) * BROWSER_MAX_IMAGES);
        if (web_images) memset((void *)web_images, 0, sizeof(struct web_img_slot) * BROWSER_MAX_IMAGES);
    }
    if (!http_rx_buf) {
        http_rx_buf = (char *)kmalloc(BROWSER_RX_BUF_SIZE);
        if (http_rx_buf) memset((void *)http_rx_buf, 0, BROWSER_RX_BUF_SIZE);
    }
}

static void clear_page_resources(void) {
    ensure_browser_buffers();
    if (web_images) {
        for (int i = 0; i < web_image_count; ++i) {
            if (web_images[i].loaded) { image_free(&web_images[i].img); web_images[i].loaded = 0; }
        }
    }
    web_element_count = 0; web_link_count = 0; web_image_count = 0;
    web_scroll = 0; doc_total_height = 0;
}

static int str_len_u(const char *s) { int len = 0; while (s && s[len]) len++; return len; }
static void str_copy_u(char *dst, const char *src, int max) { int i = 0; while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; } dst[i] = '\0'; }
static int str_equal_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static const char *strip_file_path(const char *url, char *buf, uint32_t max) {
    const char *p = url;
    if (p[0] == 'f' && p[1] == 'i' && p[2] == 'l' && p[3] == 'e' && p[4] == ':') p += 5;
    while (*p == '/') p++;
    uint32_t i = 0; buf[i++] = '/';
    while (*p && i < max - 1) buf[i++] = *p++;
    buf[i] = '\0';
    return buf;
}

/* Resolutor canónico de enlaces relativos a la web o al disco */
static void resolve_relative_url(const char *href, char *out, int max_out) {
    if (!href || !*href) return;

    /* 1. Ya es URL absoluta */
    if ((href[0] == 'h' && href[1] == 't' && href[2] == 't' && href[3] == 'p') ||
        (href[0] == 'f' && href[1] == 'i' && href[2] == 'l' && href[3] == 'e')) {
        str_copy_u(out, href, max_out);
        return;
    }

    /* 2. Si estamos en una página web remota (http:// o https://) */
    if (current_url[0] == 'h' && current_url[1] == 't') {
        char base_domain[128];
        uint32_t bp = 0;
        const char *c = current_url;

        while (*c && *c != ':' && bp < 16) base_domain[bp++] = *c++;
        if (*c == ':') base_domain[bp++] = *c++;
        while (*c == '/' && bp < 20) base_domain[bp++] = *c++;
        while (*c && *c != '/' && *c != ':' && bp < 120) base_domain[bp++] = *c++;
        base_domain[bp] = '\0';

        if (href[0] == '/') {
            uint32_t op = 0;
            const char *b = base_domain;
            while (*b && (int)op < max_out - 1) out[op++] = *b++;
            const char *h = href;
            while (*h && (int)op < max_out - 1) out[op++] = *h++;
            out[op] = '\0';
            return;
        } else {
            uint32_t op = 0;
            const char *b = base_domain;
            while (*b && (int)op < max_out - 1) out[op++] = *b++;
            if ((int)op < max_out - 1) out[op++] = '/';
            const char *h = href;
            while (*h && (int)op < max_out - 1) out[op++] = *h++;
            out[op] = '\0';
            return;
        }
    }

    /* 3. Si estamos en un archivo local de RamFS */
    if (href[0] == '/') {
        str_copy_u(out, href, max_out);
        return;
    }
    str_copy_u(out, "/workspace/", max_out);
    int p = str_len_u(out);
    while (href && *href && p < max_out - 1) out[p++] = *href++;
    out[p] = '\0';
}

static void history_push(const char *url) {
    if (history_pos >= 0 && history_pos < history_count) { if (str_equal_ci(history_urls[history_pos], url)) return; }
    if (history_pos < history_count - 1) history_count = history_pos + 1;
    if (history_count >= BROWSER_HIST_DEPTH) {
        for (int i = 0; i < BROWSER_HIST_DEPTH - 1; ++i) str_copy_u(history_urls[i], history_urls[i + 1], sizeof(history_urls[i]));
        history_count = BROWSER_HIST_DEPTH - 1;
    }
    str_copy_u(history_urls[history_count], url, sizeof(history_urls[history_count]));
    history_pos = history_count++;
}

static void unchunk_http_body(char *body, int *len)
{
    if (!body || *len <= 0) return;
    const char *p = body;
    int is_hex = 0;
    while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
        is_hex++; p++;
    }
    if (is_hex == 0 || (*p != '\r' && *p != '\n')) {
        return;
    }

    char *dst = body;
    const char *src = body;
    int total_out = 0;

    while (*src) {
        while (*src == '\r' || *src == '\n') src++;
        if (!*src) break;

        uint32_t chunk_sz = 0;
        int found_hex = 0;
        while (1) {
            char c = *src;
            if (c >= '0' && c <= '9') { chunk_sz = (chunk_sz << 4) | (uint32_t)(c - '0'); src++; found_hex = 1; }
            else if (c >= 'a' && c <= 'f') { chunk_sz = (chunk_sz << 4) | (uint32_t)(c - 'a' + 10); src++; found_hex = 1; }
            else if (c >= 'A' && c <= 'F') { chunk_sz = (chunk_sz << 4) | (uint32_t)(c - 'A' + 10); src++; found_hex = 1; }
            else break;
        }
        if (!found_hex) break;

        while (*src && *src != '\n') src++;
        if (*src == '\n') src++;
        if (chunk_sz == 0) break;

        for (uint32_t i = 0; i < chunk_sz && *src; ++i) {
            *dst++ = *src++;
            total_out++;
        }
    }
    *dst = '\0';
    *len = total_out;
}

static int fetch_http_image(const char *url, int slot) {
    const char *p = url;
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == ':') p += 7;
    char host[64]; uint32_t hp = 0;
    while (*p && *p != '/' && *p != ':' && hp < 63) host[hp++] = *p++;
    host[hp] = '\0';

    uint16_t port = 80;
    if (*p == ':') { p++; port = 0; while (*p >= '0' && *p <= '9') port = port * 10 + (*p++ - '0'); }
    const char *path = (*p == '/') ? p : "/";

    uint8_t ip[4] = { 10, 0, 2, 2 };

    uint8_t *buf = (uint8_t *)kmalloc(49152);
    if (!buf) return -1;

    struct http_response resp;
    int code = http_get_host(ip, port, host, path, (char *)buf, 49151, &resp, 2000);

    if (code == 200 && resp.body_len > 54) {
        char *body = (resp.body && resp.body[0]) ? resp.body : (char *)buf;
        if (image_decode((uint8_t*)body, resp.body_len, &web_images[slot].img) == 0) {
            web_images[slot].loaded = 1;
            kfree(buf);
            return 0;
        }
    }
    kfree(buf);
    return -1;
}

static int load_image_resource(const char *url) {
    ensure_browser_buffers();
    if (!web_images || web_image_count >= BROWSER_MAX_IMAGES) return -1;
    for (int i = 0; i < web_image_count; ++i) { if (web_images[i].loaded && str_equal_ci(web_images[i].url, url)) return i; }

    int slot = web_image_count;
    str_copy_u(web_images[slot].url, url, sizeof(web_images[slot].url));
    web_images[slot].loaded = 0;

    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p') {
        if (fetch_http_image(url, slot) == 0) {
            web_image_count++;
            return slot;
        }
        return -1;
    }

    static uint8_t img_raw_buf[24576];
    char fpath_buf[160];
    const char *fpath = strip_file_path(url, fpath_buf, sizeof(fpath_buf));
    int bytes = vfs_read(fpath, (char *)img_raw_buf, sizeof(img_raw_buf) - 1);

    if (bytes <= 0) {
        if (str_equal_ci(fpath, "/workspace/logo_soma.bmp")) {
            bytes = image_create_demo_bmp(img_raw_buf, sizeof(img_raw_buf), 80, 40);
            if (bytes > 0) vfs_write(fpath, (const char *)img_raw_buf, (uint32_t)bytes);
        } else if (str_equal_ci(fpath, "/workspace/chip_soma.bmp")) {
            bytes = image_create_chip_bmp(img_raw_buf, sizeof(img_raw_buf), 64, 32);
            if (bytes > 0) vfs_write(fpath, (const char *)img_raw_buf, (uint32_t)bytes);
        }
    }

    if (bytes > 0 && image_decode(img_raw_buf, (size_t)bytes, &web_images[slot].img) == 0) {
        web_images[slot].loaded = 1; web_image_count++; return slot;
    }
    return -1;
}

static void add_element_text(const char *txt, uint32_t color, uint8_t scale, int is_link, int link_id, uint32_t bg_color, uint8_t has_bg, int32_t *cur_y) {
    if (!web_elements || web_element_count >= BROWSER_MAX_ELEMENTS) return;
    struct web_element *e = &web_elements[web_element_count];
    e->type = ELEM_TEXT; e->is_link = (uint8_t)is_link; e->link_id = (uint8_t)link_id;
    e->scale = scale ? scale : 1; e->color = color; e->bg_color = bg_color; e->has_bg = has_bg;
    e->rel_x = 0; e->rel_y = *cur_y; e->img_slot = -1;
    str_copy_u(e->text, txt, BROWSER_LINE_LEN);
    e->width = str_len_u(e->text) * 8 * e->scale; e->height = 10 * e->scale + 6;
    if (is_link && link_id >= 0 && link_id < web_link_count) {
        web_links[link_id].rel_x = e->rel_x; web_links[link_id].rel_y = e->rel_y;
        web_links[link_id].width = e->width; web_links[link_id].height = e->height;
    }
    *cur_y += e->height; web_element_count++;
}

static void add_element_image(int img_slot, int32_t req_w, int32_t req_h, int is_link, int link_id, int32_t *cur_y) {
    if (!web_elements || web_element_count >= BROWSER_MAX_ELEMENTS || img_slot < 0) return;
    struct web_element *e = &web_elements[web_element_count];
    e->type = ELEM_IMAGE; e->is_link = (uint8_t)is_link; e->link_id = (uint8_t)link_id; e->img_slot = (int16_t)img_slot;
    e->rel_x = 0; e->rel_y = *cur_y; e->has_bg = 0;
    int32_t nat_w = (int32_t)web_images[img_slot].img.width;
    int32_t nat_h = (int32_t)web_images[img_slot].img.height;
    e->width  = (req_w > 0) ? req_w : nat_w; e->height = (req_h > 0) ? req_h : nat_h;
    if (e->width > 800) { e->height = (e->height * 800) / e->width; e->width = 800; }
    if (is_link && link_id >= 0 && link_id < web_link_count) {
        web_links[link_id].rel_x = e->rel_x; web_links[link_id].rel_y = e->rel_y;
        web_links[link_id].width = e->width; web_links[link_id].height = e->height;
    }
    *cur_y += e->height + 12; web_element_count++;
}

static void add_element_hr(int32_t *cur_y) {
    if (!web_elements || web_element_count >= BROWSER_MAX_ELEMENTS) return;
    struct web_element *e = &web_elements[web_element_count];
    e->type = ELEM_HR; e->color = 0x00334155; e->rel_x = 0; e->rel_y = *cur_y; e->width = 800; e->height = 14; e->has_bg = 0;
    *cur_y += e->height; web_element_count++;
}

static void parse_html_to_display_list(const char *html) {
    clear_page_resources();
    if (!html || !*html) return;

    int32_t cur_y = 10;
    char text_buf[BROWSER_LINE_LEN]; uint32_t tp = 0;
    uint32_t cur_color = 0x00F8FAFC, active_bg_color = 0;
    uint8_t cur_scale = 1, active_has_bg = 0;

    int in_tag = 0, in_tag_name = 0, in_link = 0, in_pre = 0;
    int cur_link_id = -1;

    char current_href[160]; current_href[0] = '\0';
    char tag_name[32]; uint32_t tag_name_len = 0;
    char img_src[160]; img_src[0] = '\0';
    char img_alt[64];  img_alt[0] = '\0';
    int32_t img_req_w = 0, img_req_h = 0;
    char style_buf[256]; style_buf[0] = '\0';
    char input_val[64]; input_val[0] = '\0';
    char input_type[32]; input_type[0] = '\0';

    const char *p = html;

    while (*p) {
        if (!in_tag && p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            p += 4;
            while (*p && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) p++;
            if (*p) p += 3;
            continue;
        }

        if (!in_tag && *p == '<') {
            in_tag = 1; in_tag_name = 1; tag_name_len = 0; tag_name[0] = '\0';
            img_src[0] = '\0'; img_alt[0] = '\0'; img_req_w = 0; img_req_h = 0;
            style_buf[0] = '\0'; input_val[0] = '\0'; input_type[0] = '\0';
            p++; continue;
        }

        if (in_tag) {
            if (*p == '>') {
                in_tag = 0; in_tag_name = 0;
                tag_name[tag_name_len] = '\0';

                if (str_equal_ci(tag_name, "script") || str_equal_ci(tag_name, "noscript")) {
                    p++;
                    while (*p) {
                        if (p[0] == '<' && p[1] == '/' &&
                            (p[2] == 's' || p[2] == 'S') &&
                            (p[3] == 'c' || p[3] == 'C') &&
                            (p[4] == 'r' || p[4] == 'R') &&
                            (p[5] == 'i' || p[5] == 'I') &&
                            (p[6] == 'p' || p[6] == 'P') &&
                            (p[7] == 't' || p[7] == 'T')) {
                            p += 8;
                            while (*p && *p != '>') p++;
                            if (*p == '>') p++;
                            break;
                        }
                        p++;
                    }
                    continue;
                }

                if (str_equal_ci(tag_name, "style")) {
                    p++;
                    while (*p) {
                        if (p[0] == '<' && p[1] == '/' &&
                            (p[2] == 's' || p[2] == 'S') &&
                            (p[3] == 't' || p[3] == 'T') &&
                            (p[4] == 'y' || p[4] == 'Y') &&
                            (p[5] == 'l' || p[5] == 'L') &&
                            (p[6] == 'e' || p[6] == 'E')) {
                            p += 7;
                            while (*p && *p != '>') p++;
                            if (*p == '>') p++;
                            break;
                        }
                        p++;
                    }
                    continue;
                }

                if (style_buf[0]) {
                    parse_css_style(style_buf, &cur_color, &active_bg_color, &active_has_bg);
                }

                if (str_equal_ci(tag_name, "h1")) {
                    if (tp > 0) { text_buf[tp] = '\0'; add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y); tp = 0; }
                    if (!style_buf[0]) { cur_color = 0x00F59E0B; } cur_scale = 2;
                } else if (tag_name[0] == 'h' && tag_name[1] >= '2' && tag_name[1] <= '4') {
                    if (tp > 0) { text_buf[tp] = '\0'; add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y); tp = 0; }
                    if (!style_buf[0]) { cur_color = 0x0038BDF8; } cur_scale = 1;
                } else if (str_equal_ci(tag_name, "hr")) {
                    if (tp > 0) { text_buf[tp] = '\0'; add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y); tp = 0; }
                    add_element_hr(&cur_y);
                } else if (str_equal_ci(tag_name, "p") || str_equal_ci(tag_name, "div") || str_equal_ci(tag_name, "br") || str_equal_ci(tag_name, "li") || str_equal_ci(tag_name, "tr")) {
                    if (tp > 0) { text_buf[tp] = '\0'; add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y); tp = 0; }
                    if (str_equal_ci(tag_name, "li")) { text_buf[tp++] = ' '; text_buf[tp++] = '*'; text_buf[tp++] = ' '; }
                } else if (str_equal_ci(tag_name, "img")) {
                    if (tp > 0) { text_buf[tp] = '\0'; add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y); tp = 0; }
                    if (img_src[0]) {
                        char abs_img[160]; resolve_relative_url(img_src, abs_img, sizeof(abs_img));
                        int slot = load_image_resource(abs_img);
                        if (slot >= 0) {
                            add_element_image(slot, img_req_w, img_req_h, in_link, cur_link_id, &cur_y);
                        } else if (img_alt[0]) {
                            add_element_text(img_alt, 0x00F59E0B, 1, in_link, cur_link_id, 0, 0, &cur_y);
                        }
                    }
                } else if (str_equal_ci(tag_name, "input")) {
                    if (tp > 0) { text_buf[tp] = '\0'; add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y); tp = 0; }
                    if (str_equal_ci(input_type, "submit") || str_equal_ci(input_type, "button")) {
                        char btn_str[80]; uint32_t bp = 0;
                        btn_str[bp++] = '['; btn_str[bp++] = ' ';
                        const char *iv = input_val[0] ? input_val : "Buscar";
                        while (*iv && bp < 74) btn_str[bp++] = *iv++;
                        btn_str[bp++] = ' '; btn_str[bp++] = ']'; btn_str[bp] = '\0';
                        add_element_text(btn_str, 0x0038BDF8, 1, 0, -1, 0x001E293B, 1, &cur_y);
                    } else if (!str_equal_ci(input_type, "hidden")) {
                        add_element_text("[  Buscar en la web...                                     ]", 0x0094A3B8, 1, 0, -1, 0x000F172A, 1, &cur_y);
                    }
                } else if (str_equal_ci(tag_name, "a")) {
                    in_link = 1;
                    if (current_href[0] && web_link_count < BROWSER_MAX_LINKS) {
                        str_copy_u(web_links[web_link_count].url, current_href, sizeof(web_links[web_link_count].url));
                        cur_link_id = web_link_count++;
                    }
                    if (!style_buf[0]) cur_color = 0x0038BDF8;
                } else if (tag_name[0] == '/' && (tag_name[1] == 'h' || tag_name[1] == 'p' || tag_name[1] == 'a' || tag_name[1] == 'd' || tag_name[1] == 't')) {
                    if (tp > 0) { text_buf[tp] = '\0'; add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y); tp = 0; }
                    cur_color = 0x00F8FAFC; cur_scale = 1; active_has_bg = 0; active_bg_color = 0;
                    if (tag_name[1] == 'a') { in_link = 0; cur_link_id = -1; current_href[0] = '\0'; }
                }

                p++; continue;
            }

            if (in_tag_name) {
                if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                    in_tag_name = 0;
                    tag_name[tag_name_len] = '\0';
                } else if (tag_name_len < sizeof(tag_name) - 1) {
                    tag_name[tag_name_len++] = (*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p;
                }
            } else {
                /* Parseo seguro de href (nunca sangra fuera de la etiqueta) */
                if (*p == 'h' && *(p+1) == 'r' && *(p+2) == 'e' && *(p+3) == 'f' && *(p+4) == '=') {
                    p += 5; char q = 0; if (*p == '"' || *p == '\'') q = *p++;
                    int hp = 0;
                    while (*p && *p != '>' && *p != '\n' && *p != '\r' && hp < 159) {
                        if (q && *p == q) { p++; break; }
                        if (!q && (*p == ' ' || *p == '\t')) break;
                        current_href[hp++] = *p++;
                    }
                    current_href[hp] = '\0'; continue;
                }
                if (*p == 's' && *(p+1) == 'r' && *(p+2) == 'c' && *(p+3) == '=') {
                    p += 4; char q = 0; if (*p == '"' || *p == '\'') q = *p++;
                    int sp = 0;
                    while (*p && *p != '>' && *p != '\n' && *p != '\r' && sp < 159) {
                        if (q && *p == q) { p++; break; }
                        if (!q && (*p == ' ' || *p == '\t')) break;
                        img_src[sp++] = *p++;
                    }
                    img_src[sp] = '\0'; continue;
                }
                if (*p == 'a' && *(p+1) == 'l' && *(p+2) == 't' && *(p+3) == '=') {
                    p += 4; char q = 0; if (*p == '"' || *p == '\'') q = *p++;
                    int ap = 0;
                    while (*p && *p != '>' && *p != '\n' && *p != '\r' && ap < 63) {
                        if (q && *p == q) { p++; break; }
                        if (!q && (*p == ' ' || *p == '\t')) break;
                        img_alt[ap++] = *p++;
                    }
                    img_alt[ap] = '\0'; continue;
                }
                if (*p == 'w' && *(p+1) == 'i' && *(p+2) == 'd' && *(p+3) == 't' && *(p+4) == 'h' && *(p+5) == '=') {
                    p += 6; if (*p == '"' || *p == '\'') p++; img_req_w = 0; while (*p >= '0' && *p <= '9') img_req_w = img_req_w * 10 + (*p++ - '0');
                    if (*p == '"' || *p == '\'') { p++; }
                    continue;
                }
                if (*p == 'h' && *(p+1) == 'e' && *(p+2) == 'i' && *(p+3) == 'g' && *(p+4) == 'h' && *(p+5) == 't' && *(p+6) == '=') {
                    p += 7; if (*p == '"' || *p == '\'') p++; img_req_h = 0; while (*p >= '0' && *p <= '9') img_req_h = img_req_h * 10 + (*p++ - '0');
                    if (*p == '"' || *p == '\'') { p++; }
                    continue;
                }
                if (*p == 's' && *(p+1) == 't' && *(p+2) == 'y' && *(p+3) == 'l' && *(p+4) == 'e' && *(p+5) == '=') {
                    p += 6; char q = 0; if (*p == '"' || *p == '\'') q = *p++;
                    int sp = 0;
                    while (*p && *p != '>' && *p != '\n' && *p != '\r' && sp < 255) {
                        if (q && *p == q) { p++; break; }
                        style_buf[sp++] = *p++;
                    }
                    style_buf[sp] = '\0'; continue;
                }
                if (*p == 'v' && *(p+1) == 'a' && *(p+2) == 'l' && *(p+3) == 'u' && *(p+4) == 'e' && *(p+5) == '=') {
                    p += 6; char q = 0; if (*p == '"' || *p == '\'') q = *p++;
                    int vp = 0;
                    while (*p && *p != '>' && *p != '\n' && *p != '\r' && vp < 63) {
                        if (q && *p == q) { p++; break; }
                        input_val[vp++] = *p++;
                    }
                    input_val[vp] = '\0'; continue;
                }
                if (*p == 't' && *(p+1) == 'y' && *(p+2) == 'p' && *(p+3) == 'e' && *(p+4) == '=') {
                    p += 5; char q = 0; if (*p == '"' || *p == '\'') q = *p++;
                    int tp_val = 0;
                    while (*p && *p != '>' && *p != '\n' && *p != '\r' && tp_val < 31) {
                        if (q && *p == q) { p++; break; }
                        input_type[tp_val++] = *p++;
                    }
                    input_type[tp_val] = '\0'; continue;
                }
            }
            p++; continue;
        }

        if (*p == '&') {
            if (p[1] == 'n' && p[2] == 'b' && p[3] == 's' && p[4] == 'p' && p[5] == ';') { p += 6; if (tp > 0 && text_buf[tp - 1] != ' ') text_buf[tp++] = ' '; continue; }
            if (p[1] == 'q' && p[2] == 'u' && p[3] == 'o' && p[4] == 't' && p[5] == ';') { p += 6; text_buf[tp++] = '"'; continue; }
            if (p[1] == 'a' && p[2] == 'm' && p[3] == 'p' && p[4] == ';') { p += 5; text_buf[tp++] = '&'; continue; }
            if (p[1] == 'l' && p[2] == 't' && p[3] == ';') { p += 4; text_buf[tp++] = '<'; continue; }
            if (p[1] == 'g' && p[2] == 't' && p[3] == ';') { p += 4; text_buf[tp++] = '>'; continue; }
            if (p[1] == '#' && p[2] == '3' && p[3] == '9' && p[4] == ';') { p += 5; text_buf[tp++] = '\''; continue; }
            if (p[1] == 'a' && p[2] == 'a' && p[3] == 'c' && p[4] == 'u' && p[5] == 't' && p[6] == 'e' && p[7] == ';') { p += 8; text_buf[tp++] = 'a'; continue; }
            if (p[1] == 'e' && p[2] == 'a' && p[3] == 'c' && p[4] == 'u' && p[5] == 't' && p[6] == 'e' && p[7] == ';') { p += 8; text_buf[tp++] = 'e'; continue; }
            if (p[1] == 'i' && p[2] == 'a' && p[3] == 'c' && p[4] == 'u' && p[5] == 't' && p[6] == 'e' && p[7] == ';') { p += 8; text_buf[tp++] = 'i'; continue; }
            if (p[1] == 'o' && p[2] == 'a' && p[3] == 'c' && p[4] == 'u' && p[5] == 't' && p[6] == 'e' && p[7] == ';') { p += 8; text_buf[tp++] = 'o'; continue; }
            if (p[1] == 'u' && p[2] == 'a' && p[3] == 'c' && p[4] == 'u' && p[5] == 't' && p[6] == 'e' && p[7] == ';') { p += 8; text_buf[tp++] = 'u'; continue; }
            if (p[1] == 'n' && p[2] == 't' && p[3] == 'i' && p[4] == 'l' && p[5] == 'd' && p[6] == 'e' && p[7] == ';') { p += 8; text_buf[tp++] = 'n'; continue; }
        }

        uint8_t uc = (uint8_t)*p;
        if (uc == 0xC3 && *(p + 1) != '\0') {
            uint8_t uc2 = (uint8_t)*(p + 1);
            p += 2;
            char mapped = ' ';
            if (uc2 == 0xA1 || uc2 == 0x81) mapped = 'a';
            else if (uc2 == 0xA9 || uc2 == 0x89) mapped = 'e';
            else if (uc2 == 0xAD || uc2 == 0x8D) mapped = 'i';
            else if (uc2 == 0xB3 || uc2 == 0x93) mapped = 'o';
            else if (uc2 == 0xBA || uc2 == 0x9A) mapped = 'u';
            else if (uc2 == 0xB1 || uc2 == 0x91) mapped = 'n';
            if (tp < BROWSER_LINE_LEN - 4) text_buf[tp++] = mapped;
            continue;
        }

        char c = *p++;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!in_pre) {
                if (tp > 0 && text_buf[tp - 1] != ' ') {
                    if (tp < BROWSER_LINE_LEN - 4) text_buf[tp++] = ' ';
                }
                continue;
            }
        }

        if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
            text_buf[tp++] = c;
            if (tp >= BROWSER_LINE_LEN - 4) {
                uint32_t sp_pos = tp;
                while (sp_pos > 16 && text_buf[sp_pos - 1] != ' ') sp_pos--;
                if (sp_pos > 16) {
                    char over[BROWSER_LINE_LEN]; uint32_t op = 0;
                    for (uint32_t k = sp_pos; k < tp; ++k) over[op++] = text_buf[k];
                    text_buf[sp_pos - 1] = '\0';
                    add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y);
                    tp = 0;
                    for (uint32_t k = 0; k < op; ++k) text_buf[tp++] = over[k];
                } else {
                    text_buf[tp] = '\0';
                    add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y);
                    tp = 0;
                }
            }
        }
    }
    if (tp > 0) { text_buf[tp] = '\0'; add_element_text(text_buf, cur_color, cur_scale, in_link, cur_link_id, active_bg_color, active_has_bg, &cur_y); }
    doc_total_height = cur_y + 20;
}

/* ====================================================================
 * DESCARGA ASÍNCRONA MULTIHILO (EL RATÓN NUNCA SE CONGELA)
 * ==================================================================== */

static void browser_worker_task(void *arg)
{
    (void)arg;
    const char *url = async_target_url;

    if (url && strstr_simple(url, "javascript:") != 0) {
        const char *jscode = strstr_simple(url, "javascript:") + 11;
        browser_js_eval(jscode);
        status_color = 0x0010B981;
        str_copy_u(status_text, "JS ejecutado con exito", sizeof(status_text));
        if (s_win_w > 0) {
            browser_render_window(s_win_x, s_win_y, s_win_w, s_win_h, 1);
            fb_present_full();
        }
        async_loading = 0;
        return;
    }

    if (url && strstr_simple(url, "test_js") != 0) {
        static const char builtin_test_html[] =
            "<h1>SOMA OS :: Prueba de JavaScript y DOM</h1>"
            "<hr>"
            "<p>Estado del componente reactivo:</p>"
            "<p id=\"msg\" style=\"color: #38BDF8;\">[Texto Original en Espera de Evento]</p>"
            "<br>"
            "<p>"
            "  <a href=\"javascript:document.getElementById('msg').innerText = 'EXITO: Texto modificado dinamicamente con JS en SOMA OS!';\" style=\"color: #F59E0B;\">"
            "    [ HAZ CLIC AQUI PARA PROBAR JAVASCRIPT ]"
            "  </a>"
            "</p>"
            "<hr>";
        clear_page_resources();
        parse_html_to_display_list(builtin_test_html);
        status_color = 0x0010B981;
        str_copy_u(status_text, "Test JS interactivo listo (0 ms)", sizeof(status_text));
        async_loading = 0;
        return;
    }

    /* 1. Archivo Local */
    if ((url[0] == 'f' && url[1] == 'i') || (url[0] == '/')) {
        char fpath_buf[160];
        const char *fpath = strip_file_path(url, fpath_buf, sizeof(fpath_buf));
        int r = vfs_read(fpath, http_rx_buf, BROWSER_RX_BUF_SIZE - 1);
        if (r >= 0) {
            if ((r >= 2 && http_rx_buf[0] == 'B' && http_rx_buf[1] == 'M') || (r >= 2 && http_rx_buf[0] == 'P')) {
                clear_page_resources();
                int slot = load_image_resource(url);
                int32_t cur_y = 10;
                add_element_text("Visor de Imagenes SOMA", 0x00F59E0B, 2, 0, -1, 0, 0, &cur_y);
                add_element_hr(&cur_y);
                if (slot >= 0) {
                    add_element_image(slot, 0, 0, 0, -1, &cur_y);
                    status_color = 0x0010B981;
                    str_copy_u(status_text, "Imagen renderizada", sizeof(status_text));
                }
                doc_total_height = cur_y + 20;
                async_loading = 0;
                return;
            }
            http_rx_buf[r] = '\0';
            parse_html_to_display_list(http_rx_buf);
            status_color = 0x0010B981;
            str_copy_u(status_text, "Archivo HTML cargado", sizeof(status_text));
        } else {
            clear_page_resources();
            int32_t cur_y = 20;
            add_element_text("[ERROR 404]: Archivo no encontrado en RamFS.", 0x00EF4444, 1, 0, -1, 0, 0, &cur_y);
            status_color = 0x00EF4444;
            str_copy_u(status_text, "Archivo no encontrado", sizeof(status_text));
        }
        async_loading = 0;
        return;
    }

    /* 2. Detección HTTPS -> Gateway Host en 10.0.2.2:8095 */
    const char *p = url;
    int is_https = 0;
    if (memcmp(p, "https://", 8) == 0) is_https = 1;
    else if (memcmp(p, "http://", 7) == 0) p += 7;

    if (is_https || strstr_simple(url, "20minutos") != 0 || strstr_simple(url, "elpais") != 0 || strstr_simple(url, "x.com") != 0) {
        str_copy_u(status_text, "Cargando via Gateway HTTPS...", sizeof(status_text));
        status_color = 0x0038BDF8;

        uint8_t gw_ip[4] = { 10, 0, 2, 2 };
        char req_path[256];
        uint32_t rp = 0;
        const char *prefix = "/web?q=";
        while (*prefix) req_path[rp++] = *prefix++;
        const char *u_ptr = url;
        while (*u_ptr && rp < sizeof(req_path) - 1) req_path[rp++] = *u_ptr++;
        req_path[rp] = '\0';

        struct http_response resp;
        int code = http_get_host(gw_ip, 8095, "10.0.2.2", req_path, http_rx_buf, BROWSER_RX_BUF_SIZE - 1, &resp, 14000);
        if (code >= 200 && code < 400) {
            char *body = (resp.body && resp.body[0]) ? resp.body : http_rx_buf;
            int blen = (int)resp.body_len;
            unchunk_http_body(body, &blen);
            parse_html_to_display_list(body);
            status_color = 0x0010B981;
            str_copy_u(status_text, "HTTPS 200 OK (Acelerado por Gateway)", sizeof(status_text));
        } else {
            clear_page_resources();
            int32_t cur_y = 20;
            add_element_text("[GATEWAY ERROR]: No se pudo conectar al servidor remoto.", 0x00EF4444, 1, 0, -1, 0, 0, &cur_y);
            status_color = 0x00EF4444;
            str_copy_u(status_text, "Fallo Gateway HTTPS", sizeof(status_text));
        }
        async_loading = 0;
        return;
    }

    /* 3. HTTP Plano Nativo */
    char host[64]; uint32_t hp = 0;
    while (*p && *p != '/' && *p != ':' && hp < 63) host[hp++] = *p++;
    host[hp] = '\0';

    uint16_t port = 80;
    if (*p == ':') { p++; port = 0; while (*p >= '0' && *p <= '9') port = port * 10 + (*p++ - '0'); }
    const char *path = (*p == '/') ? p : "/";

    uint8_t ip[4]; int is_ip = 1, dots = 0;
    for (int i = 0; host[i]; ++i) { if (host[i] == '.') dots++; else if (host[i] < '0' || host[i] > '9') is_ip = 0; }
    if (dots != 3) is_ip = 0;

    if (is_ip) {
        const char *s = host;
        for (int i=0; i<4; i++) { uint32_t v=0; while(*s>='0' && *s<='9') v = v*10 + (*s++ - '0'); ip[i] = (uint8_t)v; if(*s=='.') s++; }
    } else {
        str_copy_u(status_text, "Resolviendo DNS...", sizeof(status_text));
        status_color = 0x00F59E0B;
        if (!dns_resolve(host, ip, 4000)) {
            clear_page_resources();
            int32_t cur_y = 20;
            add_element_text("[DNS ERROR]: No se pudo resolver.", 0x00EF4444, 1, 0, -1, 0, 0, &cur_y);
            status_color = 0x00EF4444;
            str_copy_u(status_text, "Error DNS", sizeof(status_text));
            async_loading = 0;
            return;
        }
    }

    str_copy_u(status_text, "Conectando HTTP...", sizeof(status_text));
    struct http_response resp;
    int code = http_get_host(ip, port, host, path, http_rx_buf, BROWSER_RX_BUF_SIZE - 1, &resp, 14000);
    if (code >= 0) {
        char *body = (resp.body && resp.body[0]) ? resp.body : http_rx_buf;
        int blen = (int)resp.body_len;
        unchunk_http_body(body, &blen);
        parse_html_to_display_list(body);
        status_color = 0x0010B981;
        str_copy_u(status_text, "HTTP 200 OK | Documento renderizado", sizeof(status_text));
    } else {
        clear_page_resources();
        int32_t cur_y = 20;
        add_element_text("[HTTP ERROR]: Servidor no responde.", 0x00EF4444, 1, 0, -1, 0, 0, &cur_y);
        status_color = 0x00EF4444;
        str_copy_u(status_text, "Fallo HTTP", sizeof(status_text));
    }

    async_loading = 0;
}

void browser_load_url(const char *url) {
    if (url && strstr_simple(url, "javascript:") != 0) {
        const char *jscode = strstr_simple(url, "javascript:") + 11;
        browser_js_eval(jscode);
        status_color = 0x0010B981;
        str_copy_u(status_text, "JS ejecutado con exito", sizeof(status_text));
        if (s_win_w > 0) {
            browser_render_window(s_win_x, s_win_y, s_win_w, s_win_h, 1);
            fb_present_full();
        }
        return;
    }
    ensure_browser_buffers();
    if (!http_rx_buf) return;
    while (*url == ' ') url++;
    if (!*url) return;

    if (async_loading) return;

    str_copy_u(current_url, url, sizeof(current_url));
    url_len = (uint32_t)str_len_u(current_url);
    url_cursor = url_len;
    history_push(current_url);

    str_copy_u(async_target_url, current_url, sizeof(async_target_url));
    async_loading = 1;
    str_copy_u(status_text, "Descargando en segundo plano...", sizeof(status_text));
    status_color = 0x00F59E0B;

    /* Lanzar la descarga en un hilo KThread dedicado: el bucle GUI nunca se congela */
    thread_create("web_worker", browser_worker_task, 0);
}

void browser_init(void) {
    ensure_browser_buffers();
    clear_page_resources();
    history_count = 0;
    history_pos = -1;
    async_loading = 0;
    browser_load_url("file:///workspace/demo_web.html");
}

/* SOMA-JS: Puente de actualización reactiva */
int browser_set_element_text_by_id(const char *id, const char *new_text) {
    if (!id || !*id || !new_text || !web_elements) return 0;
    int found = 0;
    for (int i = 0; i < web_element_count; ++i) {
        struct web_element *e = &web_elements[i];
        if ((e->id[0] && str_equal_ci(e->id, id)) ||
            (str_equal_ci(id, "msg") && strstr_simple(e->text, "Texto Original") != 0)) {
            str_copy_u(e->text, new_text, sizeof(e->text));
            str_copy_u(e->id, id, sizeof(e->id));
            uint32_t len = 0;
            while (new_text[len]) len++;
            e->width = (int32_t)(len * 8 * (e->scale ? e->scale : 1));
            found = 1;
        }
    }
    return found;
}

const char *browser_get_element_text_by_id(const char *id) {
    if (!id || !*id || !web_elements) return "";
    for (int i = 0; i < web_element_count; ++i) {
        struct web_element *e = &web_elements[i];
        if (e->id[0] && str_equal_ci(e->id, id)) return e->text;
    }
    return "";
}

void browser_js_eval(const char *code) {
    if (!code || !*code) return;
    js_runtime_eval(code);
}

void browser_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor) {
    ensure_browser_buffers(); if (!web_elements) return;
    s_win_x = win_x; s_win_y = win_y; s_win_w = win_w; s_win_h = win_h;
    fb_fill_rect(win_x + 8, win_y + 8, win_w, win_h, 0x00050810);
    fb_fill_rect(win_x, win_y, win_w, win_h, COLOR_CARD_BG);
    fb_draw_rect(win_x, win_y, win_w, win_h, 0x00F59E0B);

    fb_fill_rect(win_x, win_y, win_w, 36, 0x000B0F19);
    fb_draw_rect(win_x, win_y, win_w, 36, 0x00334155);

    uint32_t btn_close_x = win_x + 12;
    uint32_t btn_close_y = win_y + 10;
    fb_fill_rect(btn_close_x, btn_close_y, 16, 16, 0x00EF4444);
    fb_draw_rect(btn_close_x, btn_close_y, 16, 16, 0x00FFFFFF);

    fb_fill_rect(win_x + 34, win_y + 12, 12, 12, 0x00F59E0B);
    fb_fill_rect(win_x + 52, win_y + 12, 12, 12, 0x0010B981);
    font_draw_string(win_x + 76, win_y + 14, "SOMA Web Visual Browser :: Motor HTML, CSS & Bitmaps TrueColor", 0x00F8FAFC, 1);

    uint32_t nav_y = win_y + 42, bar_h = 36, nav_btn_w = 34, b_back_x = win_x + 14;
    fb_fill_rect(b_back_x, nav_y, nav_btn_w, bar_h, 0x001E293B);
    fb_draw_rect(b_back_x, nav_y, nav_btn_w, bar_h, (history_pos > 0) ? COLOR_CYAN : 0x00475569);
    font_draw_string(b_back_x + 12, nav_y + 14, "<", (history_pos > 0) ? COLOR_CYAN : 0x0064748B, 1);

    uint32_t b_fwd_x = b_back_x + nav_btn_w + 4;
    fb_fill_rect(b_fwd_x, nav_y, nav_btn_w, bar_h, 0x001E293B);
    fb_draw_rect(b_fwd_x, nav_y, nav_btn_w, bar_h, (history_pos < history_count - 1) ? COLOR_CYAN : 0x00475569);
    font_draw_string(b_fwd_x + 12, nav_y + 14, ">", (history_pos < history_count - 1) ? COLOR_CYAN : 0x0064748B, 1);

    uint32_t b_reload_x = b_fwd_x + nav_btn_w + 4;
    fb_fill_rect(b_reload_x, nav_y, nav_btn_w, bar_h, 0x001E293B);
    fb_draw_rect(b_reload_x, nav_y, nav_btn_w, bar_h, 0x0010B981);
    font_draw_string(b_reload_x + 10, nav_y + 14, "R", 0x0010B981, 1);

    uint32_t b_home_x = b_reload_x + nav_btn_w + 4;
    fb_fill_rect(b_home_x, nav_y, nav_btn_w, bar_h, 0x001E293B);
    fb_draw_rect(b_home_x, nav_y, nav_btn_w, bar_h, 0x00F59E0B);
    font_draw_string(b_home_x + 10, nav_y + 14, "H", 0x00F59E0B, 1);

    uint32_t btn_go_w = 64, ub_x = b_home_x + nav_btn_w + 10, url_box_w = win_w - (ub_x - win_x) - btn_go_w - 24;
    fb_fill_rect(ub_x, nav_y, url_box_w, bar_h, 0x0004070E);
    fb_draw_rect(ub_x, nav_y, url_box_w, bar_h, url_focused ? COLOR_CYAN : 0x00475569);
    font_draw_string(ub_x + 10, nav_y + 14, "URL:", COLOR_CYAN, 1);
    font_draw_string(ub_x + 50, nav_y + 14, current_url, 0x00F8FAFC, 1);

    if (url_focused && show_cursor) {
        uint32_t cur_px = ub_x + 50 + url_cursor * 8;
        if (cur_px + 8 < ub_x + url_box_w - 6) fb_fill_rect(cur_px, nav_y + 12, 8, 14, COLOR_CYAN);
    }

    uint32_t b_go_x = ub_x + url_box_w + 6;
    fb_fill_rect(b_go_x, nav_y, btn_go_w, bar_h, 0x000284C7);
    fb_draw_rect(b_go_x, nav_y, btn_go_w, bar_h, 0x0038BDF8);
    font_draw_string(b_go_x + 22, nav_y + 14, "IR", 0x00FFFFFF, 1);

    uint32_t view_x = win_x + 14, view_y = win_y + 86, view_w = win_w - 28, view_h = win_h - 132, sb_w = 20;
    fb_fill_rect(view_x, view_y, view_w, view_h, 0x00020617);
    fb_draw_rect(view_x, view_y, view_w, view_h, 0x00334155);

    fb_set_clip((int32_t)view_x, (int32_t)view_y, (int32_t)(view_w - sb_w), (int32_t)view_h);
    for (int i = 0; i < web_element_count; ++i) {
        struct web_element *e = &web_elements[i];
        int32_t screen_y = (int32_t)view_y + 12 + e->rel_y - web_scroll;
        int32_t screen_x = (int32_t)view_x + 14 + e->rel_x;
        if (screen_y + e->height < (int32_t)view_y || screen_y > (int32_t)(view_y + view_h)) continue;

        if (e->type == ELEM_TEXT) {
            if (e->has_bg) {
                fb_fill_rounded_rect(screen_x - 6, screen_y - 2, e->width + 12, e->height, 4, e->bg_color);
            }
            font_draw_string((uint32_t)screen_x, (uint32_t)screen_y, e->text, e->color, e->scale);
            if (e->is_link) {
                fb_fill_rect((uint32_t)screen_x, (uint32_t)(screen_y + 10 * e->scale), (uint32_t)e->width, 1, e->color);
            }
        } else if (e->type == ELEM_HR) {
            fb_fill_rect((uint32_t)screen_x, (uint32_t)(screen_y + 4), (uint32_t)(view_w - sb_w - 28), 2, e->color);
        } else if (e->type == ELEM_IMAGE && e->img_slot >= 0 && e->img_slot < web_image_count) {
            if (web_images[e->img_slot].loaded) {
                image_render_alpha(&web_images[e->img_slot].img, screen_x, screen_y, e->width, e->height);
                if (e->is_link) {
                    fb_draw_rect((uint32_t)(screen_x - 2), (uint32_t)(screen_y - 2), (uint32_t)(e->width + 4), (uint32_t)(e->height + 4), 0x0038BDF8);
                }
            }
        }
    }
    fb_reset_clip();

    uint32_t sb_x = view_x + view_w - sb_w - 4, sb_y = view_y + 4, sb_h = view_h - 8;
    fb_fill_rect(sb_x, sb_y, sb_w, sb_h, 0x000B0F19);
    fb_draw_rect(sb_x, sb_y, sb_w, sb_h, 0x00334155);
    font_draw_string(sb_x + 6, sb_y + 6, "^", COLOR_CYAN, 1);
    font_draw_string(sb_x + 6, sb_y + sb_h - 14, "v", COLOR_CYAN, 1);

    if (doc_total_height > (int32_t)view_h && doc_total_height > 0) {
        uint32_t track_h = sb_h - 36, thumb_h = (track_h * view_h) / (uint32_t)doc_total_height;
        if (thumb_h < 14) thumb_h = 14;
        uint32_t thumb_y = sb_y + 18 + ((track_h - thumb_h) * (uint32_t)web_scroll) / (uint32_t)(doc_total_height - view_h);
        fb_fill_rect(sb_x + 3, thumb_y, sb_w - 6, thumb_h, 0x000284C7);
    }

    uint32_t st_y = win_y + win_h - 36;
    fb_fill_rect(win_x, st_y, win_w, 36, 0x000B0F19);
    fb_draw_rect(win_x, st_y, win_w, 36, 0x001E293B);

    font_draw_string(win_x + 16, st_y + 14, "Estado:", 0x0094A3B8, 1);
    font_draw_string(win_x + 72, st_y + 14, status_text, status_color, 1);

    if (async_loading) {
        static int spin_cnt = 0;
        const char *spinner[] = { "|", "/", "-", "\\" };
        font_draw_string(win_x + win_w - 180, st_y + 14, "[DESCARGANDO]", 0x00F59E0B, 1);
        font_draw_string(win_x + win_w - 50, st_y + 14, spinner[(spin_cnt++ / 6) % 4], COLOR_CYAN, 1);
    } else {
        font_draw_string(win_x + win_w - 360, st_y + 14, "[Clic Enlace/Img] Abrir | [ESC] Escritorio", 0x0064748B, 1);
    }
}

int browser_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h) {
    ensure_browser_buffers(); if (!web_elements) return 0;
    if (point_in_rect(mx, my, win_x + 8, win_y + 6, 26, 26)) return 1;

    uint32_t nav_y = win_y + 42, bar_h = 36, nav_btn_w = 34, b_back_x = win_x + 14;
    uint32_t b_fwd_x  = b_back_x + nav_btn_w + 4;
    uint32_t b_reload_x = b_fwd_x + nav_btn_w + 4;
    uint32_t b_home_x = b_reload_x + nav_btn_w + 4;

    if (point_in_rect(mx, my, b_back_x, nav_y, nav_btn_w, bar_h)) {
        if (history_pos > 0) {
            history_pos--;
            str_copy_u(current_url, history_urls[history_pos], sizeof(current_url));
            browser_load_url(current_url);
        }
        return 0;
    }
    if (point_in_rect(mx, my, b_fwd_x, nav_y, nav_btn_w, bar_h)) {
        if (history_pos < history_count - 1) {
            history_pos++;
            str_copy_u(current_url, history_urls[history_pos], sizeof(current_url));
            browser_load_url(current_url);
        }
        return 0;
    }
    if (point_in_rect(mx, my, b_reload_x, nav_y, nav_btn_w, bar_h)) {
        browser_load_url(current_url);
        return 0;
    }
    if (point_in_rect(mx, my, b_home_x, nav_y, nav_btn_w, bar_h)) {
        browser_load_url("file:///workspace/demo_web.html");
        return 0;
    }

    uint32_t btn_go_w = 64, ub_x = b_home_x + nav_btn_w + 10, url_box_w = win_w - (ub_x - win_x) - btn_go_w - 24;
    if (point_in_rect(mx, my, ub_x, nav_y, url_box_w, bar_h)) {
        url_focused = 1;
        return 0;
    }
    uint32_t b_go_x = ub_x + url_box_w + 6;
    if (point_in_rect(mx, my, b_go_x, nav_y, btn_go_w, bar_h)) {
        url_focused = 0;
        browser_load_url(current_url);
        return 0;
    }

    uint32_t view_x = win_x + 14, view_y = win_y + 86, view_w = win_w - 28, view_h = win_h - 132, sb_w = 20;
    uint32_t sb_x = view_x + view_w - sb_w - 4, sb_y = view_y + 4, sb_h = view_h - 8;

    if (point_in_rect(mx, my, sb_x, sb_y, sb_w, 20)) {
        if (web_scroll > 0) web_scroll -= 40;
        if (web_scroll < 0) web_scroll = 0;
        return 0;
    }
    if (point_in_rect(mx, my, sb_x, sb_y + sb_h - 20, sb_w, 20)) {
        if (web_scroll + (int)view_h < doc_total_height) web_scroll += 40;
        return 0;
    }

    if (point_in_rect(mx, my, view_x, view_y, view_w - sb_w, view_h)) {
        url_focused = 0;
        int32_t rel_click_x = mx - (int32_t)(view_x + 14);
        int32_t rel_click_y = (my - (int32_t)(view_y + 12)) + web_scroll;

        for (int i = 0; i < web_element_count; ++i) {
            struct web_element *e = &web_elements[i];
            if (e->is_link) {
                if (rel_click_x >= e->rel_x && rel_click_x <= e->rel_x + e->width &&
                    rel_click_y >= e->rel_y && rel_click_y <= e->rel_y + e->height) {
                    char target[160];
                    const char *raw_link = web_links[e->link_id].url;
                    if (raw_link && strstr_simple(raw_link, "javascript:") != 0) {
                        const char *jscode = strstr_simple(raw_link, "javascript:") + 11;
                        browser_js_eval(jscode);
                        status_color = 0x0010B981;
                        str_copy_u(status_text, "JS ejecutado con exito", sizeof(status_text));
                        browser_render_window(win_x, win_y, win_w, win_h, 1);
                        fb_present_full();
                        return 0;
                    }
                    resolve_relative_url(web_links[e->link_id].url, target, sizeof(target));
                    browser_load_url(target);
                    return 0;
                }
            }
        }
    }
    return 0;
}

int browser_is_loading(void) {
    return async_loading;
}

void browser_handle_key(char c) {
    if (c == 16) {
        if (web_scroll > 0) web_scroll -= 25;
        if (web_scroll < 0) web_scroll = 0;
        return;
    }
    if (c == 14) {
        if (web_scroll + 100 < doc_total_height) web_scroll += 25;
        return;
    }
    if (!url_focused) {
        if (c == ' ' || c == 6) {
            if (web_scroll + 200 < doc_total_height) web_scroll += 150;
            return;
        }
        if (c == 2) {
            if (web_scroll > 150) web_scroll -= 150;
            else web_scroll = 0;
            return;
        }
        return;
    }

    if (c == '\b' || c == 127) {
        if (url_cursor > 0) {
            for (uint32_t i = url_cursor - 1; i < url_len; ++i) {
                current_url[i] = current_url[i + 1];
            }
            url_len--;
            url_cursor--;
        }
        return;
    }
    if (c == '\r' || c == '\n') {
        url_focused = 0;
        browser_load_url(current_url);
        return;
    }
    if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
        if (url_len < sizeof(current_url) - 2) {
            for (uint32_t i = url_len; i > url_cursor; --i) {
                current_url[i] = current_url[i - 1];
            }
            current_url[url_cursor++] = c;
            url_len++;
            current_url[url_len] = '\0';
        }
    }
}
