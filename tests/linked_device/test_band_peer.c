#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static echoy_control_view_t view(echoy_band_t *b)
{
    echoy_control_view_t v;
    assert(echoy_band_read(b, ECHOY_TILE_A, &v));
    return v;
}
static void prepare(echoy_band_t *b, int64_t now)
{
    echoy_band_job_t *j = echoy_band_take_job(b);
    assert(j && echoy_band_prepare_job(j));
    assert(echoy_band_finish_job(b, j, now));
}
static double energy(echoy_band_t *b, int64_t at)
{
    float *pcm = calloc(12000, sizeof(*pcm));
    assert(pcm);
    echoy_band_render(b, at, pcm, 12000);
    double result = 0;
    for (size_t i = 0; i < 12000; ++i) { assert(isfinite(pcm[i])); result += pcm[i]*pcm[i]; }
    free(pcm);
    return result;
}
int main(void)
{
    echoy_band_t *b = echoy_band_create();
    assert(b);
    prepare(b, 0); echoy_band_tick(b, 45000);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_HARMONY,50000));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,50000));
    echoy_plan_t upstream, expected;
    assert(echoy_compose_mask(0x1128,ECHOY_VOICE,2,.5,&upstream));
    /* Imported pitch is intentionally different from a seed-only rebuild. */
    for (size_t i=0; i<upstream.count; ++i) {
        upstream.events[i].note = 79;
        upstream.events[i].harmony_count = 3;
        upstream.events[i].harmony[0] = 55;
        upstream.events[i].harmony[1] = 62;
        upstream.events[i].harmony[2] = 79;
    }
    assert(echoy_compose(upstream.source,upstream.source_count,ECHOY_HARMONY,3,.5,&expected));
    assert(echoy_arrange_linked(&expected,&upstream,&expected));
    assert(echoy_band_set_upstream(b,true,&upstream,true,200000,true,1000000));
    assert(view(b).state.following);
    assert(!echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,1000010));
    prepare(b,1000000);
    echoy_band_tick(b,2700000);
    echoy_control_view_t v = view(b);
    assert(v.source==ECHOY_TILE_B && v.state.phase==ECHOY_IDLE);
    assert(v.plan.count==expected.count);
    for (size_t i=0;i<expected.count;++i) {
        assert(v.plan.events[i].note==expected.events[i].note);
        assert(v.plan.events[i].harmony_count==expected.events[i].harmony_count);
        assert(!memcmp(v.plan.events[i].harmony,expected.events[i].harmony,sizeof(expected.events[i].harmony)));
    }
    assert(energy(b,2700000)==0); /* Remote playing never grants local playback. */
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,3210000));
    echoy_band_tick(b,3210000);
    v=view(b);
    assert(v.state.phase==ECHOY_PLAYING && v.state.start_us==5200000);
    assert(energy(b,5300000)>1e-6);
    assert(echoy_band_set_upstream(b,true,&upstream,true,201000,true,5900000));
    assert(view(b).state.clock_origin_us==201000);
    assert(!echoy_band_take_job(b)); /* Clock-only or repeated data does not re-synthesize. */
    echoy_plan_t retained=view(b).plan;
    assert(echoy_band_set_upstream(b,false,NULL,false,0,false,6000000));
    prepare(b,6000000); echoy_band_tick(b,7701000);
    v=view(b);
    assert(!v.state.following && v.source==ECHOY_TILE_NONE && v.state.phase==ECHOY_PLAYING);
    assert(v.plan.count==retained.count);
    assert(!memcmp(v.plan.events,retained.events,retained.count*sizeof(retained.events[0])));
    assert(energy(b,7801000)>1e-6);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,8400000));
    assert(echoy_band_set_upstream(b,true,&upstream,true,201000,true,8500000));
    prepare(b,8500000); echoy_band_tick(b,10201000);
    assert(view(b).state.phase==ECHOY_IDLE && energy(b,10301000)==0);
    assert(echoy_band_set_upstream(b,false,NULL,false,0,false,11000000));
    prepare(b,11000000); echoy_band_tick(b,11045000);
    assert(echoy_band_establish_clock(b,11100000));
    assert(view(b).state.clock_started && view(b).state.phase==ECHOY_IDLE);
    echoy_band_delete(b);
    b=echoy_band_create(); assert(b); prepare(b,0); echoy_band_tick(b,45000);
    assert(echoy_band_establish_clock(b,100000));
    assert(view(b).state.clock_started && energy(b,100000)==0);
    assert(echoy_band_set_upstream(b,true,NULL,false,0,false,700000));
    assert(view(b).state.following && !view(b).plan.count);
    assert(!echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,700001));
    assert(energy(b,700000)==0);
    echoy_band_delete(b);
    puts("PASS imported full pitches, silent attach/rejoin, explicit bar-aligned start, clock refresh, exact detach retention, empty-parent lock");
    return 0;
}
