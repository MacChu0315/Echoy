// SPDX-License-Identifier: Apache-2.0

#include "board_display.h"
#include "bsp/esp_mosaico.h"
#include "echoy_ui.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "iris_screen_mirror.h"
#include "nvs_flash.h"
#include "platform_device.h"
#include "ui_bundle.h"

static const char *TAG = "echoy";
static echoy_ui_handle_t s_ui;
static echoy_device_handle_t s_device;

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
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    /* Keep Recovery reachable even when the external UI image is absent or
     * invalid. */
    iris_ota_support_start();
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

    esp_lcd_touch_handle_t touch = NULL;
    CHECK_START(board_touch_init(&touch));

    esp_gsp_esp_lcd_config_t lcd = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd.display = display;
    lcd.touch = touch;

    CHECK_START(iris_screen_mirror_init());

    esp_gsp_handle_t ui;
    CHECK_START(esp_gsp_esp_lcd_start(&app_config, &lcd, &ui));
    CHECK_START(iris_screen_mirror_attach(ui));
    echoy_platform_t platform;
    CHECK_START(echoy_device_start(&s_device, &platform));
    esp_gsp_err_t ui_error = echoy_ui_create(ui, &platform, &s_ui);
    if (ui_error != ESP_GSP_OK) {
        platform.perform_action(platform.ctx, ECHOY_ACT_CONDITION, ECHOY_AUDIO_FAULT,
                                platform.now_us(platform.ctx));
        ESP_LOGE(TAG, "UI controller failed: %d; Recovery remains active", ui_error);
        if (s_ui && echoy_ui_delete(s_ui) != ESP_GSP_OK) {
            /* A callback may still reference the runtime: keep its storage. */
            ESP_LOGE(TAG, "UI callbacks could not detach; retaining silent runtime");
            return;
        }
        s_ui = NULL;
        esp_err_t stop_error = echoy_device_stop(&s_device, 5000);
        if (stop_error != ESP_OK) {
            ESP_LOGE(TAG, "runtime shutdown pending: %s", esp_err_to_name(stop_error));
        }
        return;
    }

    ESP_LOGI(TAG,
             "Echoy single-device controller ready at %dx%d RGB565; speaker initializes silently",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
}
