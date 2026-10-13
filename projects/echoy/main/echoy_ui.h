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
    /* Optional malloc/free-compatible storage for cold UI data: view cache
     * and retained encoded images. An absent callback uses the default heap;
     * allocation failure is propagated without default-heap fallback. */
    void *(*ui_storage_alloc)(void *ctx, size_t size);
    /* Runs on the UI owner, never the audio worker. NULL uses simulator shading. */
    bool (*set_brightness)(void *ctx, unsigned percent);
    const char *software_version;
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
