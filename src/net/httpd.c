#include "arch.h"
#include "httpd.h"
#include "image.h"
#include "tcp.h"
#include "console.h"
#include "fs.h"
#include "mem.h"
#include "pmm.h"
#include "sysinfo.h"
#include "thread.h"
#include "shell.h"
#include "rtc.h"
#include "idt.h"
#include "virtio_net.h"

static int httpd_listen_h = -1;
static uint32_t httpd_hits = 0;

static const char *httpd_strstr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return haystack;
    }
    return 0;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == *b);
}

static void buf_append(char *dst, uint32_t max, uint32_t *pos, const char *src)
{
    while (*src && *pos < max - 1) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = '\0';
}

static void buf_append_dec(char *dst, uint32_t max, uint32_t *pos, uint64_t v)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        if (*pos + 1 < max) dst[(*pos)++] = '0';
        dst[*pos] = '\0';
        return;
    }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n && *pos + 1 < max) dst[(*pos)++] = tmp[--n];
    dst[*pos] = '\0';
}

static void render_dashboard(char *resp_body, uint32_t max_len, uint32_t *body_pos)
{
    uint64_t ms = timer_get_uptime_ms();
    uint32_t s = (uint32_t)(ms / 1000);
    uint32_t m = s / 60; s %= 60;
    uint32_t h = m / 60; m %= 60;

    size_t f_free = 0, f_used = 0, f_total = 0;
    pmm_get_stats(&f_free, &f_used, &f_total);

    size_t h_used = 0, h_free = 0;
    kheap_stats(&h_used, &h_free);

    uint32_t files_u = 0, bytes_u = 0, dirty_c = 0;
    vfs_get_stats(&files_u, &bytes_u, &dirty_c);

    buf_append(resp_body, max_len, body_pos,
        "<!DOCTYPE html>\n<html>\n<head>\n"
        "<title>SOMA v0.2 :: Bare-Metal Web Dashboard</title>\n"
        "<meta charset='utf-8'>\n"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>\n"
        "<style>\n"
        ":root{--bg:#090d13;--card:#161b22;--border:#30363d;--text:#c9d1d9;--accent:#58a6ff;--green:#3fb950;--purple:#bc8cff;}\n"
        "body{background:var(--bg);color:var(--text);font-family:ui-monospace,SFMono-Regular,Consolas,monospace;padding:24px;margin:0 auto;max-width:1100px;}\n"
        "h1{color:var(--accent);display:flex;align-items:center;gap:12px;border-bottom:1px solid var(--border);padding-bottom:12px;margin-top:0;}\n"
        ".badge{background:#238636;color:#fff;padding:4px 10px;border-radius:12px;font-size:12px;font-weight:bold;}\n"
        ".badge-purple{background:#8957e5;color:#fff;padding:4px 10px;border-radius:12px;font-size:12px;font-weight:bold;}\n"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px;margin-bottom:20px;}\n"
        ".card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:18px;}\n"
        ".card h3{margin-top:0;color:var(--accent);border-bottom:1px solid var(--border);padding-bottom:8px;}\n"
        "pre{background:#04070a;padding:14px;border-radius:6px;color:#7ee787;overflow-x:auto;font-size:13px;line-height:1.4;}\n"
        "table{width:100%;border-collapse:collapse;}\n"
        "th,td{text-align:left;padding:8px;border-bottom:1px solid var(--border);font-size:13px;}\n"
        "th{color:var(--accent);}\n"
        "a{color:var(--accent);text-decoration:none;font-weight:bold;}\n"
        "a:hover{text-decoration:underline;}\n"
        ".btn-link{background:#21262d;border:1px solid var(--border);color:var(--text);padding:4px 8px;border-radius:4px;font-size:12px;}\n"
        ".btn-link:hover{background:#30363d;color:#fff;}\n"
        "</style>\n</head>\n<body>\n"
        "<h1>SOMA v0.2 <span class='badge'>Ring 0 HTTP Server</span> <span class='badge-purple'>Bare-Metal</span></h1>\n"
        "<div class='grid'>\n"
        "<div class='card'>\n"
        "<h3>Telemetria de Hardware</h3>\n"
        "<table>\n"
        "<tr><td><b>Uptime:</b></td><td>"
    );
    if (h > 0) { buf_append_dec(resp_body, max_len, body_pos, h); buf_append(resp_body, max_len, body_pos, "h "); }
    buf_append_dec(resp_body, max_len, body_pos, m); buf_append(resp_body, max_len, body_pos, "m ");
    buf_append_dec(resp_body, max_len, body_pos, s); buf_append(resp_body, max_len, body_pos, "s</td></tr>\n"
        "<tr><td><b>RAM (PMM):</b></td><td>"
    );
    buf_append_dec(resp_body, max_len, body_pos, (f_used * 4096ULL) / (1024ULL * 1024ULL));
    buf_append(resp_body, max_len, body_pos, " / ");
    buf_append_dec(resp_body, max_len, body_pos, (f_total * 4096ULL) / (1024ULL * 1024ULL));
    buf_append(resp_body, max_len, body_pos, " MiB</td></tr>\n"
        "<tr><td><b>Heap Dinamico:</b></td><td>"
    );
    buf_append_dec(resp_body, max_len, body_pos, h_free / 1024ULL);
    buf_append(resp_body, max_len, body_pos, " KiB libres</td></tr>\n"
        "<tr><td><b>Almacenamiento:</b></td><td>"
    );
    buf_append_dec(resp_body, max_len, body_pos, files_u);
    buf_append(resp_body, max_len, body_pos, "/64 inodos (");
    buf_append_dec(resp_body, max_len, body_pos, bytes_u / 1024ULL);
    buf_append(resp_body, max_len, body_pos, " KiB usados)</td></tr>\n"
        "<tr><td><b>Hits HTTP:</b></td><td>"
    );
    buf_append_dec(resp_body, max_len, body_pos, httpd_hits);
    buf_append(resp_body, max_len, body_pos, "</td></tr>\n</table>\n</div>\n");

    buf_append(resp_body, max_len, body_pos,
        "<div class='card'>\n"
        "<h3>Pizarra Multi-Agente (/agent/blackboard.txt)</h3>\n"
        "<pre>"
    );
    static char bb_txt[1024];
    int r = vfs_read("/agent/blackboard.txt", bb_txt, sizeof(bb_txt) - 1);
    if (r > 0) {
        bb_txt[r] = '\0';
        buf_append(resp_body, max_len, body_pos, bb_txt);
    } else {
        buf_append(resp_body, max_len, body_pos, "(Pizarra vacia o inicializandose)");
    }
    buf_append(resp_body, max_len, body_pos, "</pre>\n</div>\n</div>\n");

    buf_append(resp_body, max_len, body_pos,
        "<div class='card'>\n"
        "<h3>Explorador de Archivos VFS V2 (Espacio de Trabajo y Usuario)</h3>\n"
        "<table>\n"
        "<tr><th>Ruta</th><th>Tamano</th><th>Accion</th></tr>\n"
    );

    static char file_list_buf[2048];
    int flen = vfs_format_list_ext(0, 0, file_list_buf, sizeof(file_list_buf) - 1);
    if (flen > 0) {
        file_list_buf[flen] = '\0';
        const char *p = file_list_buf;
        if (httpd_strstr(p, "Archivos: ") == p) p += 10;

        while (*p) {
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;

            char fname[64];
            uint32_t fp = 0;
            while (*p && *p != ',' && fp < sizeof(fname) - 1) {
                fname[fp++] = *p++;
            }
            fname[fp] = '\0';

            buf_append(resp_body, max_len, body_pos, "<tr><td><b>");
            buf_append(resp_body, max_len, body_pos, fname);
            buf_append(resp_body, max_len, body_pos, "</b></td><td>");

            static char test_read[64];
            int fsize = vfs_read(fname, test_read, sizeof(test_read) - 1);
            if (fsize >= 0) {
                buf_append_dec(resp_body, max_len, body_pos, (uint64_t)fsize);
                buf_append(resp_body, max_len, body_pos, " B");
            } else {
                buf_append(resp_body, max_len, body_pos, "-");
            }

            buf_append(resp_body, max_len, body_pos, "</td><td><a class='btn-link' href='");
            buf_append(resp_body, max_len, body_pos, fname);
            buf_append(resp_body, max_len, body_pos, "' target='_blank'>Ver Archivo</a></td></tr>\n");
        }
    }

    buf_append(resp_body, max_len, body_pos,
        "</table>\n</div>\n"
        "<div class='card' style='margin-top:16px;'>\n"
        "<h3>Endpoints REST API</h3>\n"
        "<p>"
        "<a class='btn-link' href='/api/telemetry'>/api/telemetry</a> "
        "<a class='btn-link' href='/api/fs'>/api/fs</a> "
        "<a class='btn-link' href='/api/blackboard'>/api/blackboard</a> "
        "<a class='btn-link' href='/proc/version'>/proc/version</a> "
        "<a class='btn-link' href='/proc/meminfo'>/proc/meminfo</a> "
        "<a class='btn-link' href='/dev/rtc'>/dev/rtc</a>"
        "</p>\n</div>\n</body>\n</html>\n"
    );
}

