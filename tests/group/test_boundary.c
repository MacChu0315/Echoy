/* The real owner must realign even an unchanged bank at a group boundary.
 * A Play after ARM starts promptly; changing the clock cannot move that ARM. */
#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void prepare(echoy_band_t *b, int64_t now)
{
    echoy_band_tick(b,now);
    echoy_band_job_t *j=echoy_band_take_job(b);
    if (j) { assert(echoy_band_prepare_job(j)); assert(echoy_band_finish_job(b,j,now)); }
    echoy_band_tick(b,now);
}
static void boundary(bool playing,bool changed)
{
    const int64_t boundary_at=6001250;
    echoy_band_t *b=echoy_band_create(); assert(b);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_EXAMPLE,0,0));
    prepare(b,0); prepare(b,50000); prepare(b,3000000);
    echoy_link_view_t v; assert(echoy_band_link_read(b,&v));
    assert(!v.proposal_pending);
    echoy_link_update_t u={.attached=true,.hold_changes=true,
        .connection_generation=1,.session=1,.transaction=1,
        .command=ECHOY_LINK_BEGIN,.align_boundary=true};
    assert(echoy_band_link_update(b,&u,4000000));
    assert(echoy_band_link_read(b,&v) && v.offer_ready);
    if (changed) {
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,4100000));
        prepare(b,4100000);
        assert(echoy_band_link_read(b,&v) && v.proposal_ready);
        u.transaction=2; u.command=ECHOY_LINK_BEGIN; u.revision=v.proposal_revision;
        assert(echoy_band_link_update(b,&u,4200000));
    }
    echoy_plan_t next_plan=changed ? v.proposal : v.current;
    u.command=ECHOY_LINK_ARM; u.commit_at_us=boundary_at;
    assert(echoy_band_link_update(b,&u,4500000));
    u.command=ECHOY_LINK_COMMIT;
    assert(echoy_band_link_update(b,&u,4600000));
    if (playing) assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,5000000));
    prepare(b,5000000);
    echoy_control_view_t before,after;
    assert(echoy_band_read(b,ECHOY_TILE_A,&before));
    assert(before.state.continuous==playing);
    if (playing) assert(before.state.start_us==5050000);
    assert(echoy_band_link_read(b,&v));
    if (playing) assert(v.clock_origin_us==5050000);
    echoy_audio_loops_t *expected=NULL,*next=NULL;
    assert(echoy_audio_prepare(&before.plan,true,&expected)==ECHOY_AUDIO_OK);
    echoy_audio_result_t result=echoy_audio_prepare(&next_plan,!changed,&next);
    if (result!=ECHOY_AUDIO_OK) fprintf(stderr,"changed=%d role=%d phrase=%u count=%zu duration=%f result=%d\n",changed,next_plan.role,next_plan.phrase,next_plan.count,next_plan.duration,result);
    assert(result==ECHOY_AUDIO_OK);
    float pcm[240]; double energy=0;
    for (int64_t at=5700000;at<6300000;at+=10000) {
        echoy_band_render(b,at,pcm,240);
        for (unsigned i=0;i<240;++i) {
            assert(isfinite(pcm[i]));
            if (!playing) { assert(pcm[i]==0); continue; }
            int64_t sample_at=at+(int64_t)i*1000000/ECHOY_AUDIO_SAMPLE_RATE;
            int64_t origin=sample_at<boundary_at ? 5050000 : boundary_at;
            if (sample_at>=boundary_at && sample_at<boundary_at+8000) continue; /* Existing fade. */
            uint64_t pos=((uint64_t)(sample_at-origin)*ECHOY_AUDIO_SAMPLE_RATE+500000)/1000000;
            echoy_audio_loops_t *bank=sample_at<boundary_at ? expected : next;
            double want=bank->samples[ECHOY_AUDIO_FULL][pos%bank->frames]*.65*.45;
            if (fabs(pcm[i]-want)>=1e-6) { fprintf(stderr,"PCM at=%lld i=%u got=%.9f want=%.9f origin=%lld\n",(long long)at,i,pcm[i],want,(long long)origin); assert(0); }
            energy+=pcm[i]*pcm[i];
        }
    }
    if (playing) assert(energy>1e-6);
    assert(echoy_band_link_read(b,&v));
    assert(v.committed_transaction==u.transaction && v.clock_origin_us==boundary_at);
    assert(echoy_band_read(b,ECHOY_TILE_A,&after));
    assert(after.state.continuous==playing);
    if (!changed) assert(before.plan.count==after.plan.count);
    echoy_audio_free_loops(expected); echoy_audio_free_loops(next); echoy_band_delete(b);
}
int main(void)
{
    boundary(true,false); boundary(false,false); boundary(true,true);
    puts("PASS fixed group boundary: prompt first Play, unchanged bank, in-block exact PCM, paused silence");
}
