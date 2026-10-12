#ifndef SOMA_IMAGE_H
#define SOMA_IMAGE_H

#include <stdint.h>
#include <stddef.h>

#define IMAGE_FMT_UNKNOWN 0
#define IMAGE_FMT_BMP     1
#define IMAGE_FMT_PPM     2

struct image {
    uint32_t  width;
    uint32_t  height;
    uint32_t  format;
    uint32_t *pixels;
};

int  image_decode(const uint8_t *data, size_t size, struct image *img);
void image_free(struct image *img);

void image_render(const struct image *img, int32_t dx, int32_t dy, int32_t dw, int32_t dh);
void image_render_alpha(const struct image *img, int32_t dx, int32_t dy, int32_t dw, int32_t dh);

int  image_create_demo_bmp(uint8_t *out_buf, size_t max_size, uint32_t width, uint32_t height);
int  image_create_chip_bmp(uint8_t *out_buf, size_t max_size, uint32_t width, uint32_t height);

#endif
