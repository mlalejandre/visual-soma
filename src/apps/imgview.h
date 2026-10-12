#ifndef SOMA_IMGVIEW_H
#define SOMA_IMGVIEW_H

#include <stdint.h>

void imgview_init(void);
void imgview_load_file(const char *path);
void imgview_render_window(uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h, int show_cursor);
int  imgview_handle_click(int32_t mx, int32_t my, uint32_t win_x, uint32_t win_y, uint32_t win_w, uint32_t win_h);
void imgview_handle_key(char c);

#endif
