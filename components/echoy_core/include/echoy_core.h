// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_CORE_H
#define ECHOY_CORE_H
#include "echoy_music.h"
#include <stdint.h>
#define ECHOY_MAX_DEVICES 3
#define ECHOY_CONTACT_US INT64_C(450000)
#define ECHOY_HOLD_US INT64_C(1800000)
#define ECHOY_BEAT_US INT64_C(625000)
#define ECHOY_BAR_US INT64_C(2500000)
#define ECHOY_NO_DEVICE (-1)
typedef enum { ECHOY_OFF, ECHOY_BOOTING, ECHOY_READY } echoy_lifecycle_t;
typedef enum { ECHOY_PAUSED, ECHOY_WAITING, ECHOY_PLAYING } echoy_transport_t;
typedef enum { ECHOY_SYNC_NONE, ECHOY_SYNC_PREPARING, ECHOY_SYNC_WAITING, ECHOY_SYNC_FAILED } echoy_sync_stage_t;
typedef enum {
    ECHOY_BOOT, ECHOY_BOOT_READY, ECHOY_POWER_OFF, ECHOY_PLAY_TOGGLE, ECHOY_PAUSE,
    ECHOY_PLAY_PREPARE, ECHOY_PLAY_READY, ECHOY_PLAY_FAILED,
    ECHOY_ROLE_REQUEST, ECHOY_ROLE_READY, ECHOY_ROLE_FAILED, ECHOY_ROLE_CANCEL,
    ECHOY_PHRASE, ECHOY_ACTIVITY, ECHOY_FINISH_ACTIVITY, ECHOY_VOLUME, ECHOY_VOLUME_OPEN,
    ECHOY_OUTPUT_FAULT, ECHOY_DATA_FAULT, ECHOY_DATA_LOADING,
    ECHOY_LINK, ECHOY_MOVE, ECHOY_CONTACTS, ECHOY_SYNC_READY, ECHOY_SYNC_FAIL,
    ECHOY_NOTICE, ECHOY_BATTERY
} echoy_action_t;
typedef struct { int left, right; bool uncertain; } echoy_edge_t;
typedef struct {
    echoy_action_t type; int device; int64_t at_us;
    uint64_t boot_id, generation; double value;
    size_t edge_count; echoy_edge_t edges[2];
} echoy_input_t;
typedef enum { ECHOY_OK, ECHOY_INVALID, ECHOY_STALE, ECHOY_NOT_READY, ECHOY_TOPOLOGY_REJECTED } echoy_result_t;
typedef struct {
    uint64_t uid, boot_id, session_id, role_generation, play_generation, clock_version;
    echoy_lifecycle_t lifecycle; echoy_transport_t transport; echoy_role_t role;
    double activity, committed_activity, volume, memories[4];
    int phrase, phrase_memories[4], pending_role, preparing_role, pending_phrase, phrase_role;
    bool output_fault, data_fault, loading, motif_available, role_error, play_preparing;
    bool link_online, lost_split, moving, volume_open;
    /* Local event-output eligibility, not measured DAC onset feedback. */
    bool audible; int64_t audible_since_us;
    int battery; unsigned discovered;
    int64_t link_lost_us, moving_since_us, play_at_us, notice_until_us;
    uint64_t play_session; int clock_ppm;
} echoy_device_t;
typedef struct {
    uint64_t id; unsigned members; int leader; int64_t epoch_us, created_us, last_bar;
    int rate_ppm; bool wire_managed, arrangement_pending; unsigned target_edges;
    int64_t arrangement_at_us;
} echoy_session_t;
typedef struct { bool present, uncertain; int64_t since_us; } echoy_contact_t;
typedef struct {
    echoy_sync_stage_t stage; uint64_t generation, reference;
    unsigned members; int64_t started_us, apply_at_us;
    uint64_t boots[ECHOY_MAX_DEVICES];
} echoy_sync_t;
typedef struct {
    int device, source; uint64_t boot_id, session_id, clock_version; int64_t bar, epoch_us;
    int rate_ppm; echoy_role_t role; int phrase; echoy_transport_t transport;
    int64_t audible_since_us;
    double activity, committed_activity, volume;
    bool motif_available; echoy_motif_t motif, events;
} echoy_plan_entry_t;
typedef struct { uint64_t revision; int64_t at_us; size_t count; echoy_plan_entry_t devices[3]; } echoy_bar_plan_t;
typedef struct {
    int64_t time_us; uint64_t revision, serial, generation;
    echoy_device_t devices[3]; echoy_session_t sessions[3];
    echoy_contact_t contacts[3][3]; unsigned committed_edges;
    /* The wire control owner supplies sync status; core must not auto-merge it. */
    bool external_sync; echoy_sync_t sync; echoy_bar_plan_t plan;
} echoy_core_t;
typedef echoy_core_t echoy_snapshot_t;
/* Single control-task owner. Callbacks must enqueue inputs for that owner.
 * Caller copies snapshot/plan on the owner and publishes the copy via its own
 * queue. No returned plan contains pointers to mutable core state. */
void echoy_core_init(echoy_core_t *core, const uint64_t uids[3]);
echoy_result_t echoy_dispatch(echoy_core_t *core, const echoy_input_t *event);
void echoy_tick(echoy_core_t *core, int64_t at_us);
void echoy_snapshot(const echoy_core_t *core, echoy_snapshot_t *out);
void echoy_bar_plan(const echoy_core_t *core, echoy_bar_plan_t *out);
const char *echoy_feedback_key(const echoy_core_t *core, int device);
int64_t echoy_next_boundary(const echoy_session_t *session, int64_t at_us, bool bar);
long double echoy_phase_steps(const echoy_session_t *session, int64_t at_us);
#endif
