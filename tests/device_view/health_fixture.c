/* Test-only health/controller input. Uses the production UI and official GSP;
 * it does not pretend to emulate physical audio, battery or FreeRTOS tasks. */
#include "echoy_ui.h"
#include "gsp_sim_bridge.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

static echoy_ui_handle_t ui;
static bool initializing;
static unsigned submitted, capacity;
static bool measurement_test, feedback_test, density_queued;
static double density_target;
static int64_t density_apply_us;

/* Model the bounded ESP-IDF queue while its render task is occupied by the
 * first frame. Forward accepted calls to the actual simulator, not a fake UI. */
static bool admit(void)
{
    return !initializing || (++submitted <= capacity);
}
#define WRAP_SCALAR(name, type) \
    esp_gsp_err_t __real_##name(esp_gsp_handle_t, gsp_component_key_t, type); \
    esp_gsp_err_t __wrap_##name(esp_gsp_handle_t g, gsp_component_key_t k, type v) \
    { return admit() ? __real_##name(g, k, v) : ESP_GSP_ERR_TIMEOUT; }
WRAP_SCALAR(esp_gsp_component_set_visible, bool)
WRAP_SCALAR(esp_gsp_component_set_enabled, bool)
WRAP_SCALAR(esp_gsp_component_set_color_rgb888, uint32_t)
WRAP_SCALAR(esp_gsp_component_set_value, int32_t)
WRAP_SCALAR(esp_gsp_component_set_text, const char *)
esp_gsp_err_t __real_esp_gsp_component_set_property(esp_gsp_handle_t, gsp_component_key_t,
                                                   gsp_property_key_t, const gsp_value_t *);
esp_gsp_err_t __wrap_esp_gsp_component_set_property(esp_gsp_handle_t g, gsp_component_key_t k,
                                                    gsp_property_key_t p, const gsp_value_t *v)
{
    return admit() ? __real_esp_gsp_component_set_property(g, k, p, v) : ESP_GSP_ERR_TIMEOUT;
}
static echoy_control_view_t view = {
    .state = {.role = ECHOY_PULSE, .phrase = 1, .density = .5},
    .plan = {.role = ECHOY_PULSE, .phrase = 1, .density = .5, .duration = 2.5},
    .memories = {1, 1, 1, 1},
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
static bool brightness(void *ctx, unsigned percent)
{
    (void)ctx;
    fprintf(stderr, "HEALTH_FIXTURE brightness=%u\n", percent);
    return true;
}
static bool read_view(void *ctx, echoy_control_view_t *out)
{
    (void)ctx;
    if (feedback_test && density_queued && clock_us(NULL)>=density_apply_us) {
        view.state.density=density_target;view.state.revision++;
        density_queued=false;
        fprintf(stderr,"FEEDBACK_FIXTURE applied=%.17g\n",density_target);
    }
    *out = view;
    return true;
}
static bool action(void *ctx, echoy_action_t kind, double value, int64_t at)
{
    (void)ctx;
    (void)at;
    if (measurement_test) {
        fprintf(stderr, "MEASUREMENT_FIXTURE action=%u\n", (unsigned)kind);
        if (kind == ECHOY_ACT_PAUSE) {
            view.measurement_active = view.measurement_microphone = false;
            fprintf(stderr, "MEASUREMENT_FIXTURE cancelled\n");
            return true;
        }
        return false;
    }
    if (feedback_test && kind==ECHOY_ACT_DENSITY) {
        assert(!density_queued);
        density_target=value;density_apply_us=at+500000;density_queued=true;
        fprintf(stderr,"FEEDBACK_FIXTURE queued=%.17g\n",value);
        return true;
    }
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
    if (getenv("ECHOY_TEST_FEEDBACK")) {
        feedback_test=true;view.battery_unknown=false;view.battery=100;
    }
    capacity = getenv("ECHOY_TEST_QUEUE_DEPTH") ? (unsigned)atoi(getenv("ECHOY_TEST_QUEUE_DEPTH")) : 64;
    if (getenv("ECHOY_TEST_MEASUREMENT")) {
        measurement_test = true;
        view.measurement_active = true;
        view.measurement_microphone = atoi(getenv("ECHOY_TEST_MEASUREMENT")) == 1;
    }
    if (getenv("ECHOY_TEST_LOADING")) {
        view.condition = ECHOY_LOADING;
        view.state.fault = true;
    }
    echoy_platform_t platform = {.now_us = clock_us,
                                 .set_brightness = brightness,
                                 .software_version = "1.0.0",
                                 .read_control = read_view,
                                 .perform_action = action,
                                 .is_simulator = false};
    initializing = true;
    esp_gsp_err_t result = echoy_ui_create(gsp, &platform, &ui);
    initializing = false;
    fprintf(stderr, "HEALTH_FIXTURE startup_submissions=%u capacity=%u\n", submitted, capacity);
    view.condition = ECHOY_NORMAL;
    view.state.fault = false;
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
