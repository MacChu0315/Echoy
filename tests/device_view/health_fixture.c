/* Test-only health/controller input. Uses the production UI and official GSP;
 * it does not pretend to emulate physical audio, battery or FreeRTOS tasks. */
#include "echoy_ui.h"
#include "gsp_sim_bridge.h"
#include <stdio.h>

static echoy_ui_handle_t ui;
static echoy_control_view_t view = {
    .state = {.role = ECHOY_PULSE, .phrase = 1, .density = .5},
    .plan = {.role = ECHOY_PULSE, .phrase = 1, .density = .5, .duration = 2.5},
    .memories = {1, 1, 1, 2},
    .present = true,
    .source = -1,
    .volume = .65,
    .battery_unknown = true,
    .imu_unknown = true,
};
static int64_t clock_us(void *ctx)
{
    (void)ctx;
    return (int64_t)gsp_sim_bridge_time_ms() * 1000;
}
static bool read_view(void *ctx, echoy_control_view_t *out)
{
    (void)ctx;
    *out = view;
    return true;
}
static bool action(void *ctx, echoy_action_t kind, double value, int64_t at)
{
    (void)ctx;
    (void)at;
    if (kind == ECHOY_ACT_TAP) {
        view.battery_unknown = !view.battery_unknown;
        view.battery = view.battery_unknown ? 0 : 37;
        fprintf(stderr, "HEALTH_FIXTURE unknown=%d battery=%u\n", view.battery_unknown,
                view.battery);
        return true;
    }
    if (kind == ECHOY_ACT_LANGUAGE) {
        view.english = value != 0;
        return true;
    }
    return false;
}
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t gsp)
{
    echoy_platform_t platform = {.now_us = clock_us,
                                 .read_control = read_view,
                                 .perform_action = action,
                                 .is_simulator = false};
    esp_gsp_err_t result = echoy_ui_create(gsp, &platform, &ui);
    fprintf(stderr, "HEALTH_FIXTURE init=%d\n", result);
    return result;
}
void gsp_bridge_app_deinit(esp_gsp_handle_t gsp)
{
    (void)gsp;
    fprintf(stderr, "HEALTH_FIXTURE last_error=%d\n", echoy_ui_get_error(ui));
    esp_gsp_err_t result = echoy_ui_delete(ui);
    fprintf(stderr, "HEALTH_FIXTURE cleanup=%d\n", result);
    if (result == ESP_GSP_OK) {
        ui = NULL;
    }
}
