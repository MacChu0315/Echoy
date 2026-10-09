#include "echoy_audio_probe.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int16_t *pcm = calloc(ECHOY_PROBE_FRAMES + 720, sizeof(*pcm));
    assert(pcm);
    echoy_audio_probe_render(0, pcm, ECHOY_PROBE_FRAMES + 720);
    assert(pcm[0] == 0 && pcm[ECHOY_PROBE_FRAMES-1] == 0);
    int peak = 0;
    int64_t sum = 0;
    unsigned crossings = 0;
    for (unsigned i = 0; i < ECHOY_PROBE_FRAMES; ++i) {
        int sample = pcm[i];
        if (abs(sample) > peak) peak = abs(sample);
        sum += sample;
        if (i && pcm[i-1] < 0 && sample >= 0) ++crossings;
        if (i >= 480 && i + 24 < ECHOY_PROBE_FRAMES - 480) assert(pcm[i] == pcm[i+24]);
    }
    assert(peak == 8192 && crossings >= 998 && crossings <= 1000);
    assert(llabs(sum) < 1000);
    for (unsigned i = ECHOY_PROBE_FRAMES; i < ECHOY_PROBE_FRAMES + 720; ++i) assert(pcm[i] == 0);
    int16_t block[137];
    for (unsigned at = 0; at < ECHOY_PROBE_FRAMES; at += 137) {
        echoy_audio_probe_render(at, block, 137);
        for (unsigned k = 0; k < 137; ++k) assert(block[k] == pcm[at+k]);
    }
    free(pcm);
    puts("PASS fixed 1 kHz level, bounded duration, ramps, silent tail and chunk continuity");
}
