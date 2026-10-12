#ifndef MYOS_VIRTIO_NET_H
#define MYOS_VIRTIO_NET_H

#include <stdint.h>

/*
 * Driver VirtIO-net (legacy PCI, I/O BAR).
 *
 * Las tramas de esta API son tramas Ethernet puras: el driver
 * anade/quita la cabecera virtio_net_hdr internamente.
 */

/* Devuelve 1 si el dispositivo queda operativo, 0 si falla. */
int virtio_net_init(void);

/* MAC del dispositivo (6 bytes). Valido tras virtio_net_init(). */
const uint8_t *virtio_net_mac(void);

/* Envia una trama Ethernet (se rellena hasta 60 bytes). 0 = OK, -1 = error. */
int virtio_net_send(const void *frame, uint16_t len);

/*
 * Si hay una trama recibida, la copia a out (max bytes), escribe su
 * longitud en *len_out y devuelve 1. Si no hay ninguna devuelve 0.
 */
int virtio_net_poll(void *out, uint16_t max, uint16_t *len_out);

void virtio_net_stats(uint32_t *tx_pkts, uint32_t *rx_pkts, uint64_t *tx_b, uint64_t *rx_b);

#endif
