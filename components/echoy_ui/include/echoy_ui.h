#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the 480x480 display and render the minimal Echoy boot surface. */
esp_err_t echoy_ui_start(void);

#ifdef __cplusplus
}
#endif
