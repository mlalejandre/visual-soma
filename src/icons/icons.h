#ifndef SOMA_ICONS_H
#define SOMA_ICONS_H

#include <stdint.h>
#include "fb.h"
#include "icons/icon_agent.h"
#include "icons/icon_terminal.h"
#include "icons/icon_browser.h"
#include "icons/icon_files.h"
#include "icons/icon_image.h"
#include "icons/icon_notes.h"
#include "icons/icon_memory.h"
#include "icons/icon_layout.h"
#include "icons/icon_agents.h"
#include "icons/icon_paint.h"

enum soma_icon_id {
    ICON_AGENT = 0,
    ICON_TERMINAL = 1,
    ICON_BROWSER = 2,
    ICON_FILES = 3,
    ICON_IMAGE = 4,
    ICON_NOTES = 5,
    ICON_MEMORY = 6,
    ICON_LAYOUT = 7,
    ICON_AGENTS = 8,
    ICON_PAINT = 9,
    ICON_COUNT = 10
};

static inline const uint8_t (*icon_get_data(int id))[16]
{
    switch (id) {
        case ICON_AGENT:    return icon_agent_bits;
        case ICON_TERMINAL: return icon_terminal_bits;
        case ICON_BROWSER:  return icon_browser_bits;
        case ICON_FILES:    return icon_files_bits;
        case ICON_IMAGE:    return icon_image_bits;
        case ICON_NOTES:    return icon_notes_bits;
        case ICON_MEMORY:   return icon_memory_bits;
        case ICON_LAYOUT:   return icon_layout_bits;
        case ICON_AGENTS:   return icon_agents_bits;
        case ICON_PAINT:    return icon_paint_bits;
        default:            return icon_agent_bits;
    }
}

/* Dibuja un icono de 16x16 escalado en las coordenadas (x, y) */
static inline void icon_draw(uint32_t x, uint32_t y, int icon_id, uint32_t fg_color, int scale)
{
    const uint8_t (*data)[16] = icon_get_data(icon_id);
    if (scale <= 0) scale = 1;

    fb_damage((int32_t)x, (int32_t)y, 16 * scale, 16 * scale);

    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            uint8_t val = data[r][c];
            if (val == 0) continue;

            uint32_t col = (val == 1) ? fg_color : 0x00FFFFFF;
            if (scale == 1) {
                fb_draw_pixel(x + c, y + r, col);
            } else {
                fb_fill_rect(x + c * scale, y + r * scale, scale, scale, col);
            }
        }
    }
}

#endif
