#include "echoy_art.h"
#include "echoy_art_tables.h"
#include "echoy_art_blend.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define STEP_SECONDS (60.0 / 96.0 / 4.0)
#define PAPER 0xf1efe8
#define PATH_POINTS 272
#define BLOCK_WIDTH 16
#define BLOCK_HEIGHT 8

typedef struct { float x, y; } point_t;
typedef struct { int x0, y0, x1, y1; } bounds_t;
typedef struct { point_t a, b, c; float area; bounds_t bounds; } join_t;
struct echoy_art_workspace {
    bounds_t segments[PATH_POINTS];
    join_t joins[PATH_POINTS];
    uint16_t pixels[300 * 204];
    echoy_art_frame_t frame;
    echoy_plan_t plan;
    double voice_shape[7][141];
    int voice_note;
    bool voice_valid;
    bool valid;
};
struct echoy_art_workspace *echoy_art_workspace_create(void)
{
    return calloc(1,sizeof(struct echoy_art_workspace));
}
struct echoy_art_workspace *echoy_art_workspace_create_with_allocator(
    void *(*allocate)(void *ctx, size_t size), void *ctx)
{
    if (!allocate) return NULL;
    struct echoy_art_workspace *work = allocate(ctx, sizeof(*work));
    if (work) memset(work, 0, sizeof(*work));
    return work;
}
void echoy_art_workspace_delete(struct echoy_art_workspace *workspace) { free(workspace); }

