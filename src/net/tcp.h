#ifndef MYOS_TCP_H
#define MYOS_TCP_H

#include <stdint.h>

#define TCP_EOF   (-1)
#define TCP_ERR   (-2)

int tcp_connect(const uint8_t *dst_ip, uint16_t dst_port, uint32_t timeout_ms);
int tcp_send(int h, const void *data, uint32_t len);
int tcp_recv(int h, void *buf, uint16_t max, uint32_t timeout_ms);
int tcp_close(int h);
int tcp_listen(uint16_t port);
int tcp_accept(int listen_h, uint32_t timeout_ms);
void tcp_cleanup_for_thread(uint32_t tid);

void tcp_input(
    const uint8_t *src_ip,
    const uint8_t *dst_ip,
    const uint8_t *tcp,
    uint16_t len
);

void net_run_tcp_test(void);

#endif
