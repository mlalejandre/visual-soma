#ifndef SOMA_AGENT_H
#define SOMA_AGENT_H

#include <stdint.h>

void agent_run(const char *agent_name, const char *mission);
void agent_state_load(void);
void hpatch_command(const char *arg);
/* MAYORDOMO_V2_KNOWLEDGE: regenera /mem/herramientas_sistema.txt desde el catalogo vivo del shell */
void agent_refresh_knowledge(void);

/* Herramientas de Memoria Estructurada y Jerárquica */
int mem_tree_cmd(const char *args, char *out_buf, uint32_t max_out);
int mem_read_cmd(const char *args, char *out_buf, uint32_t max_out);
int mem_write_cmd(const char *args, char *out_buf, uint32_t max_out);
int mem_search_cmd(const char *args, char *out_buf, uint32_t max_out);

/* Orquestación y Delegación Multi-Agente */
int delegate_cmd(const char *args, char *out_buf, uint32_t max_out);

extern char agent_resume_mission[512];
extern char agent_resume_name[32];
extern int  agent_resume_pending;
extern char agent_last_response[8192];

#endif
