#ifndef SOMA_MEMVIEW_H
#define SOMA_MEMVIEW_H

#include <stdint.h>

void memview_init(void);
void memview_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  memview_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void memview_handle_key(char c);
const char *memview_get_selected_path(void);

#endif
