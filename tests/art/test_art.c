#include "echoy_art.h"
#include "echoy_art_frame.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 300, HEIGHT = 204, GUARD = 32 };
static unsigned checks, failures;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static uint16_t color(unsigned rgb)
{
    return (uint16_t)(((((rgb >> 16) & 255) * 31 + 127) / 255 << 11) |
                      ((((rgb >> 8) & 255) * 63 + 127) / 255 << 5) | (((rgb & 255) * 31 + 127) / 255));
}

static echoy_plan_t fixture(void)
{
    echoy_plan_t p = { .count = 4 };
    p.events[0] = (echoy_note_t){ .step = 0, .layer = ECHOY_ANCHOR, .note = 60,
        .duration = .3125, .notes = {50, 57, 65}, .note_count = 3,
        .harmony = {50, 57, 65}, .harmony_count = 3 };
    p.events[1] = (echoy_note_t){ .step = 4, .layer = ECHOY_SUPPORT, .note = 67,
        .duration = .15625, .notes = {53}, .note_count = 1,
        .harmony = {53, 60, 69}, .harmony_count = 3 };
    p.events[2] = (echoy_note_t){ .step = 8.25, .layer = ECHOY_ANCHOR, .note = 72,
        .duration = .625, .notes = {55, 62, 70}, .note_count = 3,
        .harmony = {55, 62, 70}, .harmony_count = 3 };
    p.events[3] = (echoy_note_t){ .step = 15, .layer = ECHOY_ENDING, .note = 65,
        .duration = .1, .notes = {65}, .note_count = 1,
        .harmony = {50, 57, 65}, .harmony_count = 3 };
    return p;
}

static uint16_t *render(esp_gsp_canvas_draw_cb_t draw, echoy_art_frame_t *frame)
{
    uint16_t *pixels = malloc(frame->width * frame->height * sizeof(*pixels));
    assert(pixels);
    memset(pixels, 0xa5, frame->width * frame->height * sizeof(*pixels));
    esp_gsp_canvas_surface_t s = { .pixels = pixels, .stride_bytes = frame->width * 2,
        .width = frame->width, .height = frame->height,
        .pixel_format = ESP_GSP_CANVAS_PIXEL_RGB565 };
    draw(&s, frame);
    return pixels;
}

/* A tile receives its own pointer and row stride, never a full-frame pointer.
 * Guards catch writes to row padding, neighboring tiles, and either buffer end. */
static void tiled_matches(esp_gsp_canvas_draw_cb_t draw, echoy_art_frame_t frame,
                          unsigned tile_width, unsigned tile_height)
{
    uint16_t *full = render(draw, &frame);
    for (unsigned y = 0; y < frame.height; y += tile_height) {
        for (unsigned x = 0; x < frame.width; x += tile_width) {
            unsigned w = frame.width - x < tile_width ? frame.width - x : tile_width;
            unsigned h = frame.height - y < tile_height ? frame.height - y : tile_height;
            const size_t stride = w * 2 + 12, size = stride * h + GUARD * 2;
            unsigned char *bytes = malloc(size);
            assert(bytes);
            memset(bytes, 0x6c, size);
            esp_gsp_canvas_surface_t s = { .pixels = bytes + GUARD, .stride_bytes = stride,
                .x = x, .y = y, .width = w, .height = h,
                .pixel_format = ESP_GSP_CANVAS_PIXEL_RGB565 };
            draw(&s, &frame);
            bool equal = true, guards = true;
            for (unsigned row = 0; row < h; ++row) {
                equal &= memcmp(bytes + GUARD + row * stride,
                                full + (y + row) * frame.width + x, w * 2) == 0;
                for (size_t i = w * 2; i < stride; ++i)
                    guards &= bytes[GUARD + row * stride + i] == 0x6c;
            }
            for (unsigned i = 0; i < GUARD; ++i)
                guards &= bytes[i] == 0x6c && bytes[size - GUARD + i] == 0x6c;
            CHECK(equal);
            CHECK(guards);
            free(bytes);
        }
    }
    free(full);
}

