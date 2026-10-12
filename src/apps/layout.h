#ifndef SOMA_LAYOUT_H
#define SOMA_LAYOUT_H

#include <stdint.h>

void layout_init(void);
void layout_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  layout_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void layout_handle_key(char c);

/* Carga un documento en formato #SOMA-LAYOUT v1 (texto plano estructurado) */
void layout_set_source(const char *src, uint32_t len);

/* Devuelve el prompt que el usuario ha escrito para el subagente maquetador */
/* Paso 2: Generar / Enter invocan agent_run("maquetador") y recargan el lienzo */
const char *layout_get_prompt(void);

#endif
