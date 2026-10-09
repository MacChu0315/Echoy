#pragma once
#include <stddef.h>
#include <stdint.h>

/* Explicit diagnostic only: one second of 1 kHz at -12 dBFS peak, with
 * 20 ms ramps. It bypasses musical gain, never starts automatically. */
#define ECHOY_PROBE_FRAMES 24000u
#define ECHOY_PROBE_TAIL_FRAMES 720u
void echoy_audio_probe_render(size_t offset, int16_t *out, size_t frames);
