#pragma once
#include "echoy_model.h"
#include "echoy_pickup.h"
/* Local audition only; touch recording keeps every input timestamp. */
#define ECHOY_PREVIEW_MIN_INTERVAL_US INT64_C(100000)
typedef enum {
    ECHOY_NORMAL,
    ECHOY_AUDIO_FAULT,
    ECHOY_LOADING,
    ECHOY_BATTERY_EMPTY
} echoy_condition_t;
typedef enum {
    ECHOY_COOP_SOLO,
    ECHOY_COOP_CONNECTED,
    ECHOY_COOP_WAITING,
    ECHOY_COOP_DEFERRED,
    ECHOY_COOP_FOLLOWING,
    ECHOY_COOP_RECOVERING
} echoy_cooperation_t;
typedef enum {
    ECHOY_COMMAND_NONE,
    ECHOY_COMMAND_RESTORE,
    ECHOY_COMMAND_AUDIO_FAULT,
    ECHOY_COMMAND_LOADING,
    ECHOY_COMMAND_RECOVER,
    ECHOY_COMMAND_BATTERY_EMPTY
} echoy_command_t;
typedef enum {
    ECHOY_ACT_RECORD,
    ECHOY_ACT_TAP, /* value0: legacy/capture; value1..4: visible preview role+1 */
    ECHOY_ACT_PAUSE,
    ECHOY_ACT_TRANSPORT,
    ECHOY_ACT_AUDITION,
    ECHOY_ACT_ROLE,
    ECHOY_ACT_PHRASE,
    ECHOY_ACT_DENSITY,
    ECHOY_ACT_VOLUME,
    ECHOY_ACT_CONDITION,
    ECHOY_ACT_PRESENT,
    ECHOY_ACT_EXAMPLE,
    ECHOY_ACT_LANGUAGE,
    ECHOY_ACT_CANCEL_RECORD,
    ECHOY_ACT_REDUCED_MOTION
} echoy_action_t;
typedef struct {
    echoy_snapshot_t state;
    echoy_plan_t plan;
    echoy_pickup_t pickup; /* Display only; plan remains the relationship source. */
    unsigned memories[4], battery;
    double volume;
    bool present, preparing, preparation_failed, english, reduced_motion;
    /* Hardware adapters distinguish unavailable readings from real zeroes. */
    bool battery_unknown, imu_unknown;
    /* Finite diagnostic only; ordinary touch recording never opens a mic. */
    bool measurement_active, measurement_microphone;
    int tile, source, clock;
    echoy_condition_t condition;
    echoy_cooperation_t cooperation;
} echoy_control_view_t;
