#include <stdint.h>
#include "console.h"
#include "http.h"
#include "io.h"
#include "mem.h"
#include "tcp.h"
#include "net.h"
#include "arch.h"

extern uint64_t tsc_ticks_per_ms;
#define TSC_PER_MS (tsc_ticks_per_ms > 0 ? tsc_ticks_per_ms : 62500ULL)

static char req_buf[16384] __attribute__((aligned(16)));

static uint32_t my_strlen(const char *s)
{
    uint32_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int buf_append(char *dst, uint32_t max, uint32_t *pos, const char *src)
{
    while (*src && *pos < max - 1) dst[(*pos)++] = *src++;
    dst[*pos] = '\0';
    return (*pos < max - 1);
}

static int buf_append_num(char *dst, uint32_t max, uint32_t *pos, uint32_t val)
{
    char tmp[12]; int n = 0;
    if (val == 0) return buf_append(dst, max, pos, "0");
    while (val > 0) { tmp[n++] = (char)('0' + (val % 10)); val /= 10; }
    while (n > 0 && *pos < max - 1) dst[(*pos)++] = tmp[--n];
    dst[*pos] = '\0';
    return (*pos < max - 1);
}

static void http_parse_response(char *buf, uint32_t len, struct http_response *resp)
{
    resp->status_code = -1;
    resp->body = 0;
    resp->body_len = 0;
    if (len < 12) return;
    if (memcmp(buf, "HTTP/1.", 7) != 0) return;

    const char *p = buf + 7;
    while (*p && *p != ' ') p++;
    if (*p == ' ') {
        p++; int code = 0;
        for (int i = 0; i < 3 && p[i] >= '0' && p[i] <= '9'; ++i) code = code * 10 + (p[i] - '0');
        resp->status_code = code;
    }

    for (uint32_t i = 0; i < len - 3; ++i) {
        if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
            resp->body = buf + i + 4;
            resp->body_len = len - (i + 4);
            return;
        }
    }
    for (uint32_t i = 0; i < len - 1; ++i) {
        if (buf[i] == '\n' && buf[i+1] == '\n') {
            resp->body = buf + i + 2;
            resp->body_len = len - (i + 2);
            return;
        }
    }
}

static int http_request_host_unlocked(
    const uint8_t *ip,
    uint16_t port,
    const char *host,
    const char *method,
    const char *path,
    const char *content_type,
    const char *body,
    uint32_t body_len,
    char *buf,
    uint32_t buf_size,
    struct http_response *resp,
    uint32_t timeout_ms
) __attribute__((aligned(16)));

