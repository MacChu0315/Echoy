// SPDX-License-Identifier: Apache-2.0
#include "bsp/esp_mosaico.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include <stdint.h>

enum { LCD_TRANSFER_QUEUE_DEPTH = 2 };

esp_err_t __real_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *out);

esp_err_t __wrap_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *out)
{
    /* The pinned BSP uses eight-line chunks with internal DMA staging; the
     * SDK rounds the transfer size to DMA descriptor capacity. Two queued
     * chunks bound staging payload to about 16 KiB (excluding allocator and
     * descriptor overhead). Preserve the tested transport flags/ownership. */
    if (config && bus == (esp_lcd_spi_bus_handle_t)(uintptr_t)BSP_LCD_SPI_HOST &&
        config->cs_gpio_num == BSP_LCD_CS && config->flags.quad_mode &&
        config->trans_queue_depth > LCD_TRANSFER_QUEUE_DEPTH) {
        esp_lcd_panel_io_spi_config_t bounded = *config;
        bounded.trans_queue_depth = LCD_TRANSFER_QUEUE_DEPTH;
        ESP_LOGI("echoy_lcd_io", "LCD transfer queue: %u -> %u",
                 (unsigned)config->trans_queue_depth, LCD_TRANSFER_QUEUE_DEPTH);
        return __real_esp_lcd_new_panel_io_spi(bus, &bounded, out);
    }
    return __real_esp_lcd_new_panel_io_spi(bus, config, out);
}
