#ifndef MYOS_HTTP_H
#define MYOS_HTTP_H

#include <stdint.h>

struct http_response {
    char    *body;            /* 8 bytes (offset 0, 8-byte aligned) */
    uint32_t body_len;        /* 4 bytes (offset 8) */
    int      status_code;     /* 4 bytes (offset 12) -> total 16 B */
} __attribute__((aligned(8)));

int http_request(
    const uint8_t *ip,
    uint16_t port,
    const char *method,
    const char *path,
    const char *content_type,
    const char *body,
    uint32_t body_len,
    char *buf,
    uint32_t buf_size,
    struct http_response *resp,
    uint32_t timeout_ms
);

int http_get_host(
    const uint8_t *ip,
    uint16_t port,
    const char *host,
    const char *path,
    char *buf,
    uint32_t buf_size,
    struct http_response *resp,
    uint32_t timeout_ms
);

int http_get(
    const uint8_t *ip,
    uint16_t port,
    const char *path,
    char *buf,
    uint32_t buf_size,
    struct http_response *resp,
    uint32_t timeout_ms
);

int http_post_json(
    const uint8_t *ip,
    uint16_t port,
    const char *path,
    const char *json_body,
    char *buf,
    uint32_t buf_size,
    struct http_response *resp,
    uint32_t timeout_ms
);

#endif
