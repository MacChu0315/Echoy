#pragma once
void fake_log(const char *tag, const char *format, ...);
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
