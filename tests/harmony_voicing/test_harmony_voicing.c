#include "echoy_audio.h"
#include <assert.h>
#include <complex.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 60000u
#define RATE 24000.0
#define PI 3.1415926535897932384626433832795
static float raw[N], actual[N], reference[N], preceding[N], rotated[N];
static float saved_source[N], saved_foreground[N];
static bool forbid_allocation;

void *__real_malloc(size_t bytes);
void *__real_calloc(size_t count, size_t bytes);
void *__real_realloc(void *memory, size_t bytes);
void __real_free(void *memory);
void *__wrap_malloc(size_t bytes) { assert(!forbid_allocation); return __real_malloc(bytes); }
void *__wrap_calloc(size_t count, size_t bytes) { assert(!forbid_allocation); return __real_calloc(count, bytes); }
void *__wrap_realloc(void *memory, size_t bytes) { assert(!forbid_allocation); return __real_realloc(memory, bytes); }
void __wrap_free(void *memory) { assert(!forbid_allocation); __real_free(memory); }

static bool cut(float *samples, size_t frames)
{
    forbid_allocation = true;
    bool ok = echoy_audio_cut_harmony_bass(samples, frames);
    forbid_allocation = false;
    return ok;
}

/* Independent coefficients and long warm-up reproduce the selected offline
   candidate, without calling the production filter or its state code. */
static void long_periodic_reference(const float *input)
{
    const double k = tan(PI * 500 / RATE);
    const double scale = 1 / (1 + sqrt(2) * k + k * k);
    const float b0 = (float)scale, b1 = (float)(-2 * scale), b2 = b0;
    const float a1 = (float)(2 * (k * k - 1) * scale);
    const float a2 = (float)((1 - sqrt(2) * k + k * k) * scale);
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (unsigned pass = 0; pass < 22; ++pass) {
        for (unsigned i = 0; i < N; ++i) {
            float x = input[i];
            float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            assert(isfinite(y));
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            if (pass == 20) preceding[i] = y;
            if (pass == 21) reference[i] = y;
        }
    }
}

static double energy(const float *samples)
{
    double sum = 0;
    for (unsigned i = 0; i < N; ++i) sum += samples[i] * (double)samples[i];
    return sum;
}

static void agrees_with_long_reference(bool exact)
{
    memcpy(actual, raw, sizeof(raw));
    long_periodic_reference(raw);
    assert(cut(actual, N));
    if (exact) {
        assert(memcmp(preceding, reference, sizeof(reference)) == 0);
        assert(memcmp(actual, reference, sizeof(actual)) == 0);
    } else {
        /* Finite float rounding can cycle even after 22 warm-ups; in particular
           5000 Hz / .2 peak / .73 phase has a measured 1.79e-7 period difference.
           Test the full waveform, including the wrap, against the fixed cap. */
        for (unsigned i = 0; i < N; ++i) {
            assert(fabsf(preceding[i] - reference[i]) <= 4.7e-7f);
            assert(fabsf(actual[i] - reference[i]) <= 4.7e-7f);
        }
    }
}

/* Catches wrong cutoff, signs, phase, reset at wrap, and gain compensation.
   Expectations come from the analytic transfer rather than the C helper. */
