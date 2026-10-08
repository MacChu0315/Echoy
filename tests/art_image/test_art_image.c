#include "echoy_art.h"
#include "echoy_art_image.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, records;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

/* Inject allocation failures only while the encoder runs; renderer and decoder
 * remain real. Track live buffers to verify every early return releases them. */
static unsigned fail_at, allocation_calls, live_allocations;
static void *tracked[8];
void *__real_malloc(size_t size);
void __real_free(void *pointer);

void *__wrap_malloc(size_t size)
{
    if (fail_at && ++allocation_calls == fail_at) return NULL;
    void *pointer = __real_malloc(size);
    if (pointer && fail_at) {
        bool inserted = false;
        for (unsigned i = 0; i < sizeof(tracked) / sizeof(tracked[0]); ++i) {
            if (!tracked[i]) {
                tracked[i] = pointer;
                ++live_allocations;
                inserted = true;
                break;
            }
        }
        assert(inserted);
    }
    return pointer;
}

void __wrap_free(void *pointer)
{
    if (pointer) {
        for (unsigned i = 0; i < sizeof(tracked) / sizeof(tracked[0]); ++i) {
            if (tracked[i] == pointer) {
                tracked[i] = NULL;
                --live_allocations;
                break;
            }
        }
    }
    __real_free(pointer);
}

static void write32(FILE *out, unsigned value)
{
    uint8_t bytes[] = {value, value >> 8, value >> 16, value >> 24};
    assert(fwrite(bytes, 1, sizeof(bytes), out) == sizeof(bytes));
}

/* The bundle stores independently rendered RGB565 pixels beside encoded QOI.
 * Pillow consumes the QOI bytes later, without linking any encoder helpers. */
static void roundtrip(FILE *bundle, echoy_art_frame_t frame)
{
    uint8_t *qoi = NULL;
    size_t size = 0;
    bool ok = echoy_art_encode_qoi(&frame, &qoi, &size);
    CHECK(ok);
    if (!ok) return;
    CHECK(qoi != NULL);
    CHECK(size >= 23 && size <= (size_t)frame.width * frame.height * 4 + 22);
    const size_t count = (size_t)frame.width * frame.height;
    uint16_t *pixels = malloc(count * sizeof(*pixels));
    assert(pixels);
    esp_gsp_canvas_surface_t surface = { .pixels = pixels, .stride_bytes = frame.width * 2,
        .width = frame.width, .height = frame.height, .pixel_format = ESP_GSP_CANVAS_PIXEL_RGB565 };
    echoy_art_draw(&surface, &frame);
    write32(bundle, frame.width);
    write32(bundle, frame.height);
    write32(bundle, (unsigned)size);
    assert(fwrite(qoi, 1, size, bundle) == size);
    for (size_t i = 0; i < count; ++i) {
        uint8_t bytes[] = {pixels[i], pixels[i] >> 8};
        assert(fwrite(bytes, 1, 2, bundle) == 2);
    }
    ++records;
    free(pixels);
    free(qoi);
}

static void invalid_input(void)
{
    uint8_t sentinel = 0;
    uint8_t *out = &sentinel;
    size_t size = 9;
    CHECK(!echoy_art_encode_qoi(NULL, &out, &size));
    CHECK(out == NULL && size == 0);
    echoy_art_frame_t frame = {.width = 73, .height = 48};
    size = 9;
    CHECK(!echoy_art_encode_qoi(&frame, NULL, &size));
    CHECK(size == 0);
    out = &sentinel;
    CHECK(!echoy_art_encode_qoi(&frame, &out, NULL));
    CHECK(out == NULL);
    CHECK(!echoy_art_encode_qoi(&frame, NULL, NULL));
    const unsigned dimensions[][2] = {{0,48}, {73,0}, {481,48}, {73,481},
                                       {UINT_MAX,UINT_MAX}, {UINT_MAX,1}, {1,UINT_MAX}};
    for (unsigned i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); ++i) {
        frame.width = dimensions[i][0];
        frame.height = dimensions[i][1];
        out = &sentinel;
        size = 9;
        CHECK(!echoy_art_encode_qoi(&frame, &out, &size));
        CHECK(out == NULL && size == 0);
    }
}

