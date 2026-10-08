#pragma once
#include "echoy_music.h"

#define ECHOY_AUDIO_SAMPLE_RATE 24000u
#define ECHOY_AUDIO_LOOP_FRAMES 60000u

typedef enum {
    ECHOY_AUDIO_SOURCE, ECHOY_AUDIO_FOREGROUND, ECHOY_AUDIO_FULL, ECHOY_AUDIO_KIND_COUNT
} echoy_audio_kind_t;

typedef enum {
    ECHOY_AUDIO_OK, ECHOY_AUDIO_INVALID, ECHOY_AUDIO_EMPTY, ECHOY_AUDIO_NO_MEMORY
} echoy_audio_result_t;

typedef struct {
    float *samples[ECHOY_AUDIO_KIND_COUNT];
    size_t frames;
    double target_rms;
} echoy_audio_loops_t;

typedef enum {
    ECHOY_AUDIO_TAP, ECHOY_AUDIO_GUIDE, ECHOY_AUDIO_CAPTURE_START
} echoy_audio_cue_t;

typedef struct {
    float *samples;
    size_t frames;
} echoy_audio_buffer_t;

/* Prepare immutable 24 kHz mono loops from the supplied plan. Set
 * original_pulse1 only for a local, unlinked Pulse phrase 1: its archived
 * sparse response participates in normalization, as in the frozen prototype.
 * The output includes the synth gain/limiter, but no tile or final bus gain.
 * Success transfers ownership to the caller; failure sets *out to NULL.
 * Preparation allocates scratch memory; call before realtime consumption. */
echoy_audio_result_t echoy_audio_prepare(const echoy_plan_t *plan, bool original_pulse1,
                                       echoy_audio_loops_t **out);
void echoy_audio_free_loops(echoy_audio_loops_t *loops);

/* Short tap and timing-cue buffers start at sample zero. Scheduling the .003s
 * tap lead and the two count-in/four recording beats belongs to the caller. */
echoy_audio_result_t echoy_audio_render_cue(echoy_audio_cue_t cue, echoy_audio_buffer_t **out);
void echoy_audio_free_buffer(echoy_audio_buffer_t *buffer);
