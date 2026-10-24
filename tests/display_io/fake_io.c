#include "esp_lcd_panel_io.h"
#include <string.h>
esp_lcd_panel_io_spi_config_t observed;
esp_lcd_spi_bus_handle_t observed_bus;
unsigned calls;
int injected_error;
bool observed_null;
esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *out)
{
    ++calls; observed_bus=bus; observed_null=config==NULL;
    if (config) observed=*config;
    if (injected_error) return injected_error;
    if (!config || !out) return 0x102;
    *out=&observed;
    return 0;
}
