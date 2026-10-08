#pragma once
#include "esp_gsp.h"
#include <stdint.h>
#include "echoy_model.h"

typedef struct echoy_ui *echoy_ui_handle_t;
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
typedef struct {
    int64_t (*now_us)(void *ctx);
    void *ctx;
    bool is_simulator;
    unsigned battery_percent;
    uint16_t initial_seed;
    echoy_condition_t initial_condition;
    echoy_command_t (*poll_command)(void *ctx); /* Non-blocking PC/hardware input adapter. */
    void (*set_removed)(void *ctx, bool removed);
    bool (*export_session)(void *ctx, const echoy_snapshot_t *state, const echoy_plan_t *plan,
                           double volume);
} echoy_platform_t;
esp_gsp_err_t echoy_ui_create(esp_gsp_handle_t gsp, const echoy_platform_t *platform,
                              echoy_ui_handle_t *out);
/* Call outside GSP callbacks. On failure the handle remains alive for retry. */
esp_gsp_err_t echoy_ui_delete(echoy_ui_handle_t ui);
esp_gsp_err_t echoy_ui_get_error(echoy_ui_handle_t ui);
