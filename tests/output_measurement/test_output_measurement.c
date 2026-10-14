#include "echoy_output_measurement.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES 252000u
#define PI 3.14159265358979323846
static int16_t captured[FRAMES + 37], chunks[FRAMES + 37];
static echoy_output_measurement_result_t result;
static const unsigned frequencies[] = {220, 330, 440, 660, 1000};

static void close_to(double got, double wanted, double tolerance)
{
    if (fabs(got - wanted) > tolerance) {
        fprintf(stderr, "got %.10f, expected %.10f +/- %.10f\n", got, wanted, tolerance);
        abort();
    }
}

/* Catches wrong duration, frequency/amplitude ordering, fade or quiet spacing,
   arbitrary-block discontinuities, missing end padding and offset overflow. */
static void sequence_is_bounded_and_block_independent(void)
{
    memset(captured, 0x55, sizeof(captured));
    echoy_output_measurement_render(0, captured, FRAMES + 37);
    assert(ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES == FRAMES);
    assert(sizeof(result) < 10240);
    for (size_t i = 0; i < 48000; ++i) assert(captured[i] == 0);
    for (unsigned tone = 0; tone < 15; ++tone) {
        const size_t start = 48000 + tone * 13200;
        const double peak = .12 * (1 + tone % 3);
        double cosine = 0, sine = 0;
        for (unsigned i = 0; i < 8400; ++i) {
            double envelope = 1;
            if (i < 480) envelope = i / 480.0;
            if (i >= 8400 - 480) envelope = (8399 - i) / 480.0;
            double wanted = 32767 * peak * envelope *
                sin(2 * PI * frequencies[tone / 3] * i / 24000.0);
            close_to(captured[start + i], wanted, 1.5);
            assert(abs(captured[start + i]) <= 11796);
            if (i >= 1800 && i < 6600) {
                sine += captured[start + i] * sin(2 * PI * frequencies[tone / 3] * i / 24000.0);
                cosine += captured[start + i] * cos(2 * PI * frequencies[tone / 3] * i / 24000.0);
            }
        }
        close_to(2 * hypot(sine, cosine) / 4800 / 32767, peak, .00006);
        for (unsigned i = 8400; i < 13200; ++i) assert(captured[start + i] == 0);
    }
    for (size_t i = 246000; i < FRAMES + 37; ++i) assert(captured[i] == 0);
    size_t offset = 0;
    while (offset < FRAMES + 37) {
        size_t count = 1 + (offset * 37 + 17) % 1301;
        if (count > FRAMES + 37 - offset) count = FRAMES + 37 - offset;
        echoy_output_measurement_render(offset, chunks + offset, count);
        offset += count;
    }
    assert(memcmp(captured, chunks, sizeof(captured)) == 0);
    int16_t beyond[4] = {1, 2, 3, 4};
    echoy_output_measurement_render(SIZE_MAX - 1, beyond, 4);
    for (unsigned i = 0; i < 4; ++i) assert(beyond[i] == 0);
    echoy_output_measurement_render(0, NULL, 0);
}

/* Independent libm source. A coherent 1375 Hz background is orthogonal to all
   measured harmonics over the 200 ms window; it is not analyzer-generated. */
static void make_capture(double amplitude, double second_ratio, double phase,
                         double dc, double noise, int delay)
{
    for (unsigned i = 0; i < FRAMES; ++i) {
        double value = dc + noise * sin(2 * PI * 1375 * i / 24000.0);
        int source = (int)i - delay - 48000;
        if (source >= 0 && source < 198000 && source % 13200 < 8400) {
            int tone = source / 13200;
            int local = source % 13200;
            double angle = 2 * PI * frequencies[tone / 3] * local / 24000.0 + phase;
            double envelope = 1;
            if (local < 480) envelope = local / 480.0;
            if (local >= 7920) envelope = (8399 - local) / 480.0;
            value += amplitude * envelope * (sin(angle) + second_ratio * sin(2 * angle + .31));
        }
        long sample = lround(value * 32768);
        if (sample > INT16_MAX) sample = INT16_MAX;
        if (sample < INT16_MIN) sample = INT16_MIN;
        captured[i] = (int16_t)sample;
    }
}

/* Catches phase-sensitive amplitude, wrong harmonic bin, missing DC handling,
   or mistakenly comparing harmonic RMS against fundamental peak. */
