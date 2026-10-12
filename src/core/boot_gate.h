#ifndef MYOS_BOOT_GATE_H
#define MYOS_BOOT_GATE_H

#include <stdint.h>

/* Ejecuta la suite integral de 9 fases del sistema (0 = OK, -1 = fallo critico) */
int boot_gate_run_test_suite(char *out_buf, uint32_t max_out);

/* Verificacion ultrarrapida en modo canary (<1s, local, determinista) */
void boot_gate_check(void);

#endif
