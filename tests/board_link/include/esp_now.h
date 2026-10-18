#pragma once
#include "esp_err.h"
typedef struct { int unused; } esp_now_send_info_t;
typedef enum { ESP_NOW_SEND_SUCCESS,ESP_NOW_SEND_FAIL } esp_now_send_status_t;
typedef void (*esp_now_send_cb_t)(const esp_now_send_info_t *,esp_now_send_status_t);
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb);
esp_err_t esp_now_unregister_send_cb(void);
