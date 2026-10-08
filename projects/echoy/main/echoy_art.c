#include "echoy_art.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define PI 3.14159265358979323846
#define STEP_SECONDS (60.0 / 96.0 / 4.0)
#define PAPER 0xf1efe8
#define PATH_POINTS 272
#define BLOCK_WIDTH 16
#define BLOCK_HEIGHT 8

typedef struct { float x, y; } point_t;
typedef struct { int x0, y0, x1, y1; } bounds_t;
typedef struct {
    const esp_gsp_canvas_surface_t *surface;
    float sx, sy, offset;
} raster_t;

static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)(((((c >> 16) & 255) * 31 + 127) / 255 << 11) | ((((c >> 8) & 255) * 63 + 127) / 255 << 5) |
                      (((c & 255) * 31 + 127) / 255));
}

static float clamp01(float value)
{
    return fmaxf(0, fminf(1, value));
}

static uint32_t role_color(echoy_role_t role)
{
    static const uint32_t colors[] = {0xd95535, 0x496da1, 0x7b8452, 0xaa7b3d};
    return colors[(unsigned)role < 4 ? role : ECHOY_PULSE];
}

static void clear_surface(const esp_gsp_canvas_surface_t *s, uint32_t color)
{
    const uint16_t background = rgb565(color);
    for (unsigned y = 0; y < s->height; ++y) {
        uint16_t *row = (uint16_t *)((uint8_t *)s->pixels + y * s->stride_bytes);
        for (unsigned x = 0; x < s->width; ++x) {
            row[x] = background;
        }
    }
}

static void pixel(const esp_gsp_canvas_surface_t *s, int x, int y, uint32_t color, float alpha)
{
    if (x < s->x || y < s->y || x >= s->x + s->width || y >= s->y + s->height || alpha <= 0) {
        return;
    }
    uint16_t *p = (uint16_t *)((uint8_t *)s->pixels + (y - s->y) * s->stride_bytes) + (x - s->x);
    if (alpha >= 1) {
        *p = rgb565(color);
        return;
    }
    unsigned old = *p, r = ((old >> 11) & 31) * 255 / 31, g = ((old >> 5) & 63) * 255 / 63,
             b = (old & 31) * 255 / 31;
    r = (unsigned)(r + (int)((color >> 16) & 255) * (double)alpha - r * alpha);
    g = (unsigned)(g + (int)((color >> 8) & 255) * (double)alpha - g * alpha);
    b = (unsigned)(b + (int)(color & 255) * (double)alpha - b * alpha);
    *p = rgb565((r << 16) | (g << 8) | b);
}

static bounds_t clip(const raster_t *r, float x0, float y0, float x1, float y1)
{
    const esp_gsp_canvas_surface_t *s = r->surface;
    bounds_t b = {
        .x0 = (int)fmaxf(s->x, floorf(x0)),
        .y0 = (int)fmaxf(s->y, floorf(y0)),
        .x1 = (int)fminf(s->x + s->width, ceilf(x1)),
        .y1 = (int)fminf(s->y + s->height, ceilf(y1)),
    };
    return b;
}

/* Convert source-space distance into a one-device-pixel antialiasing edge.
 * Keeping both scale factors preserves the compressed ellipses in the gallery. */
static float edge_scale(const raster_t *r, float dx, float dy, float distance)
{
    return distance > .00001f ? distance / hypotf(dx / r->sx, dy / r->sy) :
           fminf(r->sx, r->sy);
}

static void circle(const raster_t *r, float cx, float cy, float radius,
                   float stroke, uint32_t color, float opacity)
{
    const float outer = radius + stroke * .5f;
    bounds_t b = clip(r, (cx - outer) * r->sx + r->offset - 1,
                     (cy - outer) * r->sy + r->offset - 1,
                     (cx + outer) * r->sx + r->offset + 1,
                     (cy + outer) * r->sy + r->offset + 1);
    for (int y = b.y0; y < b.y1; ++y) {
        for (int x = b.x0; x < b.x1; ++x) {
            float dx = (x + .5f - r->offset) / r->sx - cx;
            float dy = (y + .5f - r->offset) / r->sy - cy;
            float d = hypotf(dx, dy);
            float scale = edge_scale(r, dx, dy, d);
            float coverage = stroke > 0 ?
                clamp01(.5f + (stroke * .5f - fabsf(d - radius)) * scale) -
                clamp01(.5f - (stroke * .5f + fabsf(d - radius)) * scale) :
                clamp01(.5f + (radius - d) * scale);
            pixel(r->surface, x, y, color, coverage * opacity);
        }
    }
}

