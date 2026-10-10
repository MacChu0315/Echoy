#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Device output calibration, after the unchanged shared musical mixer.
 * Reference diagnostics bypass this gain to retain their fixed test level. */
#define ECHOY_DEVICE_PCM_GAIN 3.0f
bool echoy_device_pcm_convert(const float *source, int16_t *out, size_t frames,
                              size_t *clipped_samples);
