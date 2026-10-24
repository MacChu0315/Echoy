#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
typedef void *esp_lcd_spi_bus_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef struct {
    int cs_gpio_num, dc_gpio_num, spi_mode, pclk_hz;
    size_t trans_queue_depth;
    void *on_color_trans_done, *user_ctx;
    int lcd_cmd_bits, lcd_param_bits;
    struct { unsigned quad_mode:1, psram_dma_direct:1, lsb_first:1; } flags;
} esp_lcd_panel_io_spi_config_t;
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t,
    const esp_lcd_panel_io_spi_config_t *, esp_lcd_panel_io_handle_t *);