static int http_request_host_unlocked(
    const uint8_t *ip,
    uint16_t port,
    const char *host,
    const char *method,
    const char *path,
    const char *content_type,
    const char *body,
    uint32_t body_len,
    char *buf,
    uint32_t buf_size,
    struct http_response *resp,
    uint32_t timeout_ms
)
{
    if (!ip || !method || !path || !buf || buf_size < 32 || !resp) return -1;

    resp->body = 0;
    resp->body_len = 0;
    resp->status_code = -1;

    uint32_t connect_timeout = timeout_ms > 30000 ? 30000 : timeout_ms;
    int h = tcp_connect(ip, port, connect_timeout);
    if (h < 0) { kprint("HTTP: TCP connect failed\n"); return -1; }

    uint32_t pos = 0;
    buf_append(req_buf, sizeof(req_buf), &pos, method);
    buf_append(req_buf, sizeof(req_buf), &pos, " ");
    buf_append(req_buf, sizeof(req_buf), &pos, path);
    buf_append(req_buf, sizeof(req_buf), &pos, " HTTP/1.1\r\nHost: ");

    if (host && host[0]) {
        buf_append(req_buf, sizeof(req_buf), &pos, host);
    } else {
        for (int i = 0; i < 4; ++i) {
            buf_append_num(req_buf, sizeof(req_buf), &pos, ip[i]);
            if (i < 3) buf_append(req_buf, sizeof(req_buf), &pos, ".");
        }
    }

    if (port != 80) {
        buf_append(req_buf, sizeof(req_buf), &pos, ":");
        buf_append_num(req_buf, sizeof(req_buf), &pos, port);
    }

    buf_append(req_buf, sizeof(req_buf), &pos, "\r\nUser-Agent: SOMA/0.2\r\nAccept: */*\r\n");
    if (content_type) {
        buf_append(req_buf, sizeof(req_buf), &pos, "Content-Type: ");
        buf_append(req_buf, sizeof(req_buf), &pos, content_type);
        buf_append(req_buf, sizeof(req_buf), &pos, "\r\n");
    }
    if (body && body_len > 0) {
        buf_append(req_buf, sizeof(req_buf), &pos, "Content-Length: ");
        buf_append_num(req_buf, sizeof(req_buf), &pos, body_len);
        buf_append(req_buf, sizeof(req_buf), &pos, "\r\n");
    }
    buf_append(req_buf, sizeof(req_buf), &pos, "Connection: close\r\n\r\n");

    int sent;
    if (body && body_len > 0 && (pos + body_len < sizeof(req_buf))) {
        memcpy(req_buf + pos, body, body_len);
        pos += body_len;
        sent = tcp_send(h, req_buf, pos);
    } else {
        sent = tcp_send(h, req_buf, pos);
        if (sent == (int)pos && body && body_len > 0) sent = tcp_send(h, body, body_len);
    }

    if (sent < 0) { kprint("HTTP: send failed\n"); tcp_close(h); return -1; }

    uint64_t start = arch_cycle_counter();
    uint64_t limit = (uint64_t)timeout_ms * TSC_PER_MS;
    uint32_t total = 0;

    while ((arch_cycle_counter() - start) < limit) {
        if (total >= buf_size - 1) break;
        uint32_t remain = buf_size - 1 - total;
        uint16_t req_bytes = (remain > 32768) ? 32768 : (uint16_t)remain;
        int n = tcp_recv(h, buf + total, req_bytes, 4000);
        if (n > 0) { total += (uint32_t)n; continue; }
        if (n == TCP_EOF || n == TCP_ERR) break;
    }

    buf[total] = '\0';
    tcp_close(h);
    if (total == 0) { kprint("HTTP: no response\n"); return -1; }

    http_parse_response(buf, total, resp);
    return resp->status_code;
}

static int http_request_host(const uint8_t *ip, uint16_t port, const char *host, const char *method, const char *path, const char *content_type, const char *body, uint32_t body_len, char *buf, uint32_t buf_size, struct http_response *resp, uint32_t timeout_ms)
{
    net_lock();
    int r = http_request_host_unlocked(ip, port, host, method, path, content_type, body, body_len, buf, buf_size, resp, timeout_ms);
    net_unlock();
    return r;
}

int http_request(const uint8_t *ip, uint16_t port, const char *method, const char *path, const char *content_type, const char *body, uint32_t body_len, char *buf, uint32_t buf_size, struct http_response *resp, uint32_t timeout_ms)
{
    return http_request_host(ip, port, 0, method, path, content_type, body, body_len, buf, buf_size, resp, timeout_ms);
}

int http_get(const uint8_t *ip, uint16_t port, const char *path, char *buf, uint32_t buf_size, struct http_response *resp, uint32_t timeout_ms)
{
    return http_request_host(ip, port, 0, "GET", path, 0, 0, 0, buf, buf_size, resp, timeout_ms);
}

int http_get_host(const uint8_t *ip, uint16_t port, const char *host, const char *path, char *buf, uint32_t buf_size, struct http_response *resp, uint32_t timeout_ms)
{
    return http_request_host(ip, port, host, "GET", path, 0, 0, 0, buf, buf_size, resp, timeout_ms);
}

int http_post_json(const uint8_t *ip, uint16_t port, const char *path, const char *json_body, char *buf, uint32_t buf_size, struct http_response *resp, uint32_t timeout_ms)
{
    uint32_t len = my_strlen(json_body);
    return http_request_host(ip, port, 0, "POST", path, "application/json", json_body, len, buf, buf_size, resp, timeout_ms);
}
