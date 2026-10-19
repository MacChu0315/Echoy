/* Run the v1.1.0 real three-owner early-reply cases with pickup enabled. */
#include "echoy_band.h"
#include <assert.h>
#include <string.h>
static unsigned previews_checked;
static double pickup_energy;
static unsigned quiet_pickup_samples;
static echoy_band_t *pickup_band(void)
{
    echoy_band_t *b=echoy_band_create();assert(b&&echoy_band_enable_pickup(b,true));return b;
}
static bool pickup_prepare(echoy_band_job_t *j)
{
    return echoy_band_prepare_job(j)&&echoy_band_prepare_pickup_job(j);
}
static bool pickup_view(echoy_band_t *b,echoy_tile_id_t id,echoy_control_view_t *v)
{
    bool ok=echoy_band_read(b,id,v);
    if(ok && id==ECHOY_TILE_A && v->state.phase==ECHOY_PREVIEW && v->plan.source_count) {
        echoy_pickup_t expected;assert(echoy_pickup_apply(&v->plan,&expected));
        assert(!memcmp(&expected,&v->pickup,sizeof expected));++previews_checked;
    }
    return ok;
}
/* Keep the existing group's exact base-PCM oracle, while observing the
 * independently scheduled real pickup stream rather than expecting silence. */
static void pickup_render(echoy_band_t *b,int64_t at,float *base,size_t frames)
{
    assert(frames<=240);float intro[240];uint8_t gate[240];
    echoy_control_view_t view;assert(echoy_band_read(b,ECHOY_TILE_A,&view));
    bool quiet=!view.state.continuous &&
        (view.state.phase==ECHOY_IDLE || view.state.phase==ECHOY_PREPARING);
    echoy_band_render_layers(b,at,base,intro,gate,frames);
    for(size_t i=0;i<frames;i++) {
        assert(gate[i] || intro[i]==0);pickup_energy+=intro[i]*intro[i];
        if(quiet) {assert(intro[i]==0);++quiet_pickup_samples;}
    }
}
#define echoy_band_render pickup_render
#define echoy_band_create pickup_band
#define echoy_band_prepare_job pickup_prepare
#define echoy_band_read pickup_view
#define main original_three_main
#include "../group/test_three.c"
#undef main
int main(void)
{
    char *args[]={"pickup-local-reply","local-reply",NULL};
    int result=original_three_main(2,args);assert(previews_checked>0 && pickup_energy>0 && quiet_pickup_samples>0);
    printf("PASS %u pickup display snapshots match the actual early reply, before and after group commit\n",previews_checked);
    printf("PASS %u pickup samples stay silent during Pause and non-continuous preparation\n",quiet_pickup_samples);
    return result;
}
