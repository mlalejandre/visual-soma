#include "mouse.h"
#include "fb.h"
#include "io.h"
#include "arch.h"
#include "cursors/cursor_arrow.h"
#include "cursors/cursor_crosshair.h"
#include "cursors/cursor_hand.h"

#define MAX_CURSOR_DIM 32
static uint32_t cursor_argb[MAX_CURSOR_DIM * MAX_CURSOR_DIM];

struct soma_cursor_entry {
    const char *name;
    const char *desc;
    uint32_t width;
    uint32_t height;
    const uint8_t *data;
};

static const struct soma_cursor_entry CURSOR_REGISTRY[CURSOR_COUNT] = {
    [CURSOR_ARROW]     = { "arrow",     "Flecha clasica de precision",   CURSOR_ARROW_W, CURSOR_ARROW_H, (const uint8_t *)cursor_arrow_data },
    [CURSOR_CROSSHAIR] = { "crosshair", "Cruz de seleccion y diseno",   CURSOR_CROSS_W, CURSOR_CROSS_H, (const uint8_t *)cursor_cross_data },
    [CURSOR_HAND]      = { "hand",      "Mano de navegacion y enlaces",  CURSOR_HAND_W,  CURSOR_HAND_H,  (const uint8_t *)cursor_hand_data }
};

static int active_cursor_id = CURSOR_ARROW;
static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];
static int mouse_left_prev = 0;
static int mouse_left_down = 0;

static void reload_cursor_bitmap(void)
{
    const struct soma_cursor_entry *c = &CURSOR_REGISTRY[active_cursor_id];
    uint32_t w = c->width;
    uint32_t h = c->height;

    for (uint32_t r = 0; r < h; ++r) {
        for (uint32_t col = 0; col < w; ++col) {
            uint8_t pix = c->data[r * w + col];
            if (pix == 1) {
                cursor_argb[r * w + col] = 0xFF000000; /* Borde negro opaco */
            } else if (pix == 2) {
                cursor_argb[r * w + col] = 0xFFFFFFFF; /* Relleno blanco opaco */
            } else {
                cursor_argb[r * w + col] = 0x00000000; /* Transparente */
            }
        }
    }
    fb_cursor_set_image(cursor_argb, (int32_t)w, (int32_t)h);
}

int mouse_set_cursor(int cursor_id)
{
    if (cursor_id < 0 || cursor_id >= CURSOR_COUNT) return 0;
    active_cursor_id = cursor_id;
    reload_cursor_bitmap();
    return 1;
}

int mouse_set_cursor_by_name(const char *name)
{
    if (!name || !*name) return 0;
    for (int i = 0; i < CURSOR_COUNT; ++i) {
        const char *a = name, *b = CURSOR_REGISTRY[i].name;
        int eq = 1;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
            if (ca != cb) { eq = 0; break; }
            a++; b++;
        }
        if (eq && *a == '\0' && *b == '\0') {
            active_cursor_id = i;
            reload_cursor_bitmap();
            return 1;
        }
    }
    return 0;
}

int mouse_get_current_cursor(void)
{
    return active_cursor_id;
}

const char *mouse_get_cursor_name(int cursor_id)
{
    if (cursor_id < 0 || cursor_id >= CURSOR_COUNT) return "";
    return CURSOR_REGISTRY[cursor_id].name;
}

const char *mouse_get_cursor_desc(int cursor_id)
{
    if (cursor_id < 0 || cursor_id >= CURSOR_COUNT) return "";
    return CURSOR_REGISTRY[cursor_id].desc;
}

int mouse_get_cursor_count(void)
{
    return CURSOR_COUNT;
}

void mouse_erase_cursor(void)
{
    fb_cursor_hide();
}

void mouse_draw_cursor(int32_t mx, int32_t my)
{
    fb_cursor_show(mx, my);
}

static void mouse_wait_write(void)
{
    uint32_t timeout = 50000;
    while ((inb(0x64) & 0x02) && --timeout) arch_pause();
}

static int mouse_wait_read(void)
{
    uint32_t timeout = 50000;
    while (!(inb(0x64) & 0x01) && --timeout) arch_pause();
    return (timeout > 0);
}

static void mouse_write_cmd(uint8_t cmd)
{
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, cmd);
}

static uint8_t mouse_read_data(void)
{
    if (!mouse_wait_read()) return 0;
    return inb(0x60);
}

void mouse_shutdown(void)
{
#ifdef __x86_64__
    mouse_write_cmd(0xF5);
    mouse_read_data();
    mouse_wait_write();
    outb(0x64, 0xA7);
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, 0x65);

    for (int i = 0; i < 2000; ++i) {
        if (inb(0x64) & 0x01) inb(0x60);
        arch_pause();
    }
    mouse_cycle = 0;
#endif
}

void mouse_init(void)
{
#ifdef __x86_64__
    for (int i = 0; i < 2000; ++i) {
        if (inb(0x64) & 0x01) inb(0x60);
        arch_pause();
    }
    mouse_wait_write();
    outb(0x64, 0xA8);
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, 0x45);

    mouse_write_cmd(0xF6);
    mouse_read_data();
    mouse_write_cmd(0xF4);
    mouse_read_data();
    mouse_cycle = 0;

    reload_cursor_bitmap();
#endif
}

int mouse_poll(int32_t *out_dx, int32_t *out_dy, int *out_left_click)
{
    *out_dx = 0; *out_dy = 0; *out_left_click = 0;
    while (inb(0x64) & 0x01) {
        uint8_t status = inb(0x64);
        if (!(status & 0x20)) break;
        uint8_t b = inb(0x60);

        if (mouse_cycle == 0) {
            if ((b & 0x08) == 0) continue;
            mouse_packet[0] = b; mouse_cycle = 1;
        } else if (mouse_cycle == 1) {
            mouse_packet[1] = b; mouse_cycle = 2;
        } else if (mouse_cycle == 2) {
            mouse_packet[2] = b; mouse_cycle = 0;
            if (mouse_packet[0] & 0xC0) continue;

            int32_t dx = (int32_t)mouse_packet[1];
            int32_t dy = (int32_t)mouse_packet[2];
            if (mouse_packet[0] & 0x10) dx |= 0xFFFFFF00;
            if (mouse_packet[0] & 0x20) dy |= 0xFFFFFF00;

            *out_dx = dx; *out_dy = dy;

            int current_left = (mouse_packet[0] & 0x01) ? 1 : 0;
            if (current_left && !mouse_left_prev) {
                *out_left_click = 1;
            }
            mouse_left_prev = current_left;
            mouse_left_down = current_left;
            return 1;
        }
    }
    return 0;
}

int mouse_left_held(void)
{
    return mouse_left_down;
}

int point_in_rect(int32_t px, int32_t py, uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh)
{
    return (px >= (int32_t)rx && px < (int32_t)(rx + rw) &&
            py >= (int32_t)ry && py < (int32_t)(ry + rh));
}