static float cross(point_t a, point_t b, point_t c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/* The ribbon paths close in a sharp tip. Canvas uses a miter join by default,
 * so extend the union of round segments with the outer join triangles. */
static void miter_joins(const raster_t *r, const point_t *points, unsigned count, float half,
                        int bx, int by, int right, int bottom, float *coverage)
{
    for (unsigned i = 0; i < count; ++i) {
        const point_t vertex = points[i];
        unsigned before = (i + count - 1) % count, after = (i + 1) % count;
        if (i && vertex.x == points[i - 1].x && vertex.y == points[i - 1].y) continue;
        while (before != i && vertex.x == points[before].x && vertex.y == points[before].y)
            before = (before + count - 1) % count;
        while (after != i && vertex.x == points[after].x && vertex.y == points[after].y)
            after = (after + 1) % count;
        point_t incoming = {vertex.x - points[before].x, vertex.y - points[before].y};
        point_t outgoing = {points[after].x - vertex.x, points[after].y - vertex.y};
        float len0 = hypotf(incoming.x, incoming.y), len1 = hypotf(outgoing.x, outgoing.y);
        if (!len0 || !len1) continue;
        incoming.x /= len0;
        incoming.y /= len0;
        outgoing.x /= len1;
        outgoing.y /= len1;
        float denominator = 1 + incoming.x * outgoing.x + incoming.y * outgoing.y;
        if (denominator <= .02f) continue; /* Canvas's default miterLimit is 10. */
        float sign = incoming.x * outgoing.y - incoming.y * outgoing.x > 0 ? 1 : -1;
        point_t a = {vertex.x + sign * incoming.y * half, vertex.y - sign * incoming.x * half};
        point_t c = {vertex.x + sign * outgoing.y * half, vertex.y - sign * outgoing.x * half};
        point_t b = {vertex.x + sign * (incoming.y + outgoing.y) * half / denominator,
                     vertex.y - sign * (incoming.x + outgoing.x) * half / denominator};
        float area = cross(a, b, c);
        if (fabsf(area) < .00001f) continue;
        bounds_t bounds = clip(r, fminf(a.x, fminf(b.x, c.x)) * r->sx + r->offset,
                              fminf(a.y, fminf(b.y, c.y)) * r->sy + r->offset,
                              fmaxf(a.x, fmaxf(b.x, c.x)) * r->sx + r->offset,
                              fmaxf(a.y, fmaxf(b.y, c.y)) * r->sy + r->offset);
        if (bounds.x0 < bx) bounds.x0 = bx;
        if (bounds.y0 < by) bounds.y0 = by;
        if (bounds.x1 > right) bounds.x1 = right;
        if (bounds.y1 > bottom) bounds.y1 = bottom;
        for (int y = bounds.y0; y < bounds.y1; ++y) {
            for (int x = bounds.x0; x < bounds.x1; ++x) {
                unsigned inside = 0;
                for (unsigned sy = 0; sy < 4; ++sy) {
                    for (unsigned sx = 0; sx < 4; ++sx) {
                        point_t p = {(x + (sx + .5f) / 4 - r->offset) / r->sx,
                                     (y + (sy + .5f) / 4 - r->offset) / r->sy};
                        inside += cross(a, b, p) * area >= 0 && cross(b, c, p) * area >= 0 &&
                                  cross(c, a, p) * area >= 0;
                    }
                }
                unsigned index = (y - by) * BLOCK_WIDTH + x - bx;
                coverage[index] = fmaxf(coverage[index], inside / 16.0f);
            }
        }
    }
}

/* A small coverage block combines all segments before blending. In particular,
 * translucent paths must not darken at every segment join. No frame-sized
 * storage or renderer state is used, so arbitrary GSP tiles render identically. */
static void stroke_path(const raster_t *r, const point_t *points, unsigned count,
                        bool closed, float width, uint32_t color, float opacity)
{
    if (count < 2 || opacity <= 0) {
        return;
    }
    float min_x = points[0].x, max_x = min_x, min_y = points[0].y, max_y = min_y;
    for (unsigned i = 1; i < count; ++i) {
        min_x = fminf(min_x, points[i].x);
        max_x = fmaxf(max_x, points[i].x);
        min_y = fminf(min_y, points[i].y);
        max_y = fmaxf(max_y, points[i].y);
    }
    const float half = width * .5f, extent = closed ? half * 10 : half;
    bounds_t b = clip(r, (min_x - extent) * r->sx + r->offset - 1,
                     (min_y - extent) * r->sy + r->offset - 1,
                     (max_x + extent) * r->sx + r->offset + 1,
                     (max_y + extent) * r->sy + r->offset + 1);
    for (int by = b.y0; by < b.y1; by += BLOCK_HEIGHT) {
        for (int bx = b.x0; bx < b.x1; bx += BLOCK_WIDTH) {
            const int right = bx + BLOCK_WIDTH < b.x1 ? bx + BLOCK_WIDTH : b.x1;
            const int bottom = by + BLOCK_HEIGHT < b.y1 ? by + BLOCK_HEIGHT : b.y1;
            float coverage[BLOCK_WIDTH * BLOCK_HEIGHT] = {0};
            for (unsigned i = 0; i < count - !closed; ++i) {
                point_t a = points[i], z = points[(i + 1) % count];
                bounds_t segment = clip(r, (fminf(a.x, z.x) - half) * r->sx + r->offset - 1,
                                       (fminf(a.y, z.y) - half) * r->sy + r->offset - 1,
                                       (fmaxf(a.x, z.x) + half) * r->sx + r->offset + 1,
                                       (fmaxf(a.y, z.y) + half) * r->sy + r->offset + 1);
                if (segment.x0 < bx) segment.x0 = bx;
                if (segment.y0 < by) segment.y0 = by;
                if (segment.x1 > right) segment.x1 = right;
                if (segment.y1 > bottom) segment.y1 = bottom;
                const float dx = z.x - a.x, dy = z.y - a.y, length2 = dx * dx + dy * dy;
                for (int y = segment.y0; y < segment.y1; ++y) {
                    for (int x = segment.x0; x < segment.x1; ++x) {
                        float px = (x + .5f - r->offset) / r->sx - a.x;
                        float py = (y + .5f - r->offset) / r->sy - a.y;
                        float t = length2 ? clamp01((px * dx + py * dy) / length2) : 0;
                        float nx = px - t * dx, ny = py - t * dy, distance = hypotf(nx, ny);
                        float scale = edge_scale(r, nx, ny, distance);
                        float alpha = clamp01(.5f + (half - distance) * scale) -
                                      clamp01(.5f - (half + distance) * scale);
                        unsigned index = (y - by) * BLOCK_WIDTH + x - bx;
                        coverage[index] = fmaxf(coverage[index], alpha);
                    }
                }
            }
            if (closed) miter_joins(r, points, count, half, bx, by, right, bottom, coverage);
            for (int y = by; y < bottom; ++y) {
                for (int x = bx; x < right; ++x) {
                    pixel(r->surface, x, y, color, coverage[(y - by) * BLOCK_WIDTH + x - bx] * opacity);
                }
            }
        }
    }
}

static void line(const raster_t *r, float ax, float ay, float bx, float by,
                 float width, uint32_t color, float opacity)
{
    const point_t points[] = {{ax, ay}, {bx, by}};
    stroke_path(r, points, 2, false, width, color, opacity);
}

static void fill_path(const raster_t *r, const point_t *points, unsigned count,
                      uint32_t color, float opacity)
{
    float min_y = points[0].y, max_y = min_y;
    for (unsigned i = 1; i < count; ++i) {
        min_y = fminf(min_y, points[i].y);
        max_y = fmaxf(max_y, points[i].y);
    }
    bounds_t b = clip(r, r->surface->x, min_y * r->sy + r->offset,
                     r->surface->x + r->surface->width, max_y * r->sy + r->offset);
    for (int y = b.y0; y < b.y1; ++y) {
        for (int bx = b.x0; bx < b.x1; bx += BLOCK_WIDTH) {
            float coverage[BLOCK_WIDTH] = {0};
            const int right = bx + BLOCK_WIDTH < b.x1 ? bx + BLOCK_WIDTH : b.x1;
            for (unsigned sample = 0; sample < 4; ++sample) {
                const float at_y = (y + (sample + .5f) / 4 - r->offset) / r->sy;
                float crossings[PATH_POINTS];
                unsigned hits = 0;
                for (unsigned i = 0; i < count; ++i) {
                    point_t a = points[i], z = points[(i + 1) % count];
                    if ((a.y <= at_y && z.y > at_y) || (z.y <= at_y && a.y > at_y)) {
                        float crossing = (a.x + (at_y - a.y) * (z.x - a.x) / (z.y - a.y)) * r->sx + r->offset;
                        unsigned insert = hits++;
                        while (insert && crossings[insert - 1] > crossing) {
                            crossings[insert] = crossings[insert - 1];
                            --insert;
                        }
                        crossings[insert] = crossing;
                    }
                }
                for (unsigned i = 0; i + 1 < hits; i += 2) {
                    for (int x = bx; x < right; ++x) {
                        float span = fminf(x + 1, crossings[i + 1]) - fmaxf(x, crossings[i]);
                        coverage[x - bx] += fmaxf(0, span) * .25f;
                    }
                }
            }
            for (int x = bx; x < right; ++x) {
                pixel(r->surface, x, y, color, clamp01(coverage[x - bx]) * opacity);
            }
        }
    }
}

static bool included(const echoy_art_frame_t *frame, const echoy_note_t *note)
{
    return !frame->anchors_only || note->layer == ECHOY_ANCHOR;
}

static size_t note_count(const echoy_art_frame_t *frame)
{
    if (!frame->plan) return 0;
    return frame->plan->count < ECHOY_MAX_EVENTS ? frame->plan->count : ECHOY_MAX_EVENTS;
}

static void onset_cells(const echoy_art_frame_t *frame, unsigned cells[16])
{
    memset(cells, 0, sizeof(unsigned) * 16);
    for (size_t i = 0; i < note_count(frame); ++i) {
        const echoy_note_t *n = &frame->plan->events[i];
        if (included(frame, n) && n->step >= 0 && n->step < 16) {
            const unsigned step = (unsigned)n->step;
            unsigned mark = n->layer == ECHOY_ANCHOR ? 2 : 1;
            if (mark > cells[step]) cells[step] = mark;
        }
    }
}

static void pulse(const raster_t *r, const echoy_art_frame_t *frame,
                  const unsigned cells[16], unsigned step, float energy, uint32_t color)
{
    for (int radius = 39; radius <= 79; radius += 20) {
        circle(r, 150, 99, radius, 1, 0xd3d7c9, 1);
    }
    float width = 1.2f;
    for (unsigned i = 0; i < 16; ++i) {
        const float theta = i / 16.0 * 2 * PI - PI / 2;
        const float x = cosf(theta), y = sinf(theta), outer = cells[i] ? 88 : 82;
        width = cells[i] == 2 ? 3.8f : cells[i] ? 2 : 1.2f;
        line(r, 150 + x * 75, 99 + y * 75, 150 + x * outer, 99 + y * outer,
             width, cells[i] == 2 ? color : cells[i] ? 0x9ba98a : 0xb9c0ae, 1);
        if (frame->active && i == step) {
            circle(r, 150 + x * 79, 99 + y * 79, 5.5f, 0, color, 1);
        }
    }
    circle(r, 150, 99, 25 + energy * 4, 0, 0x363b30, 1);
    circle(r, 150, 99, 6 + energy * 2, 0, color, 1);
    if (energy > 0) {
        /* Canvas retains the final onset's lineWidth for this ring. */
        circle(r, 150, 99, 28 + (1 - energy) * 45, width, color, energy * .4f);
    }
}

static void voice(const raster_t *r, const echoy_art_frame_t *frame,
                  double now, unsigned step, float energy, uint32_t color)
{
    const echoy_note_t *event = NULL;
    for (size_t i = 0; i < note_count(frame); ++i) {
        const echoy_note_t *n = &frame->plan->events[i];
        if (!included(frame, n)) continue;
        if (frame->active ? n->step <= now : !event && n->layer == ECHOY_ANCHOR) event = n;
    }
    const double frequency = ((event && event->note ? event->note : 65) - 50) / 18.0;
    static const float strength[] = {.58f, .84f, 1, .9f, .76f, .5f, .36f};
    for (unsigned row = 0; row < 7; ++row) {
        point_t points[141];
        for (unsigned i = 0; i < 141; ++i) {
            double u = i / 140.0;
            points[i] = (point_t){10 + i * 2,
                41 + row * 11 + sin(u * PI * (3 + frequency) + row * .22) *
                pow(sin(u * PI), 1.6) * (25 + energy * 9) * strength[row]};
        }
        uint32_t ink = row == 2 || row == 3 ? color : row < 2 ? 0xa8b7bc : 0xbec6bd;
        stroke_path(r, points, 141, false, row == 2 ? 2 : 1.1f, ink, 1);
    }
    for (size_t i = 0; i < note_count(frame); ++i) {
        const echoy_note_t *n = &frame->plan->events[i];
        if (!included(frame, n)) continue;
        bool anchor = n->layer == ECHOY_ANCHOR;
        float x = 10 + n->step / 15 * 280, y = 168 - ((n->note - 48) % 24) * 1.25f;
        line(r, x, y, x, 187, 1, anchor ? 0xa7b3b3 : 0xd1d8ca, 1);
        float radius = frame->active && floor(n->step) == step ? 4.3f : anchor ? 3 : 2;
        circle(r, x, y, radius, 0, color, anchor ? 1 : .43f);
    }
}

static double random_value(unsigned n)
{
    /* The frozen artwork uses double precision before the fractional part. */
    double value = sin(n * 127.1 + 311.7) * 43758.5453;
    return value - floor(value);
}

static void dust(const raster_t *r, const echoy_art_frame_t *frame,
                 const unsigned cells[16], unsigned step, uint32_t color)
{
    for (unsigned i = 0; i < 135; ++i) {
        double u = random_value(i + 1), v = random_value(i + 54);
        float x = 10 + u * 280;
        float y = 89 + sin(u * 7) * 13 + (v - .5) * (35 * sin(u * PI) + 10) * 2;
        unsigned col = (unsigned)(u * 16);
        bool event = cells[col], hot = frame->active && col == step;
        float opacity = hot ? 1 : event ? .65f : .35 + v * .2;
        float radius = (event ? 1.8 : 1) + (hot ? 1.2 : 0) + random_value(i + 8) * .6;
        circle(r, x, y, radius, 0, event ? color : 0xbbc4a8, opacity);
    }
    for (size_t i = 0; i < note_count(frame); ++i) {
        const echoy_note_t *n = &frame->plan->events[i];
        if (!included(frame, n)) continue;
        float x = 10 + n->step / 15 * 280;
        bool anchor = n->layer == ECHOY_ANCHOR;
        line(r, x, 173, fminf(293, x + n->duration / STEP_SECONDS * 17), 173,
             anchor ? 2 : 1, color, anchor ? .65f : .28f);
    }
}

static void harmony(const raster_t *r, const echoy_art_frame_t *frame,
                    double now, float energy, uint32_t color)
{
    const echoy_note_t *current = NULL;
    for (size_t i = 0; i < note_count(frame); ++i) {
        const echoy_note_t *n = &frame->plan->events[i];
        if (!included(frame, n)) continue;
        if (frame->active ? n->step <= now && n->step + n->duration / STEP_SECONDS > now : !current) {
            current = n;
        }
    }
    const bool held = frame->active && current;
    static const int fallback[] = {50, 57, 65};
    for (unsigned row = 0; row < 3; ++row) {
        int tone = current && current->harmony_count > row ? current->harmony[row] : fallback[row];
        float center = 49 + row * 29, arch = 16 + tone % 12 * .5f + (held ? energy * 2 : 0);
        point_t points[PATH_POINTS];
        for (unsigned i = 0; i < 136; ++i) {
            double u = i / 135.0;
            points[i] = (point_t){15 + i * 2, center - sin(PI * u) * arch + sin(u * PI * 2 + row * .3) * 5};
            points[271 - i] = (point_t){15 + i * 2, center + sin(PI * u) * 10 + sin(u * PI * 2 + row * .3) * 5};
        }
        fill_path(r, points, PATH_POINTS, color, held ? .1f : .06f);
        stroke_path(r, points, PATH_POINTS, true, held ? 1.8f : 1.2f, row == 1 ? color : 0xb7a587, 1);
    }
    for (size_t i = 0; i < note_count(frame); ++i) {
        const echoy_note_t *n = &frame->plan->events[i];
        if (!included(frame, n)) continue;
        float x = 10 + n->step / 16 * 280, length = fmaxf(3, n->duration / STEP_SECONDS / 16 * 280);
        bool anchor = n->layer == ECHOY_ANCHOR;
        unsigned count = n->note_count ? n->note_count : 1;
        if (count > 3) count = 3;
        for (unsigned j = 0; j < count; ++j) {
            line(r, x, 155 + j * 6, fminf(294, x + length), 155 + j * 6,
                 anchor ? 2 : 1.5f, color, anchor ? .85f : .43f);
        }
    }
}

void echoy_art_draw(const esp_gsp_canvas_surface_t *surface, void *ctx)
{
    const echoy_art_frame_t *frame = ctx;
    clear_surface(surface, frame->background ? frame->background : PAPER);
    if (!frame->width || !frame->height) return;
    raster_t raster = {surface, frame->width / 300.0f, frame->height / 204.0f,
                       frame->empty ? 1 : 0};
    double now = fmax(0, fmin(1, frame->progress)) * 16;
    unsigned step = (unsigned)now, cells[16];
    onset_cells(frame, cells);
    float energy = clamp01(frame->energy);
    uint32_t color = role_color(frame->role);
    switch (frame->role) {
    case ECHOY_VOICE: voice(&raster, frame, now, step, energy, color); break;
    case ECHOY_DUST: dust(&raster, frame, cells, step, color); break;
    case ECHOY_HARMONY: harmony(&raster, frame, now, energy, color); break;
    default: pulse(&raster, frame, cells, step, energy, color); break;
    }
}

void echoy_feedback_draw(const esp_gsp_canvas_surface_t *surface, void *ctx)
{
    const echoy_art_frame_t *frame = ctx;
    clear_surface(surface, frame->background ? frame->background : PAPER);
    if (!frame->width || !frame->height) return;
    /* The authored 15px canvas extends the CSS 13px row by one pixel per side. */
    raster_t raster = {surface, frame->width / 300.0f, frame->height / 15.0f, 0};
    unsigned step = (unsigned)(fmax(0, fmin(1, frame->progress)) * 16), cells[16];
    if (step > 15) step = 15;
    onset_cells(frame, cells);
    uint32_t color = role_color(frame->role);
    float total_width = 0;
    for (unsigned i = 0; i < 16; ++i) {
        total_width += frame->active && i == step ? 8 : cells[i] ? 6 : 4;
    }
    float left = 10, gap = (280 - total_width) / 15;
    for (unsigned i = 0; i < 16; ++i) {
        const bool current = frame->active && i == step;
        const float width = current ? 8 : cells[i] ? 6 : 4, x = left + width / 2;
        if (current) circle(&raster, x, 7.5f, 7, 0, 0xd95535, 18.0f / 255);
        circle(&raster, x, 7.5f, width / 2, 0,
               current || cells[i] == 1 ? color : cells[i] == 2 ? 0x414b38 : 0xcdd3c1,
               !current && cells[i] == 1 ? .45f : 1);
        left += width + gap;
    }
}
