// SPDX-License-Identifier: Apache-2.0
/* Shared framebuffer adapter for fixtures and the live simulator. */
#pragma once
#include "lvgl.h"
static uint8_t pixels[480*480*3],drawbuf[480*480*3];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
    int width=a->x2-a->x1+1;
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){
        size_t dst=(y*480+x)*3,src=((y-a->y1)*width+x-a->x1)*3;
        pixels[dst]=p[src+2];pixels[dst+1]=p[src+1];pixels[dst+2]=p[src];
    }
    lv_display_flush_ready(d);
}
static lv_display_t *pc_display_init(void){lv_init();lv_display_t *display=lv_display_create(480,480);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB888);lv_display_set_buffers(display,drawbuf,NULL,sizeof(drawbuf),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);return display;}
