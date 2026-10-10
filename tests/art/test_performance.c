#include "echoy_art.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned hypot_calls;
float __real_hypotf(float x, float y);
float __wrap_hypotf(float x, float y) { ++hypot_calls; return __real_hypotf(x,y); }
int main(void)
{
    uint16_t pixels[300*204];
    echoy_art_frame_t frame={.role=ECHOY_PULSE,.width=300,.height=204};
    esp_gsp_canvas_surface_t s={.pixels=pixels,.stride_bytes=600,.width=300,.height=204,
        .pixel_format=ESP_GSP_CANVAS_PIXEL_RGB565};
    echoy_art_draw(&s,&frame);
    fprintf(stderr,"pulse expensive distance evaluations=%u\n",hypot_calls);
    /* Ring interiors/background should not need per-pixel distance roots. */
    assert(hypot_calls < 20000);
    /* Partial GSP surfaces reuse one complete raster, then an in-place plan
     * update must invalidate it. No assumptions about global singleton state. */
    struct echoy_art_workspace *work=echoy_art_workspace_create();
    assert(work);
    echoy_plan_t plan={.count=1,.events={{.step=0,.note=60,.layer=ECHOY_ANCHOR,.duration=.2}}};
    frame.plan=&plan;
    uint16_t expected[300*204];
    for(unsigned role=0;role<4;role++) {
        frame.role=role; frame.workspace=NULL;
        s.pixels=expected; echoy_art_draw(&s,&frame);
        frame.workspace=work; s.pixels=pixels;
        echoy_art_draw(&s,&frame);
        assert(!memcmp(pixels,expected,sizeof(pixels)));
        unsigned calls=hypot_calls;
        memset(pixels,0,sizeof(pixels));
        for(unsigned y=0;y<204;y+=17) {
            esp_gsp_canvas_surface_t tile=s;
            tile.pixels=pixels+y*300;tile.y=y;tile.height=17;
            echoy_art_draw(&tile,&frame);
        }
        assert(hypot_calls==calls);
        assert(!memcmp(pixels,expected,sizeof(pixels)));
        plan.events[0].note+=2;plan.events[0].step+=1;
        frame.workspace=NULL; s.pixels=expected;echoy_art_draw(&s,&frame);
        frame.workspace=work;s.pixels=pixels;echoy_art_draw(&s,&frame);
        assert(!memcmp(pixels,expected,sizeof(pixels)));
        frame.active=true;frame.energy=.7f;frame.progress=.375;
        frame.workspace=NULL;s.pixels=expected;echoy_art_draw(&s,&frame);
        frame.workspace=work;s.pixels=pixels;echoy_art_draw(&s,&frame);
        assert(!memcmp(pixels,expected,sizeof(pixels)));
        frame.active=false;frame.energy=0;
    }
    echoy_art_workspace_delete(work);
    puts("PASS sparse ring coverage, cached partial surfaces and in-place frame/plan invalidation");
}
