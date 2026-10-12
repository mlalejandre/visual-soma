#ifndef SOMA_NOTEPAD_H
#define SOMA_NOTEPAD_H

#include <stdint.h>

void notepad_init(void);
void notepad_load_file(const char *path);
void notepad_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  notepad_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void notepad_handle_key(char c);

/* SOMA_PARCHE_LIVE_V1: volcado en directo (lo usa el GUI cuando escribe SOMA) */
void     notepad_live_begin(const char *path, const char *data, uint32_t len);
uint32_t notepad_live_step(uint32_t chunk);   /* devuelve los caracteres pendientes */
void     notepad_live_end(void);

#endif
