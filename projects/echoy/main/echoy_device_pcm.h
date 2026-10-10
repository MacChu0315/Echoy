#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Device-only output adaptation at 24 kHz. Reference diagnostics bypass it.
 * The one-millisecond delay permits smooth peak attenuation before a transient. */
/* Musical-bank calibration only; cues bypass these per-role factors.
 * Pulse/Voice unchanged; Dust about -23.5 dB and Harmony about +7.5 dB,
 * from separate on-board listening matches against Voice. */
extern const float echoy_device_role_gains[4];
#define ECHOY_DEVICE_PCM_GAIN 6.0f
/* Board listening trial: Harmony remained distorted below digital clipping.
 * Its clean selected peak was about .65; retain margin for the output chain.
 * This bounds digital samples, not a guarantee of analog/acoustic linearity. */
#define ECHOY_DEVICE_PCM_CEILING .60f
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
