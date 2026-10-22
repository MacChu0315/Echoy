// SPDX-License-Identifier: Apache-2.0

#include "board_display.h"
#include "board_touch_trace.h"
#include "bsp/esp_mosaico.h"
#include "echoy_ui.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "iris_ota_support.h"
#include "iris_screen_mirror.h"
#include "nvs_flash.h"
#include "platform_device.h"
#include "echoy_audio_diagnostics.h"
#include "echoy_snapshot_diagnostics.h"
#include "echoy_measurement_diagnostics.h"
#include "sdkconfig.h"
#include "ui_bundle.h"

#if CONFIG_ESP_MOSAICO_APP_AUTO_ACCEPT
#error "Echoy accepts firmware before audio activation; disable ESP_MOSAICO_APP_AUTO_ACCEPT"
#endif
#if CONFIG_ESP_GSP_QUEUE_DEPTH < 64
#error "Echoy startup requires ESP_GSP_QUEUE_DEPTH >= 64; regenerate from current defaults"
#endif
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT && CONFIG_ESP_IRIS_MAX_RPC_HANDLERS < 10
/* Recovery/acceptance: 3; read-only snapshots: 3; bounded measurement: 4. */
#error "Output measurement requires ESP_IRIS_MAX_RPC_HANDLERS >= 10"
#endif

static const char *TAG = "echoy";
static echoy_ui_handle_t s_ui;
static echoy_device_handle_t s_device;
static echoy_snapshot_diagnostics_handle_t s_snapshot_diagnostics;
static int s_brightness_percent = -1;

static bool set_brightness(void *ctx, unsigned percent)
{
    (void)ctx;
    if (s_brightness_percent == (int)percent) {
        return true;
    }
    if (bsp_display_brightness_set((int)percent) != ESP_OK) {
        return false;
    }
    s_brightness_percent = (int)percent;
    return true;
}

/* UI/peripheral failures must not reboot-loop out of retained Recovery. */
#define CHECK_START(operation)                                                                     \
    do {                                                                                           \
        esp_err_t start_error = (operation);                                                       \
        if (start_error != ESP_OK) {                                                               \
            ESP_LOGE(TAG, "%s: %s; Recovery remains active", #operation,                           \
                     esp_err_to_name(start_error));                                                \
            return;                                                                                \
        }                                                                                          \
    } while (0)

