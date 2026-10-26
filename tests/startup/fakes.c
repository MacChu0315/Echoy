#include "fakes.h"
#include <assert.h>
#include <string.h>

void app_main(void);
static bool rendering, in_ui_callback, ready, audio, healthy;
static bool motor_on = true, motor_attempted, motor_off_before_iris, iris_started, powered;
static unsigned brightness_calls, fail_level, fail_remaining;
static int actual_brightness = -1;
static echoy_platform_t captured;
static const char *test_case;

esp_err_t esp_iris_boot_probe(void) { return ESP_OK; }
esp_err_t bsp_motor_set(bool on)
{
    motor_attempted = true;
    assert(!iris_started && !powered);
    if (!strcmp(test_case,"motor_failure")) return ESP_FAIL;
    motor_on = on;
    return ESP_OK;
}
void iris_ota_support_start(void) { motor_off_before_iris = !motor_on; iris_started = true; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t ui_bundle_open(esp_gsp_config_t *p)
{ (void)p; return strcmp(test_case,"bundle_failure") ? ESP_OK : ESP_FAIL; }
esp_err_t bsp_power_init(void) { powered = true; return ESP_OK; }
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
esp_err_t board_display_init(esp_display_present_target_config_t *p) { (void)p; return ESP_OK; }
esp_err_t bsp_display_brightness_set(int level)
{
    ++brightness_calls;
    /* Model the unsupported main/render concurrent access to panel IO. */
    if (rendering && !in_ui_callback) return ESP_FAIL;
    if ((unsigned)level == fail_level && fail_remaining) {
        --fail_remaining;
        return ESP_FAIL;
    }
    actual_brightness = level;
    return ESP_OK;
}
esp_err_t board_touch_init(esp_lcd_touch_handle_t *p) { *p = (void *)1; return ESP_OK; }
esp_err_t board_touch_trace_attach(esp_lcd_touch_handle_t p) { (void)p; return ESP_OK; }
void board_touch_trace_wake_from_isr(void *p) { (void)p; }
esp_err_t iris_screen_mirror_init(void) { return ESP_OK; }
esp_err_t iris_screen_mirror_attach(esp_gsp_handle_t p) { (void)p; return ESP_OK; }
esp_err_t esp_gsp_esp_lcd_start(const esp_gsp_config_t *a, const esp_gsp_esp_lcd_config_t *b, esp_gsp_handle_t *p)
{ (void)a; (void)b; rendering = true; *p = (void *)1; return ESP_OK; }
static int64_t now_us(void *p) { (void)p; return 0; }
static bool action(void *p, int a, double v, int64_t t) { (void)p; (void)a; (void)v; (void)t; return true; }
esp_err_t echoy_device_start(echoy_device_handle_t *d, echoy_platform_t *p)
{ *d = (void *)1; *p = (echoy_platform_t){.now_us=now_us, .perform_action=action}; return ESP_OK; }
const esp_app_desc_t *esp_app_get_description(void)
{ static const esp_app_desc_t desc = {.version="test"}; return &desc; }
esp_gsp_err_t echoy_ui_create(esp_gsp_handle_t g, const echoy_platform_t *p, echoy_ui_handle_t *u)
{
    (void)g;
    captured = *p;
    *u = (void *)1;
    /* Real UI creation runs on main after GSP starts, requesting initial 100%. */
    return p->set_brightness(p->ctx, 100) ? ESP_OK : ESP_FAIL;
}
void echoy_device_link_read(void) {}
void echoy_device_link_receive(void) {}
esp_err_t board_link_start(void (*r)(void), void (*s)(void), void *p)
{ (void)r; (void)s; (void)p; return strcmp(test_case,"radio_failure") ? ESP_OK : ESP_FAIL; }
esp_err_t esp_iris_mark_healthy(void)
{ if (!strcmp(test_case,"health_failure")) return ESP_FAIL; healthy = true; return ESP_OK; }
esp_err_t echoy_device_activate(echoy_device_handle_t p)
{ (void)p; assert(healthy); audio = true; return ESP_OK; }
esp_err_t echoy_ui_delete(echoy_ui_handle_t p) { (void)p; return ESP_OK; }
esp_err_t board_link_stop(unsigned t) { (void)t; return ESP_OK; }
esp_err_t echoy_device_stop(echoy_device_handle_t *p, unsigned t) { (void)t; *p = NULL; return ESP_OK; }
esp_err_t echoy_snapshot_diagnostics_start(const echoy_platform_t *p, echoy_snapshot_diagnostics_handle_t *d)
{ (void)p; *d = (void *)1; ready = true; return ESP_OK; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "test"; }

int main(int argc, char **argv)
{
    assert(argc == 2); test_case = argv[1];
    if (!strcmp(test_case,"initial_failure")) { fail_level = 100; fail_remaining = 1; }
    app_main();
    assert(motor_attempted && iris_started);
    if (!strcmp(test_case,"motor_failure")) {
        assert(!powered && !rendering && !audio && !healthy && !ready);
    } else if (!strcmp(test_case,"bundle_failure")) {
        assert(!powered && !rendering && !audio && !healthy && !ready);
    } else if (!strcmp(test_case,"initial_failure")) {
        assert(!rendering && !audio && !healthy && !ready);
        assert(actual_brightness == -1);
    } else if (!strcmp(test_case,"radio_failure") || !strcmp(test_case,"health_failure")) {
        assert(rendering && !audio && !healthy && !ready);
        assert(actual_brightness == 100);
    } else {
        assert(rendering && audio && healthy && ready);
        assert(actual_brightness == 100 && brightness_calls == 1);
        in_ui_callback = true;
        assert(captured.set_brightness(NULL,20));
        assert(actual_brightness == 20 && brightness_calls == 2);
        assert(captured.set_brightness(NULL,20) && brightness_calls == 2);
        fail_level = 60; fail_remaining = 1;
        assert(!captured.set_brightness(NULL,60) && actual_brightness == 20);
        assert(captured.set_brightness(NULL,60) && actual_brightness == 60);
        assert(brightness_calls == 4);
    }
    if (strcmp(test_case,"motor_failure")) assert(!motor_on && motor_off_before_iris);
    puts("PASS startup ordering and brightness ownership");
}