static void measures_known_tones_harmonics_dc_and_delay(void)
{
    const int delays[] = {0, 1200, -1200};
    for (unsigned test = 0; test < 3; ++test) {
        make_capture(.2, .1, .57 + test, .03, .001, delays[test]);
        assert(echoy_output_measurement_analyze(captured, FRAMES, &result, NULL, NULL));
        assert(result.flags & ECHOY_OUTPUT_MEASUREMENT_ANALYSIS_COMPLETE);
        assert(result.tones_analyzed == 15);
        assert(result.noise.flags & ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID);
        assert(result.noise.sample_count == 14400);
        close_to(result.noise.ac_rms, .001 / sqrt(2), .000005);
        close_to(result.noise.dc, .03, .000002);
        for (unsigned tone = 0; tone < 15; ++tone) {
            const echoy_output_measurement_tone_t *t = &result.tones[tone];
            assert(t->frequency_hz == frequencies[tone / 3]);
            close_to(t->pcm_peak, .12 * (tone % 3 + 1), .000001);
            assert(t->sample_count == 4800);
            assert(t->window_start == 49800 + tone * 13200);
            assert(t->flags & ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID);
            assert(!(t->flags & ECHOY_OUTPUT_MEASUREMENT_LOW_SNR));
            close_to(t->harmonic_amplitude[0], .2, .000015);
            close_to(t->harmonic_amplitude[1], .02, .000015);
            close_to(t->harmonic_ratio[0], .1, .0001);
            close_to(t->harmonic_rss_ratio, .1, .0001);
            for (unsigned h = 2; h < 5; ++h) assert(t->harmonic_amplitude[h] < .000015);
            close_to(t->dc, .03, .000002);
            close_to(t->rms, sqrt(.03 * .03 + (.2 * .2 + .02 * .02 + .001 * .001) / 2), .00001);
            close_to(t->fundamental_snr_db, 46.0206, .06);
            assert(t->clipped == 0 && t->near_clip == 0);
            assert(t->excerpt_count == 256);
            assert(memcmp(t->excerpt, captured + 52072 + tone * 13200, sizeof(t->excerpt)) == 0);
        }
    }
}

/* Catches no-signal/low-SNR results being labelled usable and DC being treated
   as a fundamental; the noise floor must not become NaN for digital silence. */
static void rejects_unusable_noise_and_fundamental(void)
{
    memset(captured, 0, sizeof(captured));
    assert(echoy_output_measurement_analyze(captured, FRAMES, &result, NULL, NULL));
    assert(result.noise.flags & ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID);
    for (unsigned i = 0; i < 15; ++i) {
        assert(!(result.tones[i].flags & ECHOY_OUTPUT_MEASUREMENT_FUNDAMENTAL_VALID));
        assert(!(result.tones[i].flags & ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID));
        assert(result.tones[i].flags & ECHOY_OUTPUT_MEASUREMENT_LOW_SNR);
        assert(isfinite(result.tones[i].fundamental_snr_db));
        assert(result.tones[i].harmonic_rss_ratio == 0);
    }
    make_capture(.001, .1, .4, .2, .002, 0);
    assert(echoy_output_measurement_analyze(captured, FRAMES, &result, NULL, NULL));
    assert(result.tones[0].flags & ECHOY_OUTPUT_MEASUREMENT_LOW_SNR);
    assert(!(result.tones[0].flags & ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID));
    close_to(result.tones[0].fundamental_snr_db, -6.0206, .2);
    close_to(result.noise.dc, .2, .000002);
}

/* A settling transient before 1.2 s or after 1.8 s must not contaminate the
   late quiet noise window. This does not establish why the physical first
   run had a different first-tone level. */
static void noise_window_excludes_startup_and_stays_before_tones(void)
{
    make_capture(.2, 0, 0, .03, .001, 0);
    for (unsigned i = 0; i < 28800; ++i) captured[i] = 7000;
    for (unsigned i = 43200; i < 48000; ++i) captured[i] = -7000;
    assert(echoy_output_measurement_analyze(captured, FRAMES, &result, NULL, NULL));
    assert(result.noise.sample_count == 14400);
    close_to(result.noise.dc, .03, .000002);
    close_to(result.noise.ac_rms, .001 / sqrt(2), .000005);
    assert(result.tones[0].flags & ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID);
}

/* Catches rail saturation both inside and outside selected analysis windows,
   and treating ADC headroom failure as an output distortion measurement. */
