#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CONFIG_ESP_GSP_QUEUE_DEPTH 64
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_GSP_OK ESP_OK
#define ECHOY_ACT_CONDITION 1
#define ECHOY_AUDIO_FAULT 1
#define BSP_LCD_H_RES 480
#define BSP_LCD_V_RES 480
#define ESP_ERROR_CHECK_WITHOUT_ABORT(call) ((void)(call))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))

typedef int esp_err_t;
typedef int esp_gsp_err_t;
typedef void *esp_gsp_handle_t;
typedef void *echoy_ui_handle_t;
typedef void *echoy_device_handle_t;
typedef void *echoy_snapshot_diagnostics_handle_t;
typedef void *esp_lcd_touch_handle_t;
typedef struct { int unused; } esp_gsp_config_t;
typedef struct { int unused; } esp_display_present_target_config_t;
typedef struct {
    esp_display_present_target_config_t display;
    esp_lcd_touch_handle_t touch;
    bool perf_log;
    void (*touch_wake_from_isr)(void *);
    void *touch_wake_user_ctx;
} esp_gsp_esp_lcd_config_t;
#define ESP_GSP_ESP_LCD_CONFIG_INIT() ((esp_gsp_esp_lcd_config_t){0})
typedef struct {
    void *ctx;
    bool (*set_brightness)(void *, unsigned);
    bool (*perform_action)(void *, int, double, int64_t);
    int64_t (*now_us)(void *);
    const char *software_version;
} echoy_platform_t;
typedef struct { const char *version; } esp_app_desc_t;

esp_err_t esp_iris_boot_probe(void);
void iris_ota_support_start(void);
esp_err_t nvs_flash_init(void);
esp_err_t ui_bundle_open(esp_gsp_config_t *);
esp_err_t bsp_power_init(void);
esp_err_t bsp_i2c_init(void);
esp_err_t board_display_init(esp_display_present_target_config_t *);
esp_err_t bsp_display_brightness_set(int);
esp_err_t board_touch_init(esp_lcd_touch_handle_t *);
esp_err_t board_touch_trace_attach(esp_lcd_touch_handle_t);
void board_touch_trace_wake_from_isr(void *);
esp_err_t iris_screen_mirror_init(void);
esp_err_t iris_screen_mirror_attach(esp_gsp_handle_t);
esp_err_t esp_gsp_esp_lcd_start(const esp_gsp_config_t *, const esp_gsp_esp_lcd_config_t *, esp_gsp_handle_t *);
esp_err_t echoy_device_start(echoy_device_handle_t *, echoy_platform_t *);
const esp_app_desc_t *esp_app_get_description(void);
esp_gsp_err_t echoy_ui_create(esp_gsp_handle_t, const echoy_platform_t *, echoy_ui_handle_t *);
void echoy_device_link_read(void);
void echoy_device_link_receive(void);
esp_err_t board_link_start(void (*)(void), void (*)(void), void *);
esp_err_t esp_iris_mark_healthy(void);
esp_err_t echoy_device_activate(echoy_device_handle_t);
esp_err_t echoy_ui_delete(echoy_ui_handle_t);
esp_err_t board_link_stop(unsigned);
esp_err_t echoy_device_stop(echoy_device_handle_t *, unsigned);
esp_err_t echoy_snapshot_diagnostics_start(const echoy_platform_t *, echoy_snapshot_diagnostics_handle_t *);
const char *esp_err_to_name(esp_err_t);
