#include "echoy_device_pcm.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float source[24000];
static int16_t whole[24000], chunks[24000];
static void convert(echoy_device_pcm_t *s, const float *in, int16_t *out, size_t n)
{
    echoy_device_pcm_stats_t stats;
    assert(echoy_device_pcm_convert(s, in, out, n, &stats));
    assert(stats.clipped == 0);
    for (size_t i = 0; i < n; i++) {
        assert(abs(out[i]) <= 19661);
    }
}

static void speaker_filter_response(void)
{
    /* Independent steady-state bounds for a 120 Hz, second-order Butterworth
     * high-pass. The quiet input must not engage peak limiting. */
    const unsigned frequencies[] = {40, 120, 1000};
    const double minimum[] = {.105, .702, .998};
    const double maximum[] = {.116, .712, 1.002};
    for (unsigned f = 0; f < 3; ++f) {
        echoy_device_pcm_t state;
        echoy_device_pcm_reset(&state);
        for (unsigned i = 0; i < 24000; ++i) {
            source[i] = .005f * sinf((float)(2 * 3.141592653589793 * frequencies[f] * i / 24000));
        }
        echoy_device_pcm_stats_t stats;
        assert(echoy_device_pcm_convert(&state, source, whole, 24000, &stats));
        assert(stats.clipped == 0 && stats.limited == 0);
        double energy = 0;
        for (unsigned i = 12000; i < 24000; ++i) {
            double x = whole[i] / 32767.0;
            energy += x * x;
        }
        double ratio = sqrt(energy / 12000) / (.005 * ECHOY_DEVICE_PCM_GAIN / sqrt(2));
        assert(ratio >= minimum[f] && ratio <= maximum[f]);
    }
    echoy_device_pcm_t state;
    echoy_device_pcm_reset(&state);
    for (unsigned i = 0; i < 24000; ++i) source[i] = .005f;
    convert(&state, source, whole, 24000);
    for (unsigned i = 12000; i < 24000; ++i) assert(abs(whole[i]) <= 1);
}

int main(void)
{
    speaker_filter_response();
    echoy_device_pcm_t a, b;
    echoy_device_pcm_reset(&a);
    /* Quiet 1 kHz stays at the expected pass-band level, with 24-frame latency.
     * The high-pass intentionally changes phase, so exact old samples differ. */
    for (unsigned i = 0; i < 24000; i++) {
        source[i] = .01f * sinf(i * .2617993878f);
    }
    convert(&a, source, whole, 24000);
    for (unsigned i = 0; i < 24; i++) {
        assert(whole[i] == 0);
    }
    double energy = 0;
    for (unsigned i = 12000; i < 24000; i++) energy += whole[i] * (double)whole[i];
    double rms = sqrt(energy / 12000) / 32767;
    assert(rms > .1271 && rms < .1274);

    /* Abrupt, bipolar overloads must respect the ceiling at every alignment. */
    for (unsigned i = 0; i < 24000; i++) {
        source[i] = i % 257 == 0 ? (i % 2 ? -2 : 2) : .1f * sinf(i * .019f);
    }
    echoy_device_pcm_reset(&a);
    echoy_device_pcm_stats_t stats;
    assert(echoy_device_pcm_convert(&a, source, whole, 24000, &stats));
    assert(stats.limited > 0 && stats.clipped == 0);
    for (unsigned i = 0; i < 24000; i++) {
        assert(abs(whole[i]) <= 19661);
    }
    echoy_device_pcm_reset(&b);
    for (unsigned at = 0; at < 24000;) {
        unsigned n = (at % 239) + 1;
        if (at + n > 24000) {
            n = 24000 - at;
        }
        convert(&b, source + at, chunks + at, n);
        at += n;
    }
    assert(!memcmp(whole, chunks, sizeof(whole)));

    /* A single strong transient must not permanently duck later quiet audio. */
    echoy_device_pcm_reset(&a);
    for (unsigned i = 0; i < 24000; i++) {
        source[i] = .001f * sinf(i * .2617993878f);
    }
    source[0] = 1;
    convert(&a, source, whole, 24000);
    assert(whole[24] >= 19659 && whole[24] <= 19661);
    energy = 0;
    for (unsigned i = 12000; i < 24000; i++) energy += whole[i] * (double)whole[i];
    rms = sqrt(energy / 12000) / 32767;
    assert(rms > .0126 && rms < .0129);
    /* Reset discards both pending sound and gain reduction after cancellation. */
    source[0] = .1f;
    convert(&a, source, whole, 1);
    echoy_device_pcm_reset(&a);
    memset(source, 0, sizeof(source));
    convert(&a, source, whole, 240);
    for (unsigned i = 0; i < 240; i++) {
        assert(whole[i] == 0);
    }
    source[0] = .1f;
    convert(&a, source, whole, 240);
    assert(whole[24] >= 19659 && whole[24] <= 19661);

    /* Reject the entire invalid block without mutating output or the delay. */
    float invalid[] = {NAN, INFINITY, -INFINITY, FLT_MAX, -FLT_MAX,
                       FLT_MAX / (ECHOY_DEVICE_PCM_GAIN * 2)};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        b = a;
        float input[] = {.1f, invalid[i]};
        int16_t output[] = {123, 456};
        assert(!echoy_device_pcm_convert(&a, input, output, 2, &stats));
        assert(output[0] == 123 && output[1] == 456 && !memcmp(&a, &b, sizeof(a)));
    }
    assert(!echoy_device_pcm_convert(NULL, source, whole, 1, &stats));
    assert(!echoy_device_pcm_convert(&a, NULL, whole, 1, &stats));
    assert(!echoy_device_pcm_convert(&a, source, NULL, 1, &stats));
    assert(!echoy_device_pcm_convert(&a, source, whole, 1, NULL));
    puts("PASS speaker filter, quiet-signal gain, peak ceiling, block continuity, release, reset and invalid PCM "
         "rejection");
}
