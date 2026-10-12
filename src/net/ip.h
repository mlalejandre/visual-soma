#ifndef MYOS_IP_H
#define MYOS_IP_H

#include <stdint.h>

/* Checksum de Internet (RFC 1071). Sobre datos con checksum valido da 0. */
uint16_t ip_checksum(const void *data, uint16_t len);

/*
 * Envia un datagrama IPv4. Resuelve el siguiente salto (destino si esta
 * en la subred, si no el gateway) mediante ARP. 0 = OK, -1 = error.
 */
int ip_send(
    const uint8_t *dst_ip,
    uint8_t proto,
    const void *payload,
    uint16_t len
);

/* Trama Ethernet completa recibida (llamada desde net.c). */
void ip_input(const uint8_t *frame, uint16_t len);

/* Un ping. 1 = respuesta, 0 = timeout, -1 = error al enviar. */
int icmp_ping(const uint8_t *dst_ip, uint16_t seq, uint32_t timeout_ms);

/* Prueba de la capa IPv4 + ICMP. */
void net_run_ping_test(void);

#endif