static void clipping_invalidates_ratios_without_hiding_values(void)
{
    make_capture(.2, .1, .1, 0, 0, 0);
    captured[49800] = INT16_MAX;
    captured[49801] = INT16_MIN;
    captured[49802] = 32500;
    assert(echoy_output_measurement_analyze(captured, FRAMES, &result, NULL, NULL));
    assert(result.capture_clipped == 2 && result.capture_near_clip == 3);
    assert(result.capture_adc_peak == 1);
    assert(result.flags & ECHOY_OUTPUT_MEASUREMENT_ADC_CLIPPED);
    assert(result.tones[0].clipped == 2 && result.tones[0].near_clip == 3);
    assert(result.tones[0].flags & ECHOY_OUTPUT_MEASUREMENT_ADC_CLIPPED);
    assert(result.tones[0].harmonic_amplitude[0] > .19);
    for (unsigned i = 0; i < 15; ++i) assert(!(result.tones[i].flags & ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID));

    make_capture(.2, 0, 0, 0, 0, 0);
    captured[100] = INT16_MAX; /* Outside noise/tone windows. */
    assert(echoy_output_measurement_analyze(captured, FRAMES, &result, NULL, NULL));
    assert(result.capture_clipped == 1);
    assert(!(result.tones[0].flags & ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID));

    make_capture(.2, 0, 0, 0, 0, 0);
    captured[28800] = 32500; /* Inside the noise window. */
    assert(echoy_output_measurement_analyze(captured, FRAMES, &result, NULL, NULL));
    assert(!(result.noise.flags & ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID));
    assert(result.noise.flags & ECHOY_OUTPUT_MEASUREMENT_ADC_NEAR_CLIP);
    assert(!(result.tones[0].flags & ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID));
}

static bool cancel_after_calls(void *context)
{
    unsigned *calls = context;
    return (*calls)++ == 4;
}

/* Catches out-of-bounds analysis, stale result reuse after failures, partial
   windows being called complete, and cancellation being ignored. */
static void incomplete_and_cancelled_capture_remain_explicit(void)
{
    make_capture(.2, 0, 0, 0, 0, 0);
    assert(!echoy_output_measurement_analyze(captured, 49801, &result, NULL, NULL));
    assert(result.flags & ECHOY_OUTPUT_MEASUREMENT_INSUFFICIENT_SAMPLES);
    assert(!(result.flags & ECHOY_OUTPUT_MEASUREMENT_ANALYSIS_COMPLETE));
    assert(result.tones[0].flags & ECHOY_OUTPUT_MEASUREMENT_INSUFFICIENT_SAMPLES);
    assert(!(result.tones[0].flags & ECHOY_OUTPUT_MEASUREMENT_WINDOW_VALID));
    assert(result.tones[0].excerpt_count == 0);
    assert(result.frames_captured == 49801);
    assert(!echoy_output_measurement_analyze(captured, 0, &result, NULL, NULL));
    assert(result.tones_analyzed == 0);
    assert(!(result.noise.flags & ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID));
    assert(!echoy_output_measurement_analyze(NULL, FRAMES, &result, NULL, NULL));
    assert(result.tones_analyzed == 0);
    assert(!echoy_output_measurement_analyze(captured, FRAMES, NULL, NULL, NULL));
    unsigned calls = 0;
    assert(!echoy_output_measurement_analyze(captured, FRAMES, &result, cancel_after_calls, &calls));
    assert(result.flags & ECHOY_OUTPUT_MEASUREMENT_ANALYSIS_CANCELLED);
    assert(!(result.flags & ECHOY_OUTPUT_MEASUREMENT_ANALYSIS_COMPLETE));
    assert(result.tones_analyzed < 15);
    assert(!echoy_output_measurement_analyze(captured, FRAMES - 1, &result, NULL, NULL));
    assert(result.flags & ECHOY_OUTPUT_MEASUREMENT_INSUFFICIENT_SAMPLES);
    assert(result.tones_analyzed == 15); /* Tail missing, windows still useful. */
}

int main(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "render") == 0) sequence_is_bounded_and_block_independent();
    if (argc == 1 || strcmp(argv[1], "analysis") == 0) {
        measures_known_tones_harmonics_dc_and_delay();
        rejects_unusable_noise_and_fundamental();
        noise_window_excludes_startup_and_stays_before_tones();
        clipping_invalidates_ratios_without_hiding_values();
        incomplete_and_cancelled_capture_remain_explicit();
    }
    puts("output measurement: passed");
    return 0;
}
