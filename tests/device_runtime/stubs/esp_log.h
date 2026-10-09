#pragma once
void fake_log(const char *tag, const char *format, ...);
#define ESP_LOGE(tag, ...) fake_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) fake_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) fake_log(tag, __VA_ARGS__)
