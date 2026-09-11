// SPDX-License-Identifier: Apache-2.0
#include "echoy_ui.h"
#include "echoy_view.h"
#include "bsp/esp_mosaico.h"
#include "esp_check.h"
#include "esp_log.h"
static const char *TAG="echoy_ui";
static echoy_view_t *view;
echoy_view_t *echoy_ui_view(void){return view;}
esp_err_t echoy_ui_start(void){
    lv_display_t *display=bsp_display_start();
    ESP_RETURN_ON_FALSE(display,ESP_FAIL,TAG,"start display");
    ESP_RETURN_ON_FALSE(lv_display_get_horizontal_resolution(display)==480&&lv_display_get_vertical_resolution(display)==480,ESP_ERR_INVALID_SIZE,TAG,"480x480 display required");
    ESP_RETURN_ON_FALSE(bsp_display_lock(-1),ESP_ERR_TIMEOUT,TAG,"lock display");
    view=echoy_view_create(lv_display_get_screen_active(display),NULL,NULL);
    if(view){echoy_snapshot_t *s=lv_malloc(sizeof(*s));if(s){const uint64_t ids[]={1,2,3};echoy_core_init(s,ids);s->devices[0].lifecycle=ECHOY_BOOTING;echoy_view_update(view,s,0);lv_free(s);}else{echoy_view_destroy(view);view=NULL;}}
    bsp_display_unlock();
    ESP_RETURN_ON_FALSE(view,ESP_ERR_NO_MEM,TAG,"create product view");
    ESP_LOGI(TAG,"Echoy shared LVGL product UI ready at 480x480");return ESP_OK;
}
