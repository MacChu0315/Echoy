// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "echoy_ui.h"
#include "esp_err.h"

typedef struct echoy_device *echoy_device_handle_t;

/* Firmware lifecycle only. BSP power/I2C must already be initialized. GSP
 * callbacks receive only nonblocking queue admission and a cached loading view.
 * Workers remain gated until activate: finish startup flash writes first. */
esp_err_t echoy_device_start(echoy_device_handle_t *out, echoy_platform_t *platform);
/* Lifecycle owner only, after UI creation and successful esp_iris_mark_healthy.
 * Starts silent audio once; never call from a render callback. */
esp_err_t echoy_device_activate(echoy_device_handle_t device);
/* After detaching all UI callbacks, outside any GSP task. Preparation may need
 * to finish before shutdown; timeout retains the handle for a later attempt. */
esp_err_t echoy_device_stop(echoy_device_handle_t *device, uint32_t timeout_ms);