static void serve_client(int h)
{
    static char rx_buf[2048];
    static char *resp_body = 0;
    static char resp_hdr[512];

    if (!resp_body) {
        resp_body = (char *)kmalloc(16384);
        if (!resp_body) return;
    }

    int n = tcp_recv(h, rx_buf, sizeof(rx_buf) - 1, 1500);
    if (n <= 0) return;
    rx_buf[n] = '\0';
    httpd_hits++;

    char path[128];
    path[0] = '\0';
    if (rx_buf[0] == 'G' && rx_buf[1] == 'E' && rx_buf[2] == 'T' && rx_buf[3] == ' ') {
        const char *p = rx_buf + 4;
        while (*p == ' ') p++;
        uint32_t pp = 0;
        while (*p && *p != ' ' && *p != '?' && pp < sizeof(path) - 1) {
            path[pp++] = *p++;
        }
        path[pp] = '\0';
    }

    uint32_t body_pos = 0;
    const char *content_type = "text/html; charset=utf-8";
    int status_code = 200;

    if (str_eq(path, "/") || str_eq(path, "/index.html") || str_eq(path, "/dashboard")) {
        render_dashboard(resp_body, 16384, &body_pos);
    } else if (str_eq(path, "/api/telemetry") || str_eq(path, "/api/status")) {
        content_type = "application/json";
        uint64_t ms = timer_get_uptime_ms();
        size_t f_free = 0, f_used = 0, f_total = 0;
        pmm_get_stats(&f_free, &f_used, &f_total);
        size_t h_used = 0, h_free = 0;
        kheap_stats(&h_used, &h_free);
        uint32_t tx_p = 0, rx_p = 0; uint64_t tx_b = 0, rx_b = 0;
        virtio_net_stats(&tx_p, &rx_p, &tx_b, &rx_b);

        buf_append(resp_body, 16384, &body_pos, "{\n");
        buf_append(resp_body, 16384, &body_pos, "  \"os\": \"SOMA\",\n");
        buf_append(resp_body, 16384, &body_pos, "  \"version\": \"0.2-release\",\n");
        buf_append(resp_body, 16384, &body_pos, "  \"arch\": \"" MYOS_ARCH_NAME "\",\n");
        buf_append(resp_body, 16384, &body_pos, "  \"uptime_ms\": ");
        buf_append_dec(resp_body, 16384, &body_pos, ms);
        buf_append(resp_body, 16384, &body_pos, ",\n  \"ram_used_mb\": ");
        buf_append_dec(resp_body, 16384, &body_pos, (f_used * 4096ULL) / (1024ULL * 1024ULL));
        buf_append(resp_body, 16384, &body_pos, ",\n  \"ram_total_mb\": ");
        buf_append_dec(resp_body, 16384, &body_pos, (f_total * 4096ULL) / (1024ULL * 1024ULL));
        buf_append(resp_body, 16384, &body_pos, ",\n  \"heap_free_kb\": ");
        buf_append_dec(resp_body, 16384, &body_pos, h_free / 1024ULL);
        buf_append(resp_body, 16384, &body_pos, ",\n  \"net_tx_bytes\": ");
        buf_append_dec(resp_body, 16384, &body_pos, tx_b);
        buf_append(resp_body, 16384, &body_pos, ",\n  \"net_rx_bytes\": ");
        buf_append_dec(resp_body, 16384, &body_pos, rx_b);
        buf_append(resp_body, 16384, &body_pos, ",\n  \"http_hits\": ");
        buf_append_dec(resp_body, 16384, &body_pos, (uint64_t)httpd_hits);
        buf_append(resp_body, 16384, &body_pos, "\n}\n");
    } else if (str_eq(path, "/api/blackboard")) {
        content_type = "application/json";
        static char bb_txt[1024];
        int r = vfs_read("/agent/blackboard.txt", bb_txt, sizeof(bb_txt) - 1);
        if (r > 0) bb_txt[r] = '\0'; else bb_txt[0] = '\0';

        buf_append(resp_body, 16384, &body_pos, "{\n  \"blackboard\": \"");
        for (int i = 0; bb_txt[i] && body_pos < 16000; ++i) {
            if (bb_txt[i] == '\"' || bb_txt[i] == '\\') {
                buf_append(resp_body, 16384, &body_pos, "\\");
            }
            if (bb_txt[i] == '\n') {
                buf_append(resp_body, 16384, &body_pos, "\\n");
                continue;
            }
            if ((unsigned char)bb_txt[i] >= 32) {
                char ch[2] = { bb_txt[i], 0 };
                buf_append(resp_body, 16384, &body_pos, ch);
            }
        }
        buf_append(resp_body, 16384, &body_pos, "\"\n}\n");
    } else if (str_eq(path, "/api/fs")) {
        content_type = "application/json";
        uint32_t files_u = 0, bytes_u = 0, dirty_c = 0;
        vfs_get_stats(&files_u, &bytes_u, &dirty_c);

        buf_append(resp_body, 16384, &body_pos, "{\n  \"inodos_usados\": ");
        buf_append_dec(resp_body, 16384, &body_pos, files_u);
        buf_append(resp_body, 16384, &body_pos, ",\n  \"inodos_max\": 64,\n  \"bytes_usados\": ");
        buf_append_dec(resp_body, 16384, &body_pos, bytes_u);
        buf_append(resp_body, 16384, &body_pos, ",\n  \"max_file_size\": 16384\n}\n");
    } else {
        if (httpd_strstr(path, "/agent/state") || httpd_strstr(path, "_stm.txt") || httpd_strstr(path, "/sys/")) {
            status_code = 403;
            buf_append(resp_body, 16384, &body_pos, "403 Forbidden - Acceso denegado a archivo protegido de sistema\n");
        } else {
            content_type = "text/plain; charset=utf-8";
            if (httpd_strstr(path, ".bmp")) content_type = "image/bmp";
            else if (httpd_strstr(path, ".html")) content_type = "text/html; charset=utf-8";

            int r = vfs_read(path, resp_body, 16383);
            if (r >= 0) {
                body_pos = (uint32_t)r;
            } else {
                status_code = 404;
                buf_append(resp_body, 16384, &body_pos, "404 Not Found - Archivo no encontrado en RamFS\n");
            }
        }
    }

    uint32_t hdr_pos = 0;
    if (status_code == 200) {
        buf_append(resp_hdr, sizeof(resp_hdr), &hdr_pos, "HTTP/1.1 200 OK\r\n");
    } else if (status_code == 403) {
        buf_append(resp_hdr, sizeof(resp_hdr), &hdr_pos, "HTTP/1.1 403 Forbidden\r\n");
    } else {
        buf_append(resp_hdr, sizeof(resp_hdr), &hdr_pos, "HTTP/1.1 404 Not Found\r\n");
    }
    buf_append(resp_hdr, sizeof(resp_hdr), &hdr_pos, "Server: SOMA-Kernel/0.2\r\n");
    buf_append(resp_hdr, sizeof(resp_hdr), &hdr_pos, "Content-Type: ");
    buf_append(resp_hdr, sizeof(resp_hdr), &hdr_pos, content_type);
    buf_append(resp_hdr, sizeof(resp_hdr), &hdr_pos, "\r\nContent-Length: ");
    buf_append_dec(resp_hdr, sizeof(resp_hdr), &hdr_pos, (uint64_t)body_pos);
    buf_append(resp_hdr, sizeof(resp_hdr), &hdr_pos, "\r\nConnection: close\r\n\r\n");

    tcp_send(h, resp_hdr, hdr_pos);
    if (body_pos > 0) {
        tcp_send(h, resp_body, body_pos);
    }
}

