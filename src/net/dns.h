#ifndef MYOS_DNS_H
#define MYOS_DNS_H

#include <stdint.h>

/* Servidor DNS (SLIRP: 10.0.2.3). */
extern uint8_t net_dns[4];

/* Resuelve un registro A. 1 = OK, 0 = fallo/timeout. */
int dns_resolve(const char *name, uint8_t *ip_out, uint32_t timeout_ms);

/* Prueba de UDP + DNS. */
void net_run_dns_test(void);

#endif
