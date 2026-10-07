#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct echoy_model *echoy_model_handle_t;
typedef enum {
    ECHOY_IDLE,
    ECHOY_COUNT_IN,
    ECHOY_RECORDING,
    ECHOY_PREPARING,
    ECHOY_PREVIEW,
    ECHOY_PLAYING
} echoy_phase_t;
typedef enum { ECHOY_NOTICE_NONE, ECHOY_NOTICE_CANCELLED, ECHOY_NOTICE_EMPTY } echoy_notice_t;
typedef struct {
    echoy_phase_t phase;
    echoy_notice_t notice;
    uint16_t seed, pending_seed;
    uint32_t generation;
    int64_t capture_at_us, start_us, clock_origin_us;
    bool continuous, clock_started, following, fault;
} echoy_snapshot_t;

echoy_model_handle_t echoy_model_create(void);
void echoy_model_delete(echoy_model_handle_t model);
void echoy_model_get_snapshot(echoy_model_handle_t model, echoy_snapshot_t *out);
bool echoy_model_record(echoy_model_handle_t model, int64_t now_us);
bool echoy_model_tap(echoy_model_handle_t model, int64_t now_us);
void echoy_model_tick(echoy_model_handle_t model, int64_t now_us);
void echoy_model_pause(echoy_model_handle_t model);
bool echoy_model_transport(echoy_model_handle_t model, int64_t now_us);
bool echoy_model_prepare_done(echoy_model_handle_t model, uint32_t generation, int64_t now_us);
void echoy_model_set_following(echoy_model_handle_t model, bool following);
void echoy_model_set_fault(echoy_model_handle_t model, bool fault);
