#include "esp_lcd_panel_io.h"
#include <assert.h>
#include <stdio.h>
extern int fake_bsp_create(void);
extern esp_lcd_panel_io_spi_config_t observed;
int main(void)
{
    assert(fake_bsp_create()==0 && observed.trans_queue_depth==2);
    puts("PASS wrapper extraction before the BSP archive's constructor reference");
}
