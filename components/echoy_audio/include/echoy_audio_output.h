// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_AUDIO_OUTPUT_H
#define ECHOY_AUDIO_OUTPUT_H
#include "echoy_audio.h"
typedef struct {
    /* Failed open may leave partial state; close must accept that state and
     * return success only when another open can safely initialize afresh. */
    int (*open)(void *ctx, unsigned rate, unsigned bits, unsigned channels);
    int (*write)(void *ctx, const void *pcm, size_t bytes);
    int (*close)(void *ctx);
    void *ctx;
} echoy_output_ops_t;
typedef struct {
    echoy_audio_t *audio; echoy_output_ops_t ops;
    int16_t blocks[ECHOY_AUDIO_BLOCKS][ECHOY_AUDIO_FRAMES*2];
    unsigned block; int last_error,close_error; bool opened,retry_required;
    uint64_t writes,write_errors,open_errors;
} echoy_audio_output_t;
/* One audio-task owner. Four preallocated rotating write buffers; synchronous
 * backend owns its DMA queue. Failure never changes a core or bar_plan. */
bool echoy_output_init(echoy_audio_output_t *o,echoy_audio_t *a,echoy_output_ops_t ops);
bool echoy_output_open(echoy_audio_output_t *o,int64_t presentation_us);
bool echoy_output_block(echoy_audio_output_t *o);
bool echoy_output_close(echoy_audio_output_t *o);
#endif
