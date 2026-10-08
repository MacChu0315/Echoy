#pragma once
#include "echoy_model.h"
typedef enum {
    ECHOY_NORMAL,
    ECHOY_AUDIO_FAULT,
    ECHOY_LOADING,
    ECHOY_BATTERY_EMPTY
} echoy_condition_t;
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
    ECHOY_ACT_TAP,
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
    ECHOY_ACT_LANGUAGE
} echoy_action_t;
typedef struct {
    echoy_snapshot_t state;
    echoy_plan_t plan;
    unsigned memories[4], battery;
    double volume;
    bool present, preparing, preparation_failed, english;
    int tile, source, clock;
    echoy_condition_t condition;
} echoy_control_view_t;