static void allocation_failures(void)
{
    echoy_art_frame_t frame = {.width = 73, .height = 48};
    bool reached_success = false;
    for (unsigned attempt = 1; attempt <= 8 && !reached_success; ++attempt) {
        uint8_t sentinel = 0, *out = &sentinel;
        size_t size = 99;
        allocation_calls = 0;
        fail_at = attempt;
        bool ok = echoy_art_encode_qoi(&frame, &out, &size);
        if (ok) {
            CHECK(allocation_calls < fail_at);
            CHECK(out && size > 0);
            free(out);
            reached_success = true;
        } else {
            CHECK(out == NULL && size == 0);
            CHECK(allocation_calls >= fail_at);
        }
        CHECK(live_allocations == 0);
        fail_at = 0;
    }
    CHECK(reached_success);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *bundle = fopen(argv[1], "wb");
    assert(bundle);
    invalid_input();
    allocation_failures();
    echoy_plan_t plan = {.count = 3};
    plan.events[0] = (echoy_note_t){.step = 0, .layer = ECHOY_ANCHOR, .note = 62,
        .duration = .3125, .notes = {50,57,65}, .note_count = 3,
        .harmony = {50,57,65}, .harmony_count = 3};
    plan.events[1] = (echoy_note_t){.step = 3.5, .layer = ECHOY_SUPPORT, .note = 69,
        .duration = .15, .notes = {60}, .note_count = 1,
        .harmony = {53,60,69}, .harmony_count = 3};
    plan.events[2] = (echoy_note_t){.step = 8, .layer = ECHOY_ANCHOR, .note = 72,
        .duration = .625, .notes = {55,62,70}, .note_count = 3,
        .harmony = {55,62,70}, .harmony_count = 3};
    const unsigned dimensions[][2] = {{300,204}, {206,101}, {73,48}};
    echoy_art_frame_t frame = {.plan = &plan};
    for (unsigned role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) {
        for (unsigned dimension = 0; dimension < 3; ++dimension) {
            frame.role = role;
            frame.width = dimensions[dimension][0];
            frame.height = dimensions[dimension][1];
            frame.background = dimension == 1 ? 0xe5e7de : dimension == 2 ? 0xecefe4 : 0;
            frame.active = false;
            frame.progress = 0;
            frame.energy = 0;
            roundtrip(bundle, frame);
            frame.active = true;
            frame.progress = .52;
            frame.energy = .7f;
            roundtrip(bundle, frame);
        }
    }
    /* Exercise every RGB input level independently. Pixel (0,0) is clear,
     * so decoded fixtures must cover all 32/64/32 quantized channel levels. */
    frame = (echoy_art_frame_t){.width = 8, .height = 8};
    for (unsigned channel = 0; channel < 3; ++channel) {
        for (unsigned value = 0; value < 256; ++value) {
            unsigned shift = channel * 8;
            frame.background = (0x080808u & ~(255u << shift)) | (value << shift);
            roundtrip(bundle, frame);
        }
    }
    /* With the empty border offset, artwork lies below this one-pixel row.
     * Constant black tests the initial previous pixel; red forces RGB + RUN. */
    const unsigned runs[] = {1, 61, 62, 63, 64, 124, 125, 480};
    frame = (echoy_art_frame_t){.height = 1, .empty = true};
    for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
        frame.width = runs[i];
        frame.background = 1; /* Nonzero semantic color quantizes to black. */
        roundtrip(bundle, frame);
        frame.background = 0xff0000;
        roundtrip(bundle, frame);
    }
    frame = (echoy_art_frame_t){.width = 480, .height = 480, .role = ECHOY_HARMONY};
    roundtrip(bundle, frame);
    assert(fclose(bundle) == 0);
    printf("art image checks: %u; failures: %u; decode fixtures: %u\n", checks, failures, records);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
