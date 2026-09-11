// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
typedef enum { UI_PERFORM, UI_ROLE, UI_STATUS } echoy_route_t;
typedef enum { UI_DOWN, UI_MOVE, UI_UP, UI_CANCEL } echoy_pointer_t;
typedef enum { UI_NONE, UI_CHOOSE, UI_BACK, UI_PLAY, UI_ROLE_SELECT, UI_PHRASE_SELECT,
    UI_VOLUME_OPEN, UI_VOLUME_CLOSE, UI_SET_VOLUME, UI_SET_ACTIVITY, UI_FINISH_ACTIVITY,
    UI_RECOVER, UI_CANCEL_LOAD } echoy_ui_action_t;
typedef struct { echoy_ui_action_t action; double value; } echoy_ui_event_t;
typedef struct {
    echoy_route_t route; bool modal, loading, blocked; double activity, volume;
    int capture, start_y, pressed; double start_value;
} echoy_touch_t;
echoy_ui_event_t echoy_touch(echoy_touch_t *, echoy_pointer_t, int x, int y);
