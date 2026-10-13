#pragma once
#include "echoy_art.h"
#include <stddef.h>
#include <stdint.h>

typedef struct echoy_art_producer echoy_art_producer_t;
typedef struct {
    /* Required, malloc/free-compatible. Device callers provide PSRAM-only
     * storage; a failed allocation is never retried on a different heap. */
    void *(*storage_alloc)(void *ctx, size_t size);
    void *storage_ctx;
} echoy_art_producer_config_t;

typedef struct {
    uint32_t rendered, published, stale, coalesced;
    uint32_t draw_max_us;
    uint32_t worker_stack_min; /* ESP bytes; zero before the first draw/on host. */
} echoy_art_producer_stats_t;

/* The UI/lifecycle owner serializes these calls. Only the worker and GSP's
 * release callback run concurrently. Create owns two 300x204 RGB565 buffers,
 * copied plans and an independent workspace. No worker operation calls GSP. */
esp_gsp_err_t echoy_art_producer_create(const echoy_art_producer_config_t *config,
                                      echoy_art_producer_t **out);

/* Nonblocking structural invalidation. Already accepted GSP frames remain
 * borrowed until release; an epoch change cannot retract a queued GSP frame. */
void echoy_art_producer_set_epoch(echoy_art_producer_t *producer, uint32_t epoch);

/* Nonblocking latest-request mailbox. Copies frame and the complete plan;
 * ignores frame.workspace. False means busy, stopping, wrong epoch or invalid
 * dimensions (only 300x204 is supported); the UI retains its pending request.
 * Same-epoch progress/energy changes do not invalidate completed frames. */
bool echoy_art_producer_request(echoy_art_producer_t *producer,
                                const echoy_art_frame_t *frame, uint32_t epoch);

/* UI owner only: calls the public nonblocking canvas_try_push. No-ready is OK
 * with published=false. Immediate errors, including TIMEOUT, retain the frame
 * for a later attempt and are returned to the caller. Success borrows storage
 * until exactly one GSP release, including a release inside this API call. */
esp_gsp_err_t echoy_art_producer_publish_ready(echoy_art_producer_t *producer,
                                             esp_gsp_handle_t gsp, uint16_t bind,
                                             uint32_t current_epoch, bool *published);

/* Stops requests and joins/deletes only a confirmed-quiescent worker. Timeout
 * preserves all storage and is retryable. Success does not release GSP frames.
 * Call outside input/render callbacks; never from the audio owner. */
esp_gsp_err_t echoy_art_producer_stop(echoy_art_producer_t *producer, uint32_t timeout_ms);

/* After stop, canvas_stop and the GSP release/flush barrier. If a worker or
 * borrowed frame remains, returns INVALID_STATE with *producer unchanged. */
esp_gsp_err_t echoy_art_producer_destroy(echoy_art_producer_t **producer);

/* Independent atomic counters, not a transactional snapshot. Never waits. */
void echoy_art_producer_get_stats(const echoy_art_producer_t *producer,
                                 echoy_art_producer_stats_t *out);