void httpd_thread_func(void *arg)
{
    (void)arg;
    httpd_listen_h = tcp_listen(80);
    if (httpd_listen_h < 0) {
        kprint("HTTPD: Error abriendo socket listen en puerto 80\n");
        return;
    }
    kprint("HTTPD: Servidor Web Bare-Metal activo en puerto 80 (http://localhost:8090/)\n");

    for (;;) {
        int client = tcp_accept(httpd_listen_h, 200);
        if (client >= 0) {
            serve_client(client);
            tcp_close(client);
        }
        thread_sleep(10);
    }
}

void httpd_init(void)
{
    thread_create("httpd", httpd_thread_func, 0);
}

int cmd_httpd(const char *args, char *out_buf, uint32_t max_out)
{
    (void)args;
    uint32_t p = 0;
    kprint("\nESTADO DEL SERVIDOR WEB HTTPD (Ring 0):\n----------------------------------------\n");
    kprint("  Puerto TCP:       80 (reenviado a http://localhost:8090/ en el host)\n");
    kprint("  Estado:           ESCUCHANDO (Daemon KThread activo)\n");
    kprint("  Peticiones (Hits): "); kprint_dec(httpd_hits); kprint("\n");
    kprint("  Dashboard URL:    http://localhost:8090/\n");
    kprint("  REST Endpoints:   /api/telemetry , /api/fs , /api/blackboard\n");
    kprint("----------------------------------------\n");

    buf_append(out_buf, max_out, &p, "HTTPD: Puerto 80 activo | Hits: ");
    buf_append_dec(out_buf, max_out, &p, (uint64_t)httpd_hits);
    return 1;
}
