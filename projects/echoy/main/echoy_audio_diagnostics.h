#pragma once
#include "platform_device.h"
/* Register only after successful app startup. The device context must remain
 * alive for the rest of this firmware's lifetime (until reset). */
esp_err_t echoy_audio_diagnostics_register(echoy_device_handle_t device);
