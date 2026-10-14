#pragma once
#include "platform_device.h"
/* Temporary diagnostic service. Register only after fallible startup has
 * finished; the device context must then remain alive until reset. Registration
 * never starts capture. Ordinary firmware does not register this service. */
esp_err_t echoy_measurement_diagnostics_register(echoy_device_handle_t device);
