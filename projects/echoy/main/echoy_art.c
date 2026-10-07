#include "echoy_art.h"
#include <math.h>
#include <stdint.h>

static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)((((c >> 16) & 255) * 31 / 255 << 11) | (((c >> 8) & 255) * 63 / 255 << 5) |
                      ((c & 255) * 31 / 255));
}
static void pixel(const esp_gsp_canvas_surface_t *s, int x, int y, uint32_t c, float alpha)
{
    if (x < s->x || y < s->y || x >= s->x + s->width || y >= s->y + s->height || alpha <= 0) {
        return;
    }
    uint16_t *p = (uint16_t *)((uint8_t *)s->pixels + (y - s->y) * s->stride_bytes) + (x - s->x);
    if (alpha >= 1) {
        *p = rgb565(c);
        return;
    }
    unsigned old = *p, r = ((old >> 11) & 31) * 255 / 31, g = ((old >> 5) & 63) * 255 / 63,
             b = (old & 31) * 255 / 31;
    r = (unsigned)(r + (int)((c >> 16) & 255) * (double)alpha - r * alpha);
    g = (unsigned)(g + (int)((c >> 8) & 255) * (double)alpha - g * alpha);
    b = (unsigned)(b + (int)(c & 255) * (double)alpha - b * alpha);
    *p = rgb565((r << 16) | (g << 8) | b);
}
static void circle(const esp_gsp_canvas_surface_t *s, float cx, float cy, float radius,
                   float stroke, uint32_t c, float opacity)
{
    int x0 = (int)floorf(cx - radius - 1), x1 = (int)ceilf(cx + radius + 1);
    int y0 = (int)floorf(cy - radius - 1), y1 = (int)ceilf(cy + radius + 1);
    if (x0 < s->x) {
        x0 = s->x;
    }
    if (y0 < s->y) {
        y0 = s->y;
    }
    if (x1 > s->x + s->width) {
        x1 = s->x + s->width;
    }
    if (y1 > s->y + s->height) {
        y1 = s->y + s->height;
    }
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float d = hypotf(x + .5f - cx, y + .5f - cy);
            float a = stroke > 0 ? stroke * .5f + .5f - fabsf(d - radius) : radius + .5f - d;
            if (a > 1) {
                a = 1;
            }
            pixel(s, x, y, c, a * opacity);
        }
    }
}
static void line(const esp_gsp_canvas_surface_t *s, float ax, float ay, float bx, float by,
                 float width, uint32_t c)
{
    int x0 = (int)floorf(fminf(ax, bx) - width), x1 = (int)ceilf(fmaxf(ax, bx) + width);
    int y0 = (int)floorf(fminf(ay, by) - width), y1 = (int)ceilf(fmaxf(ay, by) + width);
    float dx = bx - ax, dy = by - ay, den = dx * dx + dy * dy;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float t = den ? ((x + .5f - ax) * dx + (y + .5f - ay) * dy) / den : 0;
            if (t < 0) {
                t = 0;
            }
            if (t > 1) {
                t = 1;
            }
            float a = width * .5f + .5f - hypotf(x + .5f - ax - t * dx, y + .5f - ay - t * dy);
            if (a > 1) {
                a = 1;
            }
            pixel(s, x, y, c, a);
        }
    }
}
void echoy_art_draw(const esp_gsp_canvas_surface_t *s, void *ctx)
{
    const uint32_t state = atomic_load_explicit((atomic_uint *)ctx, memory_order_relaxed);
    const unsigned mask = state & 0xffff, active = (state >> 16) & 31;
    const float energy = ((state >> 21) & 127) / 127.0f;
    /* The empty HTML pad has a one-pixel border inside its fixed-size box. */
    const float cx = 150 + ((state >> 28) & 1), cy = 99 + ((state >> 28) & 1);
    for (unsigned y = 0; y < s->height; y++) {
        uint16_t *row = (uint16_t *)((uint8_t *)s->pixels + y * s->stride_bytes);
        for (unsigned x = 0; x < s->width; x++) {
            row[x] = rgb565(0xf1efe8);
        }
    }
    for (int r = 39; r <= 79; r += 20) {
        circle(s, cx, cy, (float)r, 1, 0xd3d7c9, 1);
    }
    for (unsigned i = 0; i < 16; i++) {
        float theta = (float)i / 16 * 6.28318530718f - 1.57079632679f;
        float x = cosf(theta), y = sinf(theta);
        bool hit = (mask & (1u << i)) != 0;
        line(s, cx + x * 75, cy + y * 75, cx + x * (hit ? 88 : 82), cy + y * (hit ? 88 : 82),
             hit ? 3.8f : 1.2f, hit ? 0xd95535 : 0xb9c0ae);
        if (i == active) {
            circle(s, cx + x * 79, cy + y * 79, 5.5f, 0, 0xd95535, 1);
        }
    }
    circle(s, cx, cy, 25 + energy * 4, 0, 0x363b30, 1);
    circle(s, cx, cy, 6 + energy * 2, 0, 0xd95535, 1);
    if (energy > 0) {
        circle(s, cx, cy, 28 + (1 - energy) * 45, 1, 0xd95535, energy * .4f);
    }
}

void echoy_feedback_draw(const esp_gsp_canvas_surface_t *s, void *ctx)
{
    const uint32_t state = atomic_load_explicit((atomic_uint *)ctx, memory_order_relaxed);
    const unsigned mask = state & 0xffff, active = (state >> 16) & 31;
    unsigned total_width = 0;
    for (unsigned i = 0; i < 16; ++i) {
        total_width += i == active ? 8 : (mask & (1u << i)) ? 6 : 4;
    }
    for (unsigned y = 0; y < s->height; ++y) {
        uint16_t *row = (uint16_t *)((uint8_t *)s->pixels + y * s->stride_bytes);
        for (unsigned x = 0; x < s->width; ++x) {
            row[x] = rgb565(0xf1efe8);
        }
    }
    /* Match CSS justify-content:space-between after each dot changes size. */
    const float gap = (280.0f - total_width) / 15;
    float left = 10;
    for (unsigned i = 0; i < 16; ++i) {
        const bool hit = (mask & (1u << i)) != 0;
        const float width = i == active ? 8 : hit ? 6 : 4;
        const float x = left + width / 2;
        if (i == active) {
            circle(s, x, 7.5f, 7, 0, 0xd95535, 18.0f / 255);
        }
        circle(s, x, 7.5f, width / 2, 0, i == active ? 0xd95535 : hit ? 0x414b38 : 0xcdd3c1, 1);
        left += width + gap;
    }
}
