#include "echoy_strip_image.h"
#include "../ui/strip/labels.h"
#include <stdlib.h>
#include <string.h>
static void be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}
bool echoy_strip_glyph_qoi(bool english, unsigned kind, uint8_t opacity, uint32_t color_rgb888,
                           uint8_t **out, size_t *size)
{
    if (out) {
        *out = NULL;
    }
    if (size) {
        *size = 0;
    }
    if (!out || !size || kind > 3) {
        return false;
    }
    const echoy_strip_glyph_mask_t *m = &echoy_strip_glyph_masks[english][kind];
    size_t count = (size_t)m->width * m->height;
    uint8_t *p = malloc(22 + count * 5);
    if (!p) {
        return false;
    }
    memcpy(p, "qoif", 4);
    be32(p + 4, m->width);
    be32(p + 8, m->height);
    p[12] = 4;
    p[13] = 0;
    size_t at = 14;
    uint32_t previous = 255;
    unsigned run = 0;
    for (unsigned y = 0; y < m->height; y++) {
        for (unsigned x = 0; x < m->width; x++) {
            uint8_t alpha =
                (uint8_t)((m->alpha[y * m->stride + x] * (unsigned)opacity + 127) / 255);
            uint32_t pixel = (color_rgb888 << 8) | alpha;
            if (pixel == previous) {
                if (++run == 62) {
                    p[at++] = 0xc0 | (run - 1);
                    run = 0;
                }
            } else {
                if (run) {
                    p[at++] = 0xc0 | (run - 1);
                    run = 0;
                }
                p[at++] = 0xff;
                p[at++] = pixel >> 24;
                p[at++] = pixel >> 16;
                p[at++] = pixel >> 8;
                p[at++] = alpha;
                previous = pixel;
            }
        }
    }
    if (run) {
        p[at++] = 0xc0 | (run - 1);
    }
    memset(p + at, 0, 7);
    p[at + 7] = 1;
    *out = p;
    *size = at + 8;
    return true;
}