static void pulse_layers(echoy_art_frame_t frame)
{
    frame.role = ECHOY_PULSE;
    uint16_t *full = render(echoy_art_draw, &frame);
    CHECK(full[15 * WIDTH + 150] == color(0xd95535));
    CHECK(full[99 * WIDTH + 235] == color(0x9ba98a));
    frame.anchors_only = true;
    uint16_t *anchors = render(echoy_art_draw, &frame);
    CHECK(anchors[15 * WIDTH + 150] == full[15 * WIDTH + 150]);
    CHECK(anchors[99 * WIDTH + 235] == color(0xf1efe8));
    frame.active = true;
    frame.progress = .25;
    uint16_t *active = render(echoy_art_draw, &frame);
    CHECK(active[99 * WIDTH + 229] == color(0xd95535));
    CHECK(anchors[99 * WIDTH + 229] != active[99 * WIDTH + 229]);
    free(active);
    free(anchors);
    free(full);
}

static void pitched_art(echoy_art_frame_t frame)
{
    echoy_plan_t changed = *frame.plan;
    frame.role = ECHOY_VOICE;
    uint16_t *voice = render(echoy_art_draw, &frame);
    CHECK(voice[153 * WIDTH + 10] == color(0x496da1));
    changed.events[0].note = 65;
    frame.plan = &changed;
    uint16_t *higher = render(echoy_art_draw, &frame);
    CHECK(higher[153 * WIDTH + 10] != voice[153 * WIDTH + 10]);
    CHECK(memcmp(higher + 40 * WIDTH, voice + 40 * WIDTH, 80 * WIDTH * 2) != 0);
    frame.role = ECHOY_DUST;
    uint16_t *dust = render(echoy_art_draw, &frame);
    CHECK(dust[173 * WIDTH + 30] != color(0xf1efe8));
    changed.events[0].duration = .05;
    uint16_t *shorter = render(echoy_art_draw, &frame);
    CHECK(shorter[173 * WIDTH + 30] == color(0xf1efe8));
    frame.role = ECHOY_HARMONY;
    uint16_t *harmony = render(echoy_art_draw, &frame);
    CHECK(harmony[187 * WIDTH + 12] != color(0xf1efe8));
    /* Canvas defaults to miter joins: the closed ribbons have pointed tips. */
    CHECK(harmony[33 * WIDTH + 12] != color(0xf1efe8));
    changed.events[0].note_count = 1;
    changed.events[0].harmony[0] = 59;
    uint16_t *single = render(echoy_art_draw, &frame);
    CHECK(single[187 * WIDTH + 12] == color(0xf1efe8));
    CHECK(memcmp(single, harmony, 75 * WIDTH * 2) != 0);
    free(single);
    free(harmony);
    free(shorter);
    free(dust);
    free(higher);
    free(voice);
}

static void feedback(echoy_art_frame_t frame)
{
    frame.role = ECHOY_VOICE;
    frame.height = 15;
    uint16_t *idle = render(echoy_feedback_draw, &frame);
    CHECK(idle[7 * WIDTH + 13] == color(0x414b38));
    /* The support at cell 4 is blue at 45%, rather than the solid anchor ink. */
    CHECK(idle[7 * WIDTH + 86] != color(0x414b38));
    CHECK(idle[7 * WIDTH + 86] != color(0xcdd3c1));
    CHECK(idle[7 * WIDTH + 86] != color(0xf1efe8));
    frame.active = true;
    frame.progress = .25;
    uint16_t *active = render(echoy_feedback_draw, &frame);
    CHECK(active[7 * WIDTH + 86] == color(0x496da1));
    /* CSS space-between must recalculate positions when one cell grows. */
    CHECK(memcmp(idle + 7 * WIDTH + 47, active + 7 * WIDTH + 47, 8 * 2) != 0);
    frame.progress = 1;
    uint16_t *end = render(echoy_feedback_draw, &frame);
    CHECK(end[7 * WIDTH + 286] == color(0x496da1));
    free(end);
    free(active);
    free(idle);
}