static void independent_response_and_phase(void)
{
    const unsigned frequencies[] = {40, 120, 220, 330, 500, 1000, 5000, 10000};
    const double k = tan(PI * 500 / RATE), scale = 1 / (1 + sqrt(2) * k + k * k);
    for (unsigned f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); ++f) {
        double w = 2 * PI * frequencies[f] / RATE;
        double complex z = cexp(-I * w);
        double complex response = scale * (1 - 2 * z + z * z) /
            (1 + 2 * (k * k - 1) * scale * z + (1 - sqrt(2) * k + k * k) * scale * z * z);
        for (unsigned i = 0; i < N; ++i) raw[i] = (float)(.2 * sin(w * i + .73));
        agrees_with_long_reference(false);
        for (unsigned i = 0; i < N; ++i) {
            double angle = w * i + .73;
            double wanted = .2 * (creal(response) * sin(angle) + cimag(response) * cos(angle));
            assert(fabs(actual[i] - wanted) < .000004);
        }
        double actual_ratio = sqrt(energy(actual) / energy(raw));
        double t = tan(PI * frequencies[f] / RATE) / k;
        double wanted_ratio = t * t / sqrt(1 + t * t * t * t);
        assert(fabs(actual_ratio - wanted_ratio) < .000015);
        if (frequencies[f] == 120) assert(actual_ratio < .058);
        if (frequencies[f] == 500) assert(actual_ratio > .70709 && actual_ratio < .70712);
    }
}

/* Catches DC leakage, invalid zero handling and warming mutated samples. */
static void zero_and_constant_have_no_startup_transient(void)
{
    const float values[] = {0, .1f, 1, -1, 1e-35f};
    for (unsigned c = 0; c < sizeof(values) / sizeof(values[0]); ++c) {
        for (unsigned i = 0; i < N; ++i) raw[i] = values[c];
        agrees_with_long_reference(true);
        for (unsigned i = 0; i < N; ++i) assert(fabsf(actual[i]) < 1e-30f);
    }
}

/* Rotating a periodic input must only rotate its filtered result. Boundary
   impulses expose a zero initial state even when whole-loop RMS looks right. */
static void synthetic_loop_wrap_is_periodic(void)
{
    uint32_t random = 12345;
    for (unsigned i = 0; i < N; ++i) {
        random = random * 1664525u + 1013904223u;
        raw[i] = (float)((int32_t)(random >> 8) - 8388608) / 33554432.0f;
    }
    agrees_with_long_reference(true);
    for (unsigned i = 0; i < N; ++i) rotated[i] = raw[(i + 37) % N];
    assert(cut(rotated, N));
    for (unsigned i = 0; i < N; ++i) assert(rotated[i] == actual[(i + 37) % N]);
    memset(raw, 0, sizeof(raw));
    raw[0] = .25f; raw[N - 1] = -.2f; raw[N - 2] = .15f;
    agrees_with_long_reference(true);
    assert(fabsf(actual[0] - .9115866422653198f * raw[0]) > .0001f);
}

/* Errors discovered at the last sample must leave the entire bank intact. */
static void invalid_or_overflowing_input_is_unchanged(void)
{
    for (unsigned i = 0; i < N; ++i) raw[i] = (float)(.2 * sin(2 * PI * i / 60));
    const size_t bad_sizes[] = {0, 1, N - 1, N + 1, SIZE_MAX};
    assert(!cut(NULL, N));
    for (unsigned i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i) {
        memcpy(actual, raw, sizeof(raw));
        assert(!cut(actual, bad_sizes[i]));
        assert(memcmp(actual, raw, sizeof(actual)) == 0);
    }
    const float bad[] = {NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memcpy(actual, raw, sizeof(raw));
        actual[N - 1] = bad[i];
        memcpy(preceding, actual, sizeof(actual));
        assert(!cut(actual, N));
        assert(memcmp(actual, preceding, sizeof(actual)) == 0);
    }
    memcpy(actual, raw, sizeof(raw));
    actual[N - 2] = FLT_MAX;
    actual[N - 1] = -FLT_MAX;
    memcpy(preceding, actual, sizeof(actual));
    assert(!cut(actual, N));
    assert(memcmp(actual, preceding, sizeof(actual)) == 0);

    /* A larger but finite version of the rounded 5000 Hz period exceeds the
       absolute error cap. The fallback must reject it without partial output. */
    for (unsigned i = 0; i < N; ++i)
        raw[i] = 4 * (float)(.2 * sin(2 * PI * 5000 * i / RATE + .73));
    long_periodic_reference(raw);
    float maximum = 0;
    for (unsigned i = 0; i < N; ++i)
        maximum = fmaxf(maximum, fabsf(reference[i] - preceding[i]));
    assert(maximum > 4.7e-7f);
    memcpy(actual, raw, sizeof(raw));
    assert(!cut(actual, N));
    assert(memcmp(actual, raw, sizeof(actual)) == 0);
}

