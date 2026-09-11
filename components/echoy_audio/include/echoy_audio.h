// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_AUDIO_H
#define ECHOY_AUDIO_H
#include "echoy_core.h"
#define ECHOY_AUDIO_RATE 48000
#define ECHOY_AUDIO_FRAMES 256
#define ECHOY_AUDIO_BLOCKS 4
#define ECHOY_AUDIO_VOICES 64
#define ECHOY_AUDIO_PENDING 192
typedef struct echoy_audio echoy_audio_t;
typedef struct {
    uint64_t started, missed, cancelled, capacity_errors, clipped_samples;
    unsigned active, peak_voices, pending;
    int64_t cursor;
    bool output_fault;
} echoy_audio_stats_t;
/* All calls have one audio-task owner, with no allocation or locking after init.
 * Storage must be suitably aligned (malloc/static union); allocate once at boot.
 * epoch_us names the estimated DAC presentation time of output sample zero. */
size_t echoy_audio_size(void);
echoy_audio_t *echoy_audio_init(void *storage, size_t size, int64_t epoch_us);
bool echoy_audio_schedule(echoy_audio_t *a, int device, uint64_t clock_version,
                          int64_t target_us, const echoy_event_t *event);
bool echoy_audio_plan(echoy_audio_t *a, const echoy_bar_plan_t *plan, unsigned device_mask);
void echoy_audio_control(echoy_audio_t *a, int device, uint64_t clock_version,
                         double expression, double volume, bool mute);
void echoy_audio_master(echoy_audio_t *a, double volume);
void echoy_audio_render(echoy_audio_t *a, float *stereo, size_t frames);
void echoy_audio_pcm(echoy_audio_t *a, int16_t *stereo, size_t frames);
/* Advance over missed output time without replaying pending notes. */
int64_t echoy_audio_presentation_us(const echoy_audio_t *a);
void echoy_audio_skip_to(echoy_audio_t *a, int64_t presentation_us);
void echoy_audio_output_fault(echoy_audio_t *a, bool fault);
echoy_audio_stats_t echoy_audio_stats(const echoy_audio_t *a);
#endif
