#ifndef MYOS_NET_H
#define MYOS_NET_H

#include <stdint.h>

extern uint8_t net_mac[6];
extern uint8_t net_ip[4];
extern uint8_t net_gateway[4];

/* Inicializa el driver y la capa Ethernet. 1 = OK. */
int net_init(void);

/* Procesa como maximo una trama pendiente. 1 si habia trama. */
int net_poll(void);

/* Paso de espera: recibe una trama y cede la CPU (salvo dentro de un handler RX). */
void net_wait_step(void);

/* Cerrojo global de red (recursivo): serializa http/dns/icmp/llm entre hilos. */
void net_lock(void);
void net_unlock(void);

/* Resuelve ip -> MAC (cache + ARP request). 1 = OK, 0 = timeout. */
int arp_resolve(const uint8_t *ip, uint8_t *mac_out, uint32_t timeout_ms);

/* Vuelca la tabla de cache ARP real a out. Devuelve longitud. */
int arp_format_cache(char *out, uint32_t max);

/* Prueba de la capa Ethernet/ARP. */
void net_run_arp_test(void);

#endif
