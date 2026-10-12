#ifndef MYOS_LLM_H
#define MYOS_LLM_H

#include <stdint.h>

extern char llm_last_reply[4096];
extern int  llm_test_passed;

/* Comprueba /health en llama-server. 1 = 200 OK. */
int  llm_health(void);

/*
 * Envia prompt al LLM (POST /v1/chat/completions) y extrae el texto
 * de la respuesta en reply_out. Devuelve 1 si OK, 0 en fallo.
 */
int  llm_chat(const char *prompt, char *reply_out, uint32_t reply_max, uint32_t timeout_ms);
int  llm_diagnose(const char *user_question, char *reply_out, uint32_t reply_max, uint32_t timeout_ms);

/* Envia prompt con grammar forzado para respuesta JSON estricta */
int  llm_chat_json(const char *prompt, char *reply_out, uint32_t reply_max, uint32_t timeout_ms);

/* Extrae un campo clave de un objeto JSON bare-metal */
int  llm_json_get(const char *json, const char *key, char *out, uint32_t max);

/* Suite de verificacion HTTP + LLM. */
void net_run_llm_test(void);

#endif
