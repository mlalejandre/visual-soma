#ifndef SOMA_GUI_H
#define SOMA_GUI_H

#include <stdint.h>

int  cmd_vsoma(const char *args, char *out_buf, uint32_t max_out);
void gui_open_browser(const char *url);
void gui_open_file_manager(const char *folder);
void gui_open_memview(void);
void gui_open_layout(void);
void gui_open_agents(void);
void gui_open_paint(const char *path);

/* HER-AURA v4: razonamiento en vivo y streaming de la respuesta */
void gui_thought_update(const char *txt);
void gui_stream_begin(void);
void gui_stream_push(const char *chunk);

#endif