/* All twelve expressions remain preparable. The selected FULL buffer matches
   the long reference without changing source, foreground, normalization,
   plan notes or a fresh unfiltered preparation. */
static void twelve_expressions_preserve_other_buffers(void)
{
    for (unsigned role = 0; role < 4; ++role) {
        for (unsigned phrase = 1; phrase <= 3; ++phrase) {
            echoy_plan_t plan, saved_plan;
            assert(echoy_compose_mask(0x40aa, (echoy_role_t)role, phrase, .5, &plan));
            memcpy(&saved_plan, &plan, sizeof(plan));
            echoy_audio_loops_t *loops = NULL;
            assert(echoy_audio_prepare(&plan, role == ECHOY_PULSE && phrase == 1, &loops) == ECHOY_AUDIO_OK);
            assert(loops->frames == N);
            double target = loops->target_rms;
            memcpy(raw, loops->samples[ECHOY_AUDIO_FULL], sizeof(raw));
            memcpy(saved_source, loops->samples[ECHOY_AUDIO_SOURCE], sizeof(raw));
            memcpy(saved_foreground, loops->samples[ECHOY_AUDIO_FOREGROUND], sizeof(raw));
            long_periodic_reference(raw);
            assert(cut(loops->samples[ECHOY_AUDIO_FULL], loops->frames));
            assert(memcmp(reference, loops->samples[ECHOY_AUDIO_FULL], sizeof(raw)) == 0);
            assert(memcmp(saved_source, loops->samples[ECHOY_AUDIO_SOURCE], sizeof(raw)) == 0);
            assert(memcmp(saved_foreground, loops->samples[ECHOY_AUDIO_FOREGROUND], sizeof(raw)) == 0);
            assert(memcmp(&saved_plan, &plan, sizeof(plan)) == 0);
            assert(loops->target_rms == target);
            if (role == ECHOY_HARMONY) assert(energy(loops->samples[ECHOY_AUDIO_FULL]) < energy(raw));
            echoy_audio_free_loops(loops);
            loops = NULL;
            assert(echoy_audio_prepare(&plan, role == ECHOY_PULSE && phrase == 1, &loops) == ECHOY_AUDIO_OK);
            assert(memcmp(raw, loops->samples[ECHOY_AUDIO_FULL], sizeof(raw)) == 0);
            echoy_audio_free_loops(loops);
        }
    }
}

static void compare_archived_candidate(const char *input, const char *expected)
{
    FILE *file = fopen(input, "rb");
    assert(file && fread(raw, sizeof(float), N, file) == N && fgetc(file) == EOF);
    assert(fclose(file) == 0);
    agrees_with_long_reference(true);
    file = fopen(expected, "rb");
    assert(file && fread(reference, sizeof(float), N, file) == N && fgetc(file) == EOF);
    assert(fclose(file) == 0);
    assert(memcmp(reference, actual, sizeof(actual)) == 0);
    puts("PASS byte-identical to the archived selected 500 Hz candidate");
}

int main(int argc, char **argv)
{
    independent_response_and_phase();
    zero_and_constant_have_no_startup_transient();
    synthetic_loop_wrap_is_periodic();
    invalid_or_overflowing_input_is_unchanged();
    twelve_expressions_preserve_other_buffers();
    if (argc == 3) compare_archived_candidate(argv[1], argv[2]);
    else assert(argc == 1);
    puts("PASS 500 Hz response, phase/wrap, DC/zero, 22-loop reference, unchanged invalid input, no allocation and 12 expressions");
    return 0;
}