typedef struct {
    const esp_gsp_canvas_surface_t *surface;
    struct echoy_art_workspace *workspace;
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
    r = echoy_art_blend_channel(r, (color >> 16) & 255, alpha);
    g = echoy_art_blend_channel(g, (color >> 8) & 255, alpha);
    b = echoy_art_blend_channel(b, color & 255, alpha);
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
    /* At native size the denominator is the distance already computed by the
     * caller, including the near-zero fallback. Keep scaled galleries exact. */
    if (r->sx == 1 && r->sy == 1) return 1;
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
    /* Only the antialiased boundary needs distance roots. Use a conservative
     * one-pixel margin so RGB565 coverage remains identical, including scaled
     * gallery ellipses. Reject the large empty center of stroked rings first. */
    const float margin = 1 / fminf(r->sx, r->sy);
    const float outside = outer + margin;
    const float inside = fmaxf(0, radius - stroke * .5f - margin);
    for (int y = b.y0; y < b.y1; ++y) {
        for (int x = b.x0; x < b.x1; ++x) {
            float dx = (x + .5f - r->offset) / r->sx - cx;
            float dy = (y + .5f - r->offset) / r->sy - cy;
            float squared = dx * dx + dy * dy;
            if (squared > outside * outside) continue;
            if (squared < inside * inside) {
                if (stroke <= 0) pixel(r->surface, x, y, color, opacity);
                continue;
            }
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
static unsigned prepare_joins(const raster_t *r, const point_t *points, unsigned count,
                              float half, join_t *joins)
{
    unsigned used = 0;
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
        joins[used++] = (join_t){a,b,c,area,bounds};
    }
    return used;
}
static void miter_joins(const raster_t *r, const join_t *joins, unsigned count,
                        int bx, int by, int right, int bottom, float *coverage)
{
    for (unsigned i = 0; i < count; ++i) {
        const join_t *join = &joins[i];
        bounds_t bounds = join->bounds;
        if (bounds.x1 <= bx || bounds.x0 >= right || bounds.y1 <= by || bounds.y0 >= bottom) continue;
        if (bounds.x0 < bx) bounds.x0 = bx;
        if (bounds.y0 < by) bounds.y0 = by;
        if (bounds.x1 > right) bounds.x1 = right;
        if (bounds.y1 > bottom) bounds.y1 = bottom;
        point_t a=join->a, b=join->b, c=join->c;
        float area=join->area;
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
    /* Coverage ends within half a device pixel of the stroke. A full pixel
     * at the smaller scale leaves a conservative margin: only known-empty
     * corners skip distance roots; antialiased boundaries keep the old math. */
    const float outside = half + 1 / fminf(r->sx, r->sy);
    const float outside2 = outside * outside;
    bounds_t b = clip(r, (min_x - extent) * r->sx + r->offset - 1,
                     (min_y - extent) * r->sy + r->offset - 1,
                     (max_x + extent) * r->sx + r->offset + 1,
                     (max_y + extent) * r->sy + r->offset + 1);
    /* Geometry is invariant across coverage blocks. Compute it once instead
     * of repeating bounds/normalization for every small raster tile. */
    /* All private callers use at most PATH_POINTS vertices. Open paths need
     * no join workspace, keeping their render-thread stack footprint small. */
    bounds_t local_segments[r->workspace ? 1 : count];
    join_t local_joins[r->workspace || !closed ? 1 : count];
    bounds_t *segments = r->workspace ? r->workspace->segments : local_segments;
    join_t *joins = r->workspace ? r->workspace->joins : local_joins;
    unsigned join_count = closed ? prepare_joins(r, points, count, half, joins) : 0;
    bool monotone = !closed;
    for (unsigned i = 0; i < count - !closed; ++i) {
        point_t a = points[i], z = points[(i + 1) % count];
        if (z.x < a.x) monotone = false;
        segments[i] = clip(r, (fminf(a.x,z.x)-half)*r->sx+r->offset-1,
                           (fminf(a.y,z.y)-half)*r->sy+r->offset-1,
                           (fmaxf(a.x,z.x)+half)*r->sx+r->offset+1,
                           (fmaxf(a.y,z.y)+half)*r->sy+r->offset+1);
    }
    for (int by = b.y0; by < b.y1; by += BLOCK_HEIGHT) {
        unsigned first_segment = 0;
        for (int bx = b.x0; bx < b.x1; bx += BLOCK_WIDTH) {
            const int right = bx + BLOCK_WIDTH < b.x1 ? bx + BLOCK_WIDTH : b.x1;
            const int bottom = by + BLOCK_HEIGHT < b.y1 ? by + BLOCK_HEIGHT : b.y1;
            float coverage[BLOCK_WIDTH * BLOCK_HEIGHT] = {0};
            /* Voice's open paths advance in x. Their clipped bounds do too,
             * so visit only this block's range, in the original segment order.
             * Restart the cursor for every block row; other paths scan fully. */
            if (monotone) while (first_segment < count - !closed &&
                segments[first_segment].x1 <= bx) ++first_segment;
            for (unsigned i = first_segment; i < count - !closed; ++i) {
                point_t a = points[i], z = points[(i + 1) % count];
                bounds_t segment = segments[i];
                if (monotone && segment.x0 >= right) break;
                if (segment.x1 <= bx || segment.x0 >= right || segment.y1 <= by || segment.y0 >= bottom) continue;
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
                        float nx = px - t * dx, ny = py - t * dy;
                        if (nx * nx + ny * ny > outside2) continue;
                        float distance = hypotf(nx, ny);
                        float scale = edge_scale(r, nx, ny, distance);
                        float alpha = clamp01(.5f + (half - distance) * scale) -
                                      clamp01(.5f - (half + distance) * scale);
                        unsigned index = (y - by) * BLOCK_WIDTH + x - bx;
                        coverage[index] = fmaxf(coverage[index], alpha);
                    }
                }
            }
            if (closed) miter_joins(r, joins, join_count, bx, by, right, bottom, coverage);
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
        float crossings[4][PATH_POINTS];
        unsigned hits[4] = {0};
        for (unsigned sample = 0; sample < 4; ++sample) {
            const float at_y = (y + (sample + .5f) / 4 - r->offset) / r->sy;
            for (unsigned i = 0; i < count; ++i) {
                point_t a = points[i], z = points[(i + 1) % count];
                if ((a.y <= at_y && z.y > at_y) || (z.y <= at_y && a.y > at_y)) {
                    float crossing = (a.x + (at_y-a.y)*(z.x-a.x)/(z.y-a.y))*r->sx+r->offset;
                    unsigned insert = hits[sample]++;
                    while (insert && crossings[sample][insert-1] > crossing) {
                        crossings[sample][insert] = crossings[sample][insert-1];
                        --insert;
                    }
                    crossings[sample][insert] = crossing;
                }
            }
        }
        for (int bx = b.x0; bx < b.x1; bx += BLOCK_WIDTH) {
            float coverage[BLOCK_WIDTH] = {0};
            const int right = bx + BLOCK_WIDTH < b.x1 ? bx + BLOCK_WIDTH : b.x1;
            for (unsigned sample = 0; sample < 4; ++sample) {
                for (unsigned i = 0; i + 1 < hits[sample]; i += 2) {
                    for (int x = bx; x < right; ++x) {
                        float span = fminf(x+1,crossings[sample][i+1])-fmaxf(x,crossings[sample][i]);
                        coverage[x-bx] += fmaxf(0,span)*.25f;
                    }
                }
            }
            for (int x = bx; x < right; ++x)
                pixel(r->surface,x,y,color,clamp01(coverage[x-bx])*opacity);
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
    const int note = event && event->note ? event->note : 65;
    const double frequency = (note - 50) / 18.0;
    struct echoy_art_workspace *work = r->workspace;
    /* Only pitch changes this part of the curve. Retain double precision and
     * the original multiplication order so energy/scale changes reuse it
     * without changing the RGB565 coverage. Each UI owns its own cache. */
    if (work && (!work->voice_valid || work->voice_note != note)) {
        for (unsigned row = 0; row < 7; ++row) {
            for (unsigned i = 0; i < 141; ++i) {
                double u = i / 140.0;
                work->voice_shape[row][i] =
                    sin(u * PI * (3 + frequency) + row * .22) * voice_window[i];
            }
        }
        work->voice_note = note;
        work->voice_valid = true;
    }
    static const float strength[] = {.58f, .84f, 1, .9f, .76f, .5f, .36f};
    for (unsigned row = 0; row < 7; ++row) {
        point_t points[141];
        for (unsigned i = 0; i < 141; ++i) {
            double u = i / 140.0;
            double shape = work ? work->voice_shape[row][i] :
                sin(u * PI * (3 + frequency) + row * .22) * voice_window[i];
            points[i] = (point_t){10 + i * 2,
                41 + row * 11 + shape * (25 + energy * 9) * strength[row]};
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

static void dust(const raster_t *r, const echoy_art_frame_t *frame,
                 const unsigned cells[16], unsigned step, uint32_t color)
{
    for (unsigned i = 0; i < 135; ++i) {
        double u = dust_u[i], v = dust_v[i];
        float x = 10 + u * 280;
        float y = dust_y[i];
        unsigned col = (unsigned)(u * 16);
        bool event = cells[col], hot = frame->active && col == step;
        float opacity = hot ? 1 : event ? .65f : .35 + v * .2;
        float radius = (event ? 1.8 : 1) + (hot ? 1.2 : 0) + dust_radius[i];
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
    const double *waves[] = {harmony_wave_0, harmony_wave_1, harmony_wave_2};
    /* Spread the home ribbons around the same middle line. Compact role cards
     * retain their original spacing; the note-duration marks stay below. */
    const bool home = frame->width == 300 && frame->height == 204;
    const float row_gap = home ? 44 : 29;
    /* Home stroke ink starts at screen y263, matching the slider label tops. */
    const float mark_y = home ? 175 : 155;
    for (unsigned row = 0; row < 3; ++row) {
        int tone = current && current->harmony_count > row ? current->harmony[row] : fallback[row];
        float center = 78 + ((int)row - 1) * row_gap;
        float arch = 16 + tone % 12 * .5f + (held ? energy * 2 : 0);
        point_t points[PATH_POINTS];
        for (unsigned i = 0; i < 136; ++i) {
            points[i] = (point_t){15 + i * 2, center - harmony_arch[i] * arch + waves[row][i]};
            points[271 - i] = (point_t){15 + i * 2, center + harmony_arch[i] * 10 + waves[row][i]};
        }
        fill_path(r, points, PATH_POINTS, color, held ? .1f : .06f);
        stroke_path(r, points, PATH_POINTS, true, held ? 1.8f : 1.2f, row == 1 ? color : 0xb7a587, 1);
    }
    for (size_t i = 0; i < note_count(frame); ++i) {
        const echoy_note_t *n = &frame->plan->events[i];
        if (!included(frame, n)) continue;
        float x = 10 + n->step / 16 * 280, length = fmaxf(3, n->duration / STEP_SECONDS / 16 * 280);
        float end = fminf(294, x + length);
        if (home) {
            /* Keep onset positions, but leave a visible gap between marks even
             * when their sounding durations overlap. Five pixels between line
             * endpoints also accommodates the rounded strokes. Hidden capture
             * layers must not affect spacing; event order is not assumed. */
            for (size_t next = 0; next < note_count(frame); ++next) {
                const echoy_note_t *after = &frame->plan->events[next];
                if (included(frame, after) && after->step > n->step)
                    end = fminf(end, 10 + after->step / 16 * 280 - 5);
            }
            end = fmaxf(x, end);
        }
        bool anchor = n->layer == ECHOY_ANCHOR;
        unsigned count = n->note_count ? n->note_count : 1;
        if (count > 3) count = 3;
        for (unsigned j = 0; j < count; ++j) {
            line(r, x, mark_y + j * 6, end, mark_y + j * 6,
                 anchor ? 2 : 1.5f, color, anchor ? .85f : .43f);
        }
    }
}

static void art_render(const esp_gsp_canvas_surface_t *surface, const echoy_art_frame_t *frame)
{
    clear_surface(surface, frame->background ? frame->background : PAPER);
    if (!frame->width || !frame->height) return;
    raster_t raster = {surface, frame->workspace, frame->width / 300.0f, frame->height / 204.0f,
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

static bool same_frame(const struct echoy_art_workspace *work, const echoy_art_frame_t *frame)
{
    const echoy_art_frame_t *old = &work->frame;
    return work->valid && old->role == frame->role && old->width == frame->width &&
           old->height == frame->height && old->background == frame->background &&
           old->active == frame->active && old->energy == frame->energy &&
           old->empty == frame->empty && old->anchors_only == frame->anchors_only &&
           (!frame->active || old->progress == frame->progress) &&
           (!!old->plan == !!frame->plan) &&
           (!frame->plan || !memcmp(&work->plan, frame->plan, sizeof(work->plan)));
}
void echoy_art_draw(const esp_gsp_canvas_surface_t *surface, void *ctx)
{
    const echoy_art_frame_t *frame = ctx;
    struct echoy_art_workspace *work = frame->workspace;
    if (!work || !frame->width || !frame->height || frame->width > 300 || frame->height > 204) {
        art_render(surface, frame);
        return;
    }
    if (!same_frame(work, frame)) {
        esp_gsp_canvas_surface_t full = {.pixels=work->pixels, .stride_bytes=frame->width*2,
            .width=frame->width, .height=frame->height, .pixel_format=ESP_GSP_CANVAS_PIXEL_RGB565};
        art_render(&full, frame);
        work->frame = *frame;
        if (frame->plan) work->plan = *frame->plan;
        work->valid = true;
    }
    /* GSP supplies a clipped canvas-local rectangle, with its own row stride. */
    clear_surface(surface, frame->background ? frame->background : PAPER);
    for (unsigned y=0; y<surface->height; ++y) {
        int sy=surface->y+(int)y;
        if (sy < 0 || (unsigned)sy >= frame->height) continue;
        int left=surface->x;
        int right=surface->x+surface->width;
        if (right > (int)frame->width) right=frame->width;
        if (left < right)
            memcpy((uint8_t *)surface->pixels+y*surface->stride_bytes+(left-surface->x)*2,
                   work->pixels+sy*frame->width+left,(right-left)*2);
    }
}

void echoy_feedback_draw(const esp_gsp_canvas_surface_t *surface, void *ctx)
{
    const echoy_art_frame_t *frame = ctx;
    clear_surface(surface, frame->background ? frame->background : PAPER);
    if (!frame->width || !frame->height) return;
    /* The authored 15px canvas extends the CSS 13px row by one pixel per side. */
    raster_t raster = {surface, frame->workspace, frame->width / 300.0f, frame->height / 15.0f, 0};
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
