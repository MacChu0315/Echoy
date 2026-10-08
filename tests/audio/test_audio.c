#include "echoy_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static double rms(const float *samples, size_t count)
{
    double sum = 0;
    for (size_t i = 0; i < count; ++i) sum += samples[i] * (double)samples[i];
    return sqrt(sum / count);
}

int main(void)
{
    echoy_audio_buffer_t *cue = NULL;
    CHECK(echoy_audio_render_cue(ECHOY_AUDIO_GUIDE, &cue) == ECHOY_AUDIO_OK);
    CHECK(cue && cue->frames == 720);
    CHECK(cue->samples[0] == 0);
    CHECK(rms(cue->samples, cue->frames) > .005);
    /* Count sine zero crossings over 20ms: the guide is 1050 Hz. */
    unsigned crossings = 0;
    for (unsigned i = 1; i < 480; ++i)
        crossings += cue->samples[i - 1] <= 0 && cue->samples[i] > 0;
    CHECK(crossings == 21);
    echoy_audio_free_buffer(cue);
    cue = NULL;
    CHECK(echoy_audio_render_cue(ECHOY_AUDIO_CAPTURE_START, &cue) == ECHOY_AUDIO_OK);
    crossings = 0;
    for (unsigned i = 1; i < 480; ++i)
        crossings += cue->samples[i - 1] <= 0 && cue->samples[i] > 0;
    CHECK(crossings == 30);
    echoy_audio_free_buffer(cue);
    CHECK(echoy_audio_render_cue((echoy_audio_cue_t)99, &cue) == ECHOY_AUDIO_INVALID && !cue);
    CHECK(echoy_audio_render_cue(ECHOY_AUDIO_TAP, NULL) == ECHOY_AUDIO_INVALID);

    echoy_plan_t plan;
    CHECK(echoy_compose_mask((1u << 4) | (1u << 6), ECHOY_PULSE, 1, .5, &plan));
    echoy_audio_loops_t *loops = NULL, *repeat = NULL;
    CHECK(echoy_audio_prepare(&plan, true, &loops) == ECHOY_AUDIO_OK);
    CHECK(loops && loops->frames == 60000);
    CHECK(loops->target_rms > 0 && loops->target_rms <= .045);
    CHECK(memcmp(loops->samples[ECHOY_AUDIO_SOURCE], loops->samples[ECHOY_AUDIO_FOREGROUND], 60000 * sizeof(float)) == 0);
    for (unsigned kind = 0; kind < ECHOY_AUDIO_KIND_COUNT; ++kind) {
        CHECK(fabs(rms(loops->samples[kind], 60000) - loops->target_rms) < 1e-7);
        for (unsigned i = 0; i < 4 * 3750; ++i) CHECK(loops->samples[kind][i] == 0);
        for (unsigned i = 0; i < 60000; ++i)
            CHECK(isfinite(loops->samples[kind][i]) && fabs(loops->samples[kind][i]) <= .650001);
        CHECK(loops->samples[kind][59999] == 0);
    }
    CHECK(echoy_audio_prepare(&plan, true, &repeat) == ECHOY_AUDIO_OK);
    for (unsigned kind = 0; kind < ECHOY_AUDIO_KIND_COUNT; ++kind)
        CHECK(memcmp(loops->samples[kind], repeat->samples[kind], 60000 * sizeof(float)) == 0);
    echoy_audio_free_loops(repeat);
    echoy_audio_free_loops(loops);
    CHECK(echoy_audio_prepare(NULL, false, &loops) == ECHOY_AUDIO_INVALID && !loops);
    CHECK(echoy_audio_prepare(&plan, false, NULL) == ECHOY_AUDIO_INVALID);
    plan.source_count = 0;
    plan.count = 0;
    CHECK(echoy_audio_prepare(&plan, false, &loops) == ECHOY_AUDIO_EMPTY && !loops);
    plan.count = ECHOY_MAX_EVENTS + 1;
    CHECK(echoy_audio_prepare(&plan, false, &loops) == ECHOY_AUDIO_INVALID && !loops);
    echoy_audio_free_loops(NULL);
    echoy_audio_free_buffer(NULL);
    printf("audio behavior checks: %u\n", checks);
    return 0;
}
