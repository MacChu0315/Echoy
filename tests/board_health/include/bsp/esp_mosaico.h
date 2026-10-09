#pragma once
#include "driver/i2c_master.h"
typedef enum { BSP_BOARD_VARIANT_V1_0, BSP_BOARD_VARIANT_V1_2 } bsp_board_variant_t;
#define BSP_BATTERY_I2C_ADDR 0x55
#define BSP_BATTERY_I2C_SPEED_HZ 400000
esp_err_t bsp_board_variant_get(bsp_board_variant_t *variant);
esp_err_t bsp_battery_init(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
