#include "echoy_band.c"
#include "reference_render.h"
#include <assert.h>
#include <stdio.h>
static void action_both(echoy_band_t *a, echoy_band_t *b, int tile, echoy_action_t act, double value, int64_t at)
{ assert(echoy_band_action(a,tile,act,value,at)==echoy_band_action(b,tile,act,value,at)); }
static void prepare_both(echoy_band_t *a, echoy_band_t *b, int64_t now)
{
    echoy_band_job_t *x=echoy_band_take_job(a), *y=echoy_band_take_job(b);
    assert((x!=NULL)==(y!=NULL));
    if(x){assert(echoy_band_prepare_job(x)&&echoy_band_prepare_job(y));assert(echoy_band_finish_job(a,x,now)&&echoy_band_finish_job(b,y,now));}
}
static bool compare(echoy_band_t *a, echoy_band_t *b, int64_t at, size_t frames)
{
    float *x=malloc(frames*sizeof(*x)),*y=malloc(frames*sizeof(*y));assert(x&&y);
    echoy_band_render(a,at,x,frames); reference_render(b,at,y,frames);
    for(size_t i=0;i<frames;++i) if(memcmp(&x[i],&y[i],sizeof(float))) {fprintf(stderr,"PCM mismatch time=%lld sample=%zu got=%.9g ref=%.9g\n",(long long)at,i,x[i],y[i]);abort();}
    bool audible=false;
    for(size_t i=0;i<frames;++i) audible |= y[i]!=0;
    for(int t=0;t<N;++t) assert(a->voices[t].smoothed_volume==b->voices[t].smoothed_volume);
    free(x);free(y);
    return audible;
}
int main(void)
{
    unsigned comparisons=0;
    for(int role=0;role<4;++role)for(int phrase=1;phrase<=3;++phrase){
        echoy_band_t *a=echoy_band_create(),*b=echoy_band_create();assert(a&&b);
        prepare_both(a,b,0);
        compare(a,b,0,2400); comparisons++; /* empty commit inside a block */
        action_both(a,b,0,ECHOY_ACT_VOLUME,.17,100000);
        compare(a,b,100000,2400); comparisons++; /* silent volume smoothing */
        action_both(a,b,0,ECHOY_ACT_TAP,0,250000);
        action_both(a,b,0,ECHOY_ACT_RECORD,0,260000);
        compare(a,b,250000,144000);comparisons++; /* future cues/expiry, three-count preparation */
        action_both(a,b,0,ECHOY_ACT_CANCEL_RECORD,0,6300000);
        action_both(a,b,0,ECHOY_ACT_ROLE,role,6400000);
        action_both(a,b,0,ECHOY_ACT_PHRASE,phrase,6400000);
        action_both(a,b,0,ECHOY_ACT_EXAMPLE,0,6500000);
        prepare_both(a,b,6500000);
        assert(compare(a,b,6500000,84000));comparisons++; /* commit starts preview during render */
        /* The real owner ticks even when there is no pending arrangement;
         * finish the one-shot reply before requesting continuous playback. */
        echoy_band_tick(a,10000000);echoy_band_tick(b,10000000);
        action_both(a,b,0,ECHOY_ACT_TRANSPORT,0,10100000);
        echoy_band_tick(a,10100000);echoy_band_tick(b,10100000);
        action_both(a,b,0,ECHOY_ACT_VOLUME,.83,10200000);
        assert(compare(a,b,10200000,12000));comparisons++;
        action_both(a,b,0,ECHOY_ACT_DENSITY,.91,10800000);
        prepare_both(a,b,10800000);
        assert(compare(a,b,10800000,72000));comparisons++; /* next-bar bank change/crossfade in same block */
        action_both(a,b,0,ECHOY_ACT_PAUSE,0,13900000);
        compare(a,b,13900000,24000);comparisons++;
        action_both(a,b,1,ECHOY_ACT_PRESENT,1,15000000);
        action_both(a,b,1,ECHOY_ACT_TAP,0,15000000);
        action_both(a,b,0,ECHOY_ACT_TAP,0,15000500);
        assert(compare(a,b,15000000,12000));comparisons++; /* multiple voice cues preserve addition order */
        echoy_band_delete(a);echoy_band_delete(b);
    }
    printf("PASS %u PCM comparisons are bit-identical to frozen render, including in-block commits/crossfades/cues/volume/cancellation\n",comparisons);
}