static void harmony_mark_separation(void)
{
    /* Current seed5/phrase2 has a .23 s anchor at step2 and a .20 s
     * accompaniment at step3: their real sounding durations overlap. */
    echoy_plan_t plan = {.count = 2};
    plan.events[0] = (echoy_note_t){.step = 2, .duration = .23,
        .layer = ECHOY_ANCHOR, .note_count = 3};
    plan.events[1] = (echoy_note_t){.step = 3, .duration = .20,
        .layer = ECHOY_SUPPORT, .note_count = 3};
    echoy_plan_t original = plan;
    echoy_art_frame_t frame = {.plan = &plan, .role = ECHOY_HARMONY,
        .width = WIDTH, .height = HEIGHT};
    uint16_t *spaced = render(echoy_art_draw, &frame);
    for (unsigned row = 0; row < 3; ++row) {
        unsigned y = 175 + row * 6;
        CHECK(spaced[y * WIDTH + 50] != color(0xf1efe8));
        CHECK(spaced[y * WIDTH + 60] == color(0xf1efe8));
        CHECK(spaced[y * WIDTH + 65] != color(0xf1efe8));
    }
    CHECK(memcmp(&plan, &original, sizeof(plan)) == 0);
    /* Bounds must not depend on event array order. */
    echoy_note_t swap = plan.events[0];
    plan.events[0] = plan.events[1];
    plan.events[1] = swap;
    uint16_t *reversed = render(echoy_art_draw, &frame);
    CHECK(memcmp(spaced + 170 * WIDTH, reversed + 170 * WIDTH, 24 * WIDTH * 2) == 0);
    free(reversed);
    free(spaced);
    /* Hidden accompaniment must not shorten the captured anchor's mark. */
    frame.anchors_only = true;
    uint16_t *capture = render(echoy_art_draw, &frame);
    CHECK(capture[175 * WIDTH + 60] != color(0xf1efe8));
    free(capture);

    plan = (echoy_plan_t){.count = 32};
    for (unsigned i = 0; i < 32; ++i)
        plan.events[i] = (echoy_note_t){.step = i * .5, .duration = .23,
            .layer = ECHOY_ANCHOR, .note_count = 1};
    frame.anchors_only = false;
    uint16_t *dense = render(echoy_art_draw, &frame);
    unsigned runs = 0;
    bool was_ink = false;
    for (unsigned x = 0; x < WIDTH; ++x) {
        bool ink = dense[175 * WIDTH + x] != color(0xf1efe8);
        if (ink && !was_ink) ++runs;
        was_ink = ink;
    }
    CHECK(runs == 32);
    free(dense);
}

static void geometry(echoy_art_frame_t frame)
{
    frame.role = ECHOY_PULSE;
    frame.width = 206;
    frame.height = 101;
    frame.background = 0xe5e7de;
    uint16_t *gallery = render(echoy_art_draw, &frame);
    CHECK(gallery[49 * 206 + 103] == color(0xd95535));
    CHECK(gallery[0] == color(0xe5e7de));
    free(gallery);
    frame.width = WIDTH;
    frame.height = HEIGHT;
    frame.background = 0;
    frame.empty = true;
    uint16_t *empty = render(echoy_art_draw, &frame);
    frame.empty = false;
    uint16_t *normal = render(echoy_art_draw, &frame);
    bool shifted = true;
    for (unsigned y = 1; y < HEIGHT; ++y)
        for (unsigned x = 1; x < WIDTH; ++x)
            shifted &= empty[y * WIDTH + x] == normal[(y - 1) * WIDTH + x - 1];
    CHECK(shifted);
    free(empty);
    free(normal);
}

