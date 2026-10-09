#pragma once
#include <stdbool.h>
#include "driver/i2s_std.h"
#define BSP_AUDIO_PA_CTRL 45
#define BSP_AUDIO_I2S_MCLK 54
#define BSP_AUDIO_I2S_SCLK 37
#define BSP_AUDIO_I2S_LRCLK 49
#define BSP_AUDIO_I2S_SDOUT 52
#define BSP_AUDIO_CODEC_I2C_ADDR (0x19u << 1)
#define BSP_I2C_PORT 0
typedef enum { BSP_BOARD_VARIANT_V1_0, BSP_BOARD_VARIANT_V1_2 } bsp_board_variant_t;
esp_err_t bsp_board_variant_get(bsp_board_variant_t *);
esp_err_t bsp_power_set_vcc_3v3(bool);
esp_err_t bsp_power_set_codec_3v3(bool);
esp_err_t bsp_i2c_init(void);
void *bsp_i2c_get_handle(void);
