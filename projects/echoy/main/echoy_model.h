#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "echoy_music.h"

/* Preparation uses wall-clock seconds; the four recording beats keep their tempo. */
#define ECHOY_COUNT_IN_STEPS 3
#define ECHOY_COUNT_IN_STEP_US INT64_C(1000000)

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
    echoy_role_t role;
    unsigned phrase;
    double density;
    bool plan_pending;
    /* Copied owner capability: a frozen group update retains a playable bank. */
    bool current_playable;
    int64_t apply_at_us;
    uint32_t revision, committed_revision;
} echoy_snapshot_t;

echoy_model_handle_t echoy_model_create(void);
void echoy_model_delete(echoy_model_handle_t model);
void echoy_model_get_snapshot(echoy_model_handle_t model, echoy_snapshot_t *out);
/* A late Cancel may never become a new Record request. */
bool echoy_model_cancel_record(echoy_model_handle_t model);
bool echoy_model_record(echoy_model_handle_t model, int64_t now_us);
bool echoy_model_tap(echoy_model_handle_t model, int64_t now_us);
void echoy_model_tick(echoy_model_handle_t model, int64_t now_us);
void echoy_model_pause(echoy_model_handle_t model);
bool echoy_model_transport(echoy_model_handle_t model, int64_t now_us);
bool echoy_model_prepare_done(echoy_model_handle_t model, uint32_t generation, int64_t now_us);
void echoy_model_set_following(echoy_model_handle_t model, bool following);
void echoy_model_set_fault(echoy_model_handle_t model, bool fault);
const echoy_plan_t *echoy_model_get_plan(echoy_model_handle_t model);
unsigned echoy_model_get_role_phrase(echoy_model_handle_t model, echoy_role_t role);
bool echoy_model_select_role(echoy_model_handle_t model, echoy_role_t role, int64_t now_us);
bool echoy_model_select_phrase(echoy_model_handle_t model, unsigned phrase, int64_t now_us);
bool echoy_model_set_density(echoy_model_handle_t model, double density, int64_t now_us);
bool echoy_model_audition(echoy_model_handle_t model, int64_t now_us);
bool echoy_model_load_seed(echoy_model_handle_t model, uint16_t seed, int64_t now_us, bool preview);

/* Managed mode keeps recording/permission policy but lets the shared band
 * prepare and commit linked plans and supply its common output clock. */
echoy_model_handle_t echoy_model_create_managed(void);
void echoy_model_apply_external(echoy_model_handle_t model, const echoy_plan_t *plan,
                                uint32_t revision, bool following, int64_t origin_us,
                                bool clock_started);
void echoy_model_set_external_pending(echoy_model_handle_t model, bool pending, int64_t at_us);
/* Clock corrections must preserve pending plans, captures and playback permission. */
void echoy_model_sync_clock(echoy_model_handle_t model, int64_t origin_us, bool started);
bool echoy_model_start_external(echoy_model_handle_t model, uint32_t generation, int64_t start_us);

/* An explicitly recorded local reply may use a prepared private plan before
 * group commit. This does not publish current, clear network-pending state or
 * grant continuous/follower playback. The model owns the plan copy. */
bool echoy_model_start_local_preview(echoy_model_handle_t model,const echoy_plan_t *plan,
                                     uint32_t generation,int64_t start_us);
bool echoy_model_local_preview(echoy_model_handle_t model);
