// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "echoy_view.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Start the display with the product boot page. */
esp_err_t echoy_ui_start(void);
/** UI-thread-only handle; publish snapshots under BSP display lock. */
echoy_view_t *echoy_ui_view(void);
#ifdef __cplusplus
}
#endif
