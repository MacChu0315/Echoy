#pragma once
#include "esp_err.h"
/* The read-only RPC needs only the trace API's attach declaration. It never
 * dereferences a touch handle or calls any driver function. */
typedef struct esp_lcd_touch *esp_lcd_touch_handle_t;
