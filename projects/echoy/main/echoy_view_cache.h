#pragma once
#include "esp_gsp.h"
typedef struct echoy_view_cache *echoy_view_cache_t;
echoy_view_cache_t echoy_view_cache_create(esp_gsp_handle_t gsp);
void echoy_view_cache_delete(echoy_view_cache_t view);
esp_gsp_err_t echoy_view_show(echoy_view_cache_t view,const char *name,bool visible);
esp_gsp_err_t echoy_view_enable(echoy_view_cache_t view,const char *name,bool enabled);
esp_gsp_err_t echoy_view_text(echoy_view_cache_t view,const char *name,const char *text);
esp_gsp_err_t echoy_view_color(echoy_view_cache_t view,const char *name,uint32_t rgb888);
esp_gsp_err_t echoy_view_value(echoy_view_cache_t view,const char *name,int32_t value);
esp_gsp_err_t echoy_view_y(echoy_view_cache_t view,const char *name,int32_t y);

esp_gsp_err_t echoy_view_x(echoy_view_cache_t view,const char *name,int32_t x);
esp_gsp_err_t echoy_view_opacity(echoy_view_cache_t view,const char *name,uint8_t alpha);
esp_gsp_err_t echoy_view_scale(echoy_view_cache_t view,const char *name,uint32_t scale_q16);
