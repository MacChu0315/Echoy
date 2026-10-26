#include "esp_lcd_panel_io.h"
#include "bsp/esp_mosaico.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern esp_lcd_panel_io_spi_config_t observed;
extern esp_lcd_spi_bus_handle_t observed_bus;
extern unsigned calls;
extern int injected_error;
extern bool observed_null;
int main(void)
{
    esp_lcd_spi_bus_handle_t bus=(void *)(uintptr_t)BSP_LCD_SPI_HOST;
    esp_lcd_panel_io_spi_config_t config={.cs_gpio_num=BSP_LCD_CS,.dc_gpio_num=-1,
        .spi_mode=0,.pclk_hz=40000000,.trans_queue_depth=10,.user_ctx=(void *)7,
        .lcd_cmd_bits=32,.lcd_param_bits=8,.flags={.quad_mode=true,.psram_dma_direct=false}};
    esp_lcd_panel_io_spi_config_t original=config, expected=config;
    expected.trans_queue_depth=2;
    esp_lcd_panel_io_handle_t io=NULL;
    assert(esp_lcd_new_panel_io_spi(bus,&config,&io)==0);
    assert(calls==1 && io==&observed && observed_bus==bus);
    assert(observed.trans_queue_depth==2);
    assert(!memcmp(&config,&original,sizeof(config)));
    assert(!memcmp(&observed,&expected,sizeof(expected)));
    config.trans_queue_depth=1;
    assert(esp_lcd_new_panel_io_spi(bus,&config,&io)==0 && observed.trans_queue_depth==1);
    config.trans_queue_depth=0;
    assert(esp_lcd_new_panel_io_spi(bus,&config,&io)==0 && observed.trans_queue_depth==0);
    config=original; config.cs_gpio_num=12;
    assert(esp_lcd_new_panel_io_spi(bus,&config,&io)==0 && !memcmp(&observed,&config,sizeof(config)));
    config=original;
    assert(esp_lcd_new_panel_io_spi((void *)2,&config,&io)==0 && observed.trans_queue_depth==10);
    config.flags.quad_mode=false;
    assert(esp_lcd_new_panel_io_spi(bus,&config,&io)==0 && observed.trans_queue_depth==10);
    assert(esp_lcd_new_panel_io_spi(bus,NULL,&io)==0x102 && observed_null);
    config=original; injected_error=0x101; io=NULL;
    assert(esp_lcd_new_panel_io_spi(bus,&config,&io)==0x101 && io==NULL);
    puts("PASS bounded LCD staging, untouched caller/other buses, single-slot configuration and driver errors");
}
