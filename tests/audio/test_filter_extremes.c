#include "echoy_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "filter_extremes_reference.h"

int echoy_audio_baseline_probe(unsigned kind, unsigned note, float *output);
static float samples[ECHOY_AUDIO_LOOP_FRAMES];

int main(void)
{
    for (unsigned c = 0; c < sizeof(extremes) / sizeof(extremes[0]); ++c) {
        assert(echoy_audio_baseline_probe(extremes[c].kind, extremes[c].note, samples) == 0);
        double energy = 0, error = 0, peak = 0;
        for (unsigned i = 0; i < 128; ++i) {
            double expected = extremes[c].samples[i], actual = samples[i * 457];
            energy += expected * expected;
            error += (actual - expected) * (actual - expected);
        }
        for (unsigned i = 0; i < ECHOY_AUDIO_LOOP_FRAMES; ++i) {
            assert(isfinite(samples[i]));
            if (fabs(samples[i]) > peak) peak = fabs(samples[i]);
        }
        printf("extreme kind=%u note=%u sampled_relative_error=%.9g peak_ratio=%.9g\n",
               extremes[c].kind, extremes[c].note, sqrt(error / energy), peak / extremes[c].peak - 1);
        fflush(stdout);
        assert(sqrt(error / energy) <= .03);
        assert(fabs(peak / extremes[c].peak - 1) <= .02);
    }
    return 0;
}
