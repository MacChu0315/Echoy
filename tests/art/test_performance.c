#include "echoy_art.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned hypot_calls;
static unsigned sin_calls;
float __real_hypotf(float x, float y);
float __wrap_hypotf(float x, float y) { ++hypot_calls; return __real_hypotf(x,y); }
double __real_sin(double x);
double __wrap_sin(double x) { ++sin_calls; return __real_sin(x); }

static void voice_frame(echoy_art_frame_t frame, struct echoy_art_workspace *work,
                        bool new_pitch)
{
    uint16_t expected[300*204], pixels[300*204];
    esp_gsp_canvas_surface_t s={.pixels=expected,.stride_bytes=frame.width*2,
        .width=frame.width,.height=frame.height,.pixel_format=ESP_GSP_CANVAS_PIXEL_RGB565};
    frame.workspace=NULL;
    echoy_art_draw(&s,&frame);
    sin_calls=0;
    frame.workspace=work;
    s.pixels=pixels;
    echoy_art_draw(&s,&frame);
    assert(!memcmp(pixels,expected,frame.width*frame.height*2));
    /* Changing energy, dimensions or progress cannot require new trigonometry
     * while Voice still displays the same pitch. A new pitch remains bounded. */
    if (new_pitch) assert(sin_calls <= 7*141);
    else {
        if (sin_calls) fprintf(stderr,"voice unchanged-pitch sine evaluations=%u\n",sin_calls);
        assert(sin_calls==0);
    }
}

static void voice_pitch_cache(void)
{
    struct echoy_art_workspace *work=echoy_art_workspace_create();
    struct echoy_art_workspace *other=echoy_art_workspace_create();
    assert(work && other);
    echoy_plan_t plan={.count=2,.events={
        {.step=0,.note=60,.layer=ECHOY_ANCHOR,.duration=.2},
        {.step=4,.note=67,.layer=ECHOY_SUPPORT,.duration=.2}}};
    echoy_art_frame_t f={.plan=&plan,.role=ECHOY_VOICE,.active=true,.energy=1,
        .width=300,.height=204};
    voice_frame(f,work,true);
    f.progress=.01;f.energy=.9f;
    voice_frame(f,work,false);
    f.width=206;f.height=101;f.background=0xe5e7de;f.energy=.8f;
    voice_frame(f,work,false);
    f.empty=true;f.anchors_only=true;f.progress=.3;
    voice_frame(f,work,false);
    f.anchors_only=false;
    voice_frame(f,work,true);
    uint16_t pixels[300*204];
    esp_gsp_canvas_surface_t s={.pixels=pixels,.stride_bytes=412,.width=206,.height=101,
        .pixel_format=ESP_GSP_CANVAS_PIXEL_RGB565};
    f.role=ECHOY_DUST;f.workspace=work;
    echoy_art_draw(&s,&f);
    f.role=ECHOY_VOICE;f.energy=.7f;
    voice_frame(f,work,false);
    plan.events[1].note=64;
    voice_frame(f,work,true);
    plan.events[0].note=65;f.energy=.6f;
    voice_frame(f,work,false);
    f.active=false;
    voice_frame(f,work,true);
    f.plan=NULL;f.energy=.5f;
    voice_frame(f,work,false);
    f.plan=&plan;plan.events[0].note=90;
    voice_frame(f,other,true);
    f.plan=NULL;f.energy=.4f;
    voice_frame(f,work,false);
    echoy_art_workspace_delete(other);
    echoy_art_workspace_delete(work);
    puts("PASS Voice pitch cache preserves pixels, selection and independent workspaces");
}

static void transparent_path_distance_budget(void)
{
    uint16_t pixels[300*204];
    echoy_art_frame_t f={.role=ECHOY_VOICE,.width=300,.height=204,
        .active=true,.energy=.7f,.progress=.27};
    esp_gsp_canvas_surface_t s={.pixels=pixels,.stride_bytes=600,.width=300,.height=204,
        .pixel_format=ESP_GSP_CANVAS_PIXEL_RGB565};
    hypot_calls=0;
    echoy_art_draw(&s,&f);
    fprintf(stderr,"voice path expensive distance evaluations=%u\n",hypot_calls);
    /* Native edge scaling alone still evaluates transparent corners of each
     * segment's conservative rectangle. Their coverage cannot affect pixels. */
    assert(hypot_calls < 20000);
}
int main(void)
{
    uint16_t pixels[300*204];
    echoy_art_frame_t frame={.role=ECHOY_PULSE,.width=300,.height=204};
    esp_gsp_canvas_surface_t s={.pixels=pixels,.stride_bytes=600,.width=300,.height=204,
        .pixel_format=ESP_GSP_CANVAS_PIXEL_RGB565};
    echoy_art_draw(&s,&frame);
    fprintf(stderr,"pulse expensive distance evaluations=%u\n",hypot_calls);
    /* Native-size distance roots must not be repeated for edge scaling. */
    assert(hypot_calls < 6000);
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
    voice_pitch_cache();
    transparent_path_distance_budget();
}
