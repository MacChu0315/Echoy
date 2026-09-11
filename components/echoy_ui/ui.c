// SPDX-License-Identifier: Apache-2.0

#include "echoy_ui.h"

#include "bsp/esp_mosaico.h"
#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "echoy_ui";

esp_err_t echoy_ui_start(void)
{
    lv_display_t *display = bsp_display_start();
    ESP_RETURN_ON_FALSE(display != NULL, ESP_FAIL, TAG, "start display");

    const int32_t width = lv_display_get_horizontal_resolution(display);
    const int32_t height = lv_display_get_vertical_resolution(display);
    ESP_RETURN_ON_FALSE(width == BSP_LCD_H_RES && height == BSP_LCD_V_RES,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "unexpected display resolution: %" PRId32 "x%" PRId32,
                        width, height);
    ESP_RETURN_ON_FALSE(bsp_display_lock(-1), ESP_ERR_TIMEOUT, TAG,
                        "lock display");

    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF6F6F3), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Echoy");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x101010), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t *status = lv_label_create(screen);
    lv_label_set_text(status, "Starting...");
    lv_obj_set_style_text_color(status, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_align_to(status, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    bsp_display_unlock();
    ESP_LOGI(TAG, "Echoy boot UI ready at %" PRId32 "x%" PRId32, width,
             height);
    return ESP_OK;
}
