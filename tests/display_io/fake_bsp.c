#include "esp_lcd_panel_io.h"
#include "bsp/esp_mosaico.h"
#include <stdint.h>
int fake_bsp_create(void)
{
    const esp_lcd_panel_io_spi_config_t config={.cs_gpio_num=BSP_LCD_CS,
        .trans_queue_depth=10,.flags={.quad_mode=true}};
    esp_lcd_panel_io_handle_t io;
    return esp_lcd_new_panel_io_spi((void *)(uintptr_t)BSP_LCD_SPI_HOST,&config,&io);
}
