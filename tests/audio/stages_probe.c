/* Offline observers only. probe_stages.py instruments temporary synthesis
 * sources and verifies the bank and PCM against unmodified production C. */
#include "echoy_audio.h"
#include "echoy_device_pcm.h"
#include "echoy_link_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "probe_input.h"

static const char *directory;
static unsigned pre_calls, post_calls;

static void save(const char *name, const void *data, size_t bytes)
{
    char path[4096];
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, bytes, file) == bytes && fclose(file) == 0);
}

void probe_synth_stage(unsigned post, const float *data, size_t frames)
{
    if (post == 2) {
        save("post_fade.f32", data, frames * sizeof(float));
        return;
    }
    unsigned *calls = post ? &post_calls : &pre_calls;
    if (++*calls == 3)
        save(post ? "post_compressor.f32" : "pre_compressor.f32", data, frames * sizeof(float));
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    directory = argv[1];
    double volume = strtod(argv[2], NULL);
    assert(isfinite(volume) && volume >= 0 && volume <= 1);
    echoy_plan_t plan = {0};
    assert(probe_load_plan(&plan));
    uint8_t encoded[ECHOY_LINK_PLAN_MAX_BYTES];
    size_t bytes = echoy_link_plan_encode(&plan, encoded, sizeof(encoded));
    assert(bytes);
    save("plan.echp", encoded, bytes);
    echoy_audio_loops_t *loops = NULL;
    assert(echoy_audio_prepare(&plan, PROBE_ORIGINAL_PULSE1, &loops) == ECHOY_AUDIO_OK);
    save("normalized_full.f32", loops->samples[ECHOY_AUDIO_FULL], loops->frames * sizeof(float));

    /* One complete warm-up loop and two retained loops. Input is the steady
     * single-tile FULL path: calibrated bank * UI volume * .45 bus. This does
     * not claim to reproduce starts, crossfades, cues or multiple audible tiles. */
    size_t frames = loops->frames * 3;
    float *input = calloc(frames, sizeof(float));
    float *drive = calloc(frames, sizeof(float));
    float *gain = calloc(frames, sizeof(float));
    int16_t *pcm = calloc(frames, sizeof(int16_t));
    int16_t *blocked = calloc(frames, sizeof(int16_t));
    assert(input && drive && gain && pcm && blocked);
    for (size_t i = 0; i < frames; ++i) {
        float bank = loops->samples[ECHOY_AUDIO_FULL][i % loops->frames] *
                     echoy_device_role_gains[plan.role];
        input[i] = (float)(bank * volume * .45);
    }
    echoy_device_pcm_t state;
    echoy_device_pcm_reset(&state);
    size_t limited = 0, clipped = 0;
    for (size_t i = 0; i < frames; ++i) {
        echoy_device_pcm_stats_t stats;
        assert(echoy_device_pcm_convert(&state, input + i, pcm + i, 1, &stats));
        if (i >= loops->frames) { limited += stats.limited; clipped += stats.clipped; }
        /* Read the actual converter's state after consuming this sample. */
        drive[i] = state.samples[(state.sample_at + ECHOY_DEVICE_PCM_LOOKAHEAD) %
                                (ECHOY_DEVICE_PCM_LOOKAHEAD + 1)];
        float delayed = state.samples[state.sample_at], applied = 0;
        for (unsigned j = 0; j < ECHOY_DEVICE_PCM_LOOKAHEAD; ++j) applied += state.gains[j];
        applied /= ECHOY_DEVICE_PCM_LOOKAHEAD;
        if (fabsf(delayed) > ECHOY_DEVICE_PCM_CEILING)
            applied = fminf(applied, ECHOY_DEVICE_PCM_CEILING / fabsf(delayed));
        gain[i] = applied;
        assert(pcm[i] == (int16_t)lrintf(fmaxf(-1, fminf(1, delayed * applied)) * 32767));
    }
    echoy_device_pcm_reset(&state);
    for (size_t at = 0; at < frames; at += 240) {
        size_t n = frames - at < 240 ? frames - at : 240;
        echoy_device_pcm_stats_t stats;
        assert(echoy_device_pcm_convert(&state, input + at, blocked + at, n, &stats));
    }
    assert(memcmp(pcm, blocked, frames * sizeof(*pcm)) == 0);
    save("input.f32", input, frames * sizeof(float));
    save("hp_drive.f32", drive, frames * sizeof(float));
    save("limiter_gain.f32", gain, frames * sizeof(float));
    save("pcm.s16", pcm, frames * sizeof(int16_t));
    printf("{\"role\":%d,\"phrase\":%u,\"density\":%.17g,\"events\":%zu,"
           "\"target_rms\":%.17g,\"bank_gain\":%.9g,\"drive\":%.9g,\"ceiling\":%.9g,"
           "\"lookahead_frames\":%u,\"loop_frames\":%zu,\"limited_counter\":%zu,"
           "\"clipped_counter\":%zu,\"block_partition_bit_identical\":true}\n",
           plan.role, plan.phrase, plan.density, plan.count, loops->target_rms,
           (double)echoy_device_role_gains[plan.role], (double)ECHOY_DEVICE_PCM_GAIN,
           (double)ECHOY_DEVICE_PCM_CEILING, ECHOY_DEVICE_PCM_LOOKAHEAD, loops->frames,
           limited, clipped);
    free(input); free(drive); free(gain); free(pcm); free(blocked);
    echoy_audio_free_loops(loops);
    return 0;
}
