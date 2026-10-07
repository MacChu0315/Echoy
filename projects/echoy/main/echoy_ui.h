#pragma once
#include "esp_gsp.h"
#include <stdint.h>

typedef struct echoy_ui *echoy_ui_handle_t;
typedef struct {
    int64_t (*now_us)(void *ctx);
    void *ctx;
} echoy_platform_t;
esp_gsp_err_t echoy_ui_create(esp_gsp_handle_t gsp, const echoy_platform_t *platform,
                              echoy_ui_handle_t *out);
/* Call outside GSP callbacks. On failure the handle remains alive for retry. */
esp_gsp_err_t echoy_ui_delete(echoy_ui_handle_t ui);
esp_gsp_err_t echoy_ui_get_error(echoy_ui_handle_t ui);
