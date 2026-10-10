#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Device-only output adaptation at 24 kHz. Reference diagnostics bypass it.
 * The one-millisecond delay permits smooth peak attenuation before a transient. */
#define ECHOY_DEVICE_PCM_GAIN 6.0f
#define ECHOY_DEVICE_PCM_CEILING .90f
#define ECHOY_DEVICE_PCM_LOOKAHEAD 24

typedef struct {
    float samples[ECHOY_DEVICE_PCM_LOOKAHEAD + 1];
    float gains[ECHOY_DEVICE_PCM_LOOKAHEAD];
    float envelope;
    unsigned sample_at, gain_at;
} echoy_device_pcm_t;
typedef struct {
    size_t limited, clipped;
} echoy_device_pcm_stats_t;

void echoy_device_pcm_reset(echoy_device_pcm_t *state);
bool echoy_device_pcm_convert(echoy_device_pcm_t *state, const float *source, int16_t *out,
                              size_t frames, echoy_device_pcm_stats_t *stats);