static void playback_selection(echoy_art_frame_t frame)
{
    echoy_plan_t changed = *frame.plan;
    frame.role = ECHOY_VOICE;
    frame.active = true;
    frame.progress = .25;
    uint16_t *playing = render(echoy_art_draw, &frame);
    changed.events[0].note = 67;
    frame.plan = &changed;
    frame.active = false;
    uint16_t *same_pitch = render(echoy_art_draw, &frame);
    CHECK(memcmp(playing, same_pitch, 130 * WIDTH * 2) == 0);
    free(same_pitch);
    free(playing);

    frame.role = ECHOY_HARMONY;
    frame.active = true;
    frame.progress = .125; /* First note ends exactly here: no chord is held. */
    uint16_t *released = render(echoy_art_draw, &frame);
    frame.plan = NULL;
    uint16_t *rest = render(echoy_art_draw, &frame);
    CHECK(memcmp(released, rest, 130 * WIDTH * 2) == 0);
    free(rest);
    free(released);

    frame.plan = &changed;
    frame.progress = .5625;
    frame.energy = .6f;
    uint16_t *held = render(echoy_art_draw, &frame);
    changed.events[0].harmony[0] = 59; /* Expired earlier note cannot change art. */
    uint16_t *earlier = render(echoy_art_draw, &frame);
    CHECK(memcmp(held, earlier, WIDTH * HEIGHT * 2) == 0);
    changed.events[2].harmony[0] = 60;
    uint16_t *current = render(echoy_art_draw, &frame);
    CHECK(memcmp(held, current, 130 * WIDTH * 2) != 0);
    free(current);
    free(earlier);
    free(held);
}

static void capture_layers(echoy_art_frame_t frame)
{
    echoy_plan_t foreground = { .count = 2 };
    foreground.events[0] = frame.plan->events[0];
    foreground.events[1] = frame.plan->events[2];
    for (unsigned role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) {
        frame.role = role;
        frame.anchors_only = true;
        uint16_t *capture = render(echoy_art_draw, &frame);
        echoy_art_frame_t explicit = frame;
        explicit.plan = &foreground;
        explicit.anchors_only = false;
        uint16_t *expected = render(echoy_art_draw, &explicit);
        CHECK(memcmp(capture, expected, WIDTH * HEIGHT * 2) == 0);
        frame.anchors_only = false;
        uint16_t *accompaniment = render(echoy_art_draw, &frame);
        CHECK(memcmp(capture, accompaniment, WIDTH * HEIGHT * 2) != 0);
        free(accompaniment);
        free(expected);
        free(capture);
    }
}

int main(void)
{
    echoy_plan_t plan = fixture();
    echoy_art_frame_t frame = { .plan = &plan, .width = WIDTH, .height = HEIGHT };
    pulse_layers(frame);
    pitched_art(frame);
    harmony_mark_separation();
    feedback(frame);
    geometry(frame);
    playback_selection(frame);
    capture_layers(frame);
    const unsigned sizes[][2] = { {300, 204}, {206, 101}, {73, 48} };
    for (unsigned role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) {
        frame.role = role;
        frame.active = true;
        frame.progress = .53;
        frame.energy = .7f;
        for (unsigned size = 0; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
            frame.width = sizes[size][0];
            frame.height = sizes[size][1];
            frame.anchors_only = size == 2;
            tiled_matches(echoy_art_draw, frame, 37, 19);
        }
        frame.width = WIDTH;
        frame.height = 15;
        frame.anchors_only = false;
        tiled_matches(echoy_feedback_draw, frame, 23, 4);
        frame.plan = NULL;
        frame.width = 73;
        frame.height = 48;
        frame.empty = true;
        frame.active = false;
        frame.energy = 0;
        tiled_matches(echoy_art_draw, frame, 31, 13);
        frame.plan = &plan;
        frame.empty = false;
    }
    printf("art checks: %u; failures: %u\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