void app_main(void)
{
    /* Echoy has no haptics. Take ownership of the motor before any slow or
     * fallible startup work instead of inheriting another firmware's output. */
    const esp_err_t motor_error = bsp_motor_set(false);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    /* Keep Recovery reachable even when the external UI image is absent or
     * invalid. */
    iris_ota_support_start();
    CHECK_START(motor_error);
    ESP_LOGI(TAG, "Motor disabled for Echoy");
    CHECK_START(nvs_flash_init());

    esp_gsp_config_t app_config;
    const esp_err_t bundle_err = ui_bundle_open(&app_config);
    if (bundle_err != ESP_OK) {
        ESP_LOGE(TAG, "GSP UI unavailable (0x%x); ESP-Iris Recovery RPC remains active",
                 bundle_err);
        return;
    }

    esp_display_present_target_config_t display;
    /* Serialize the first initialization of BSP's shared bus/rails before the
     * display, codec and battery workers can use their public interfaces. */
    CHECK_START(bsp_power_init());
    CHECK_START(bsp_i2c_init());
    CHECK_START(board_display_init(&display));
    /* Establish the initial level before GSP can submit pixels on the same
     * panel IO. UI creation also requests 100% on the main task; the cache
     * prevents that duplicate command racing the first display transfer.
     * Subsequent manual changes run in GSP's serialized UI callbacks. */
    CHECK_START(set_brightness(NULL, 100) ? ESP_OK : ESP_FAIL);

    esp_lcd_touch_handle_t touch = NULL;
    CHECK_START(board_touch_init(&touch));

    esp_gsp_esp_lcd_config_t lcd = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd.display = display;
    lcd.touch = touch;
    lcd.perf_log = true;
    /* Observe the existing driver calls; no extra controller reads or ACKs.
     * GSP owns input until reset, including the early-return paths below. */
    const esp_err_t touch_trace_error = board_touch_trace_attach(touch);
    if (touch_trace_error == ESP_OK) {
        lcd.touch_wake_from_isr = board_touch_trace_wake_from_isr;
        lcd.touch_wake_user_ctx = NULL;
    } else {
        ESP_LOGW(TAG, "Touch trace unavailable: %s", esp_err_to_name(touch_trace_error));
    }

    CHECK_START(iris_screen_mirror_init());

    esp_gsp_handle_t ui;
    CHECK_START(esp_gsp_esp_lcd_start(&app_config, &lcd, &ui));
    CHECK_START(iris_screen_mirror_attach(ui));
    echoy_platform_t platform;
    CHECK_START(echoy_device_start(&s_device, &platform));
    platform.set_brightness = set_brightness;
    platform.software_version = esp_app_get_description()->version;
    esp_gsp_err_t ui_error = echoy_ui_create(ui, &platform, &s_ui);
    if (ui_error == ESP_GSP_OK) {
        /* Radio initialization may write RF calibration/NVS. Finish it before
         * activating the uninterrupted speaker sample clock. */
        ui_error = board_link_start(echoy_device_link_read, echoy_device_link_receive, s_device);
    }
    if (ui_error == ESP_GSP_OK) {
        /* Accepting the image writes OTA/NVS metadata and can suspend cached
         * I2S interrupts. Complete it before starting the sample clock, never
         * by lengthening the audio timeout or hiding an interrupted stream. */
        ui_error = esp_iris_mark_healthy();
    }
    if (ui_error == ESP_GSP_OK) {
        ui_error = echoy_device_activate(s_device);
    }
    if (ui_error != ESP_GSP_OK) {
        platform.perform_action(platform.ctx, ECHOY_ACT_CONDITION, ECHOY_AUDIO_FAULT,
                                platform.now_us(platform.ctx));
        ESP_LOGE(TAG, "Application startup failed: %d; Recovery remains active", ui_error);
        if (s_ui && echoy_ui_delete(s_ui) != ESP_GSP_OK) {
            /* A callback may still reference the runtime: keep its storage. */
            ESP_LOGE(TAG, "UI callbacks could not detach; retaining silent runtime");
            return;
        }
        s_ui = NULL;
        if (board_link_stop(5000) != ESP_OK) {
            ESP_LOGE(TAG, "link shutdown pending; retaining callback context");
            return;
        }
        esp_err_t stop_error = echoy_device_stop(&s_device, 5000);
        if (stop_error != ESP_OK) {
            ESP_LOGE(TAG, "runtime shutdown pending: %s", esp_err_to_name(stop_error));
        }
        return;
    }

    ESP_LOGI(TAG,
             "Echoy physical link controller ready at %dx%d RGB565; speaker initializes silently",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    /* Register after all fallible application startup/cleanup. This callback
     * only copies the platform's coherent view cache. The runtime stays alive
     * until reset; any future shutdown must successfully stop this service
     * before releasing s_device. Optional diagnostic failure cannot stop music. */
    esp_err_t snapshot_error = echoy_snapshot_diagnostics_start(&platform, &s_snapshot_diagnostics);
    if (snapshot_error == ESP_OK) {
        ESP_LOGI(TAG, "Read-only plan snapshots ready: RPC 0xECA1");
    } else {
        ESP_LOGW(TAG, "Plan snapshots unavailable: %s; retained_context=%u",
                 esp_err_to_name(snapshot_error), s_snapshot_diagnostics != NULL);
    }
#if CONFIG_ECHOY_AUDIO_DIAGNOSTICS
    CHECK_START(echoy_audio_diagnostics_register(s_device));
    ESP_LOGI(TAG, "Explicit audio diagnostics ready: RPC 0xECA0; no automatic tone");
#endif
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    CHECK_START(echoy_measurement_diagnostics_register(s_device));
    ESP_LOGI(TAG, "Bounded output measurement ready: RPC 0xECA2; microphone inactive until explicit start");
#endif
}
