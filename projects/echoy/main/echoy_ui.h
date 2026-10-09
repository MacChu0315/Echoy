#pragma once
#include "esp_gsp.h"
#include <stdint.h>
#include "echoy_control.h"

typedef struct echoy_ui *echoy_ui_handle_t;
typedef struct {
    int64_t (*now_us)(void *ctx);
    void *ctx;
    bool is_simulator;
    bool reduced_motion;
    bool (*read_control)(void *ctx, echoy_control_view_t *out);
    bool (*perform_action)(void *ctx, echoy_action_t action, double value, int64_t now_us);
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

/* Only after the GSP owner has closed its handle and completed all releases. */
void echoy_ui_release_closed(echoy_ui_handle_t ui);
