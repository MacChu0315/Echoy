#pragma once
#include "echoy_audio.h"

/* A successfully validated immutable view. Its backing bytes must outlive all
 * loads. Loaded buffers are independently owned and use the ordinary free APIs.
 * Open/load are preparation-worker operations, never PCM-owner operations. */
typedef struct { const unsigned char *bytes; } echoy_fixed_audio_t;
bool echoy_fixed_audio_open(const void *bytes, size_t size, bool speaker,
                           const float gains[4], echoy_fixed_audio_t *out);
echoy_audio_result_t echoy_fixed_audio_cue(const echoy_fixed_audio_t *view,
    echoy_audio_cue_t cue, echoy_audio_buffer_t **out);
echoy_audio_result_t echoy_fixed_audio_preview(const echoy_fixed_audio_t *view,
    echoy_role_t role, echoy_audio_preview_pool_t *out);
/* Supplied by the firmware's embedded resource provider (or the host fixture). */
bool echoy_fixed_audio_embedded(bool speaker, const float gains[4], echoy_fixed_audio_t *out);
