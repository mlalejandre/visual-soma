#ifndef MYOS_UDP_H
#define MYOS_UDP_H

#include <stdint.h>

/* Reserva un puerto local. 0 = OK, -1 = ocupado o sin sitio. */
int udp_bind(uint16_t port);

void udp_unbind(uint16_t port);

/* Envia un datagrama UDP. 0 = OK, -1 = error. */
int udp_sendto(
    const uint8_t *dst_ip,
    uint16_t dst_port,
    uint16_t src_port,
    const void *data,
    uint16_t len
);

/*
 * Espera un datagrama en un puerto previamente reservado.
 * Devuelve bytes copiados (>0), 0 si timeout, -1 si el puerto no existe.
 */
int udp_recvfrom(
    uint16_t port,
    void *buf,
    uint16_t max,
    uint8_t *src_ip,
    uint16_t *src_port,
    uint32_t timeout_ms
);

/* Llamada desde ip.c con el datagrama UDP (cabecera incluida). */
void udp_input(
    const uint8_t *src_ip,
    const uint8_t *dst_ip,
    const uint8_t *udp,
    uint16_t len
);

#endif
