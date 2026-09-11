// SPDX-License-Identifier: Apache-2.0

#include "echoy_ui.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "nvs_flash.h"

static const char *TAG = "echoy";

void app_main(void)
{
    const esp_err_t probe_err = esp_iris_boot_probe();
    ESP_ERROR_CHECK_WITHOUT_ABORT(probe_err);
    if (probe_err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-Iris boot probe failed: %s",
                 esp_err_to_name(probe_err));
    }

    const esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "NVS initialization failed without erasing retained data: %s",
                 esp_err_to_name(nvs_err));
        return;
    }

    /* Keep the enter-Recovery RPC alive even if product initialization fails. */
    iris_ota_support_start();

    const esp_err_t ui_err = echoy_ui_start();
    if (ui_err != ESP_OK) {
        ESP_LOGE(TAG, "Echoy display failed; Recovery remains available: %s",
                 esp_err_to_name(ui_err));
        return;
    }

    const esp_err_t healthy_err = esp_iris_mark_healthy();
    if (healthy_err != ESP_OK) {
        ESP_LOGE(TAG, "Echoy started but health confirmation failed: %s",
                 esp_err_to_name(healthy_err));
        return;
    }

    ESP_LOGI(TAG, "Echoy application ready");
}
