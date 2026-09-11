// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_SPEAKER_H
#define ECHOY_SPEAKER_H
#include "echoy_audio.h"
#include "esp_err.h"
typedef struct {
    echoy_audio_stats_t dsp;
    bool output_fault,retry_required;
    int codec_error,close_error;
    unsigned dma_descriptors,dma_frames,software_blocks,block_frames;
    int64_t estimated_dma_us; /* Config-derived bound, NOT measured DAC onset. */
    int64_t max_render_write_us;
    uint64_t output_gaps,plan_rejections;
} echoy_speaker_status_t;
/* Call start once on the control owner. Publish copies a full immutable plan
 * into a four-message queue. It never waits for UI or audio completion.
 * Only local_device is synthesized; no PCM traverses the network. */
esp_err_t echoy_speaker_start(int local_device);
esp_err_t echoy_speaker_publish(const echoy_bar_plan_t *plan);
esp_err_t echoy_speaker_retry(void);
bool echoy_speaker_status(echoy_speaker_status_t *out);
#endif
