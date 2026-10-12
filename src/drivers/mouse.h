#ifndef SOMA_MOUSE_H
#define SOMA_MOUSE_H

#include <stdint.h>

enum soma_cursor_id {
    CURSOR_ARROW     = 0,
    CURSOR_CROSSHAIR = 1,
    CURSOR_HAND      = 2,
    CURSOR_COUNT     = 3
};

void mouse_init(void);
void mouse_shutdown(void);
int  mouse_poll(int32_t *out_dx, int32_t *out_dy, int *out_left_click);
int  mouse_left_held(void);
void mouse_draw_cursor(int32_t mx, int32_t my);
void mouse_erase_cursor(void);
int  point_in_rect(int32_t px, int32_t py, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh);

/* Control dinámico de puntero */
int         mouse_set_cursor(int cursor_id);
int         mouse_set_cursor_by_name(const char *name);
int         mouse_get_current_cursor(void);
const char *mouse_get_cursor_name(int cursor_id);
const char *mouse_get_cursor_desc(int cursor_id);
int         mouse_get_cursor_count(void);

#endif
