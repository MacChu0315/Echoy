#include "echoy_device_pcm.h"
#define echoy_audio_render_pickup pickup_render_injected
#include "../../projects/echoy/main/echoy_band.c"
#undef echoy_audio_render_pickup
#include <assert.h>
#include <stdio.h>

echoy_audio_result_t echoy_audio_render_pickup(const echoy_note_t *,size_t,echoy_role_t,bool,float *);
static bool fail_pickup;
echoy_audio_result_t pickup_render_injected(const echoy_note_t *n,size_t count,echoy_role_t r,bool speaker,float *out)
{
    if(fail_pickup) {fail_pickup=false;return ECHOY_AUDIO_NO_MEMORY;}
    return echoy_audio_render_pickup(n,count,r,speaker,out);
}
static echoy_band_t *make(unsigned role,unsigned phrase,double density,uint16_t mask,bool enabled)
{
    echoy_band_t *b=echoy_band_create(); assert(b);
    assert(echoy_band_enable_pickup(b,enabled));
    assert(echoy_model_select_role(b->voices[0].model,role,0));
    if(mask) assert(set_seed(b,0,mask));
    echoy_arrange_patch_t patch={.set_role=true,.role=role,.set_phrase=true,.phrase=phrase,.set_density=true,.density=density};
    assert(echoy_arrange_configure(b->arrange,0,&patch));
    echoy_band_job_t *j=echoy_band_take_job(b); assert(j);
    echoy_arrange_job_t before=j->arrangement;
    assert(echoy_band_prepare_job(j));
    assert(echoy_band_voice_for_speaker_job(j));
    assert(echoy_band_calibrate_job(j,echoy_office_role_gains));
    float *base=NULL;
    if(j->banks[0]) {base=malloc(60000*sizeof(float));assert(base);memcpy(base,j->banks[0]->loops->samples[2],60000*sizeof(float));}
    assert(echoy_band_prepare_pickup_job(j));
    assert(!memcmp(&j->arrangement,&before,sizeof before));
    if(base) {assert(!memcmp(base,j->banks[0]->loops->samples[2],60000*sizeof(float)));free(base);}
    assert(!echoy_band_prepare_pickup_job(j));
    assert(!echoy_band_calibrate_job(j,echoy_office_role_gains));
    assert(echoy_band_finish_job(b,j,0));
    echoy_band_tick(b,100000);
    assert(!echoy_band_enable_pickup(b,!enabled));
    if(mask) assert(echoy_band_action(b,0,ECHOY_ACT_TRANSPORT,0,100000));
    echoy_band_tick(b,200000);
    return b;
}
static void comparison(unsigned role,unsigned phrase,double density,uint16_t mask)
{
    echoy_band_t *old=make(role,phrase,density,mask,false),*now=make(role,phrase,density,mask,true);
    echoy_control_view_t a,b; assert(echoy_band_read(old,0,&a)&&echoy_band_read(now,0,&b));
    assert(!memcmp(&a.plan,&b.plan,sizeof a.plan));
    assert(a.state.start_us==b.state.start_us);
    echoy_pickup_t expected;assert(echoy_pickup_apply(&b.plan,&expected));
    assert(!memcmp(&b.pickup,&expected,sizeof expected));
    echoy_device_pcm_t orig,modified;echoy_device_pcm_reset(&orig);echoy_device_pcm_reset(&modified);
    echoy_pickup_pcm_t extra;echoy_pickup_pcm_reset(&extra);
    size_t changes=0,late_changes=0;
    for(size_t offset=0;offset<120000;offset+=240) {
        int64_t at=a.state.start_us+(int64_t)(offset*1000000/24000);
        float f[240],base[240],intro[240];uint8_t gate[240];int16_t x[240],y[240];
        echoy_band_render(old,at,f,240);
        echoy_band_render_layers(now,at,base,intro,gate,240);
        assert(!memcmp(f,base,sizeof f));
        echoy_device_pcm_stats_t st;
        assert(echoy_device_pcm_convert(&orig,f,x,240,&st));
        assert(echoy_device_pcm_convert(&modified,base,y,240,&st));
        assert(!memcmp(x,y,sizeof x));
        assert(echoy_device_pcm_add_pickup(&extra,intro,gate,y,240));
        for(size_t k=0;k<240;k++) {
            size_t pos=(offset+k)%60000;
            changes+=x[k]!=y[k];
            if(!expected.count || pos>=expected.end)late_changes+=x[k]!=y[k];
            assert(abs(y[k])<=19661);
        }
        assert(!memcmp(&orig,&modified,sizeof orig));
    }
    assert(!late_changes);
    if(expected.count)assert(changes>0);else assert(!changes);
    echoy_band_delete(old);echoy_band_delete(now);
}
static void stale_and_chain(void)
{
    echoy_band_t *b=make(1,2,.8,0x50,true);
    for(int edit=0;edit<2;edit++) {
        assert(echoy_band_action(b,0,ECHOY_ACT_DENSITY,.2+edit*.2,300000+edit*100000));
        echoy_band_job_t *j=echoy_band_take_job(b);assert(j);
        assert(echoy_band_prepare_job(j)&&echoy_band_voice_for_speaker_job(j));
        if(edit==0)assert(echoy_band_action(b,0,ECHOY_ACT_ROLE,3,310000));
        if(edit==1)assert(set_seed(b,0,0x500));
        assert(echoy_band_calibrate_job(j,echoy_office_role_gains)&&echoy_band_prepare_pickup_job(j));
        assert(!echoy_band_finish_job(b,j,600000+edit*100000));
    }
    echoy_band_delete(b);
    b=make(1,1,.8,0x50,true);
    assert(echoy_band_action(b,0,ECHOY_ACT_PAUSE,0,300000));
    assert(echoy_band_action(b,0,ECHOY_ACT_RECORD,0,400000));
    /* A relation compile can be in flight during capture; Cancel must not
     * resurrect playback, whether its still-valid plan commits or is stale. */
    assert(echoy_arrange_invalidate(b->arrange));
    echoy_band_job_t *cancelled=echoy_band_take_job(b);assert(cancelled);
    assert(echoy_band_prepare_job(cancelled));
    assert(echoy_band_action(b,0,ECHOY_ACT_CANCEL_RECORD,0,500000));
    assert(echoy_band_prepare_pickup_job(cancelled));
    (void)echoy_band_finish_job(b,cancelled,600000);
    echoy_band_tick(b,3000000);
    echoy_control_view_t cancelled_view;assert(echoy_band_read(b,0,&cancelled_view));
    assert(cancelled_view.state.phase==ECHOY_IDLE && !cancelled_view.state.continuous);
    float silence[240];echoy_band_render(b,3000000,silence,240);
    for(unsigned i=0;i<240;i++)assert(silence[i]==0);
    echoy_band_delete(b);
    b=make(0,1,.8,0x50,true);
    echoy_plan_t root=echoy_arrange_get_tile(b->arrange,0)->current.music;
    assert(echoy_band_action(b,1,ECHOY_ACT_PRESENT,1,300000));
    assert(echoy_band_action(b,2,ECHOY_ACT_PRESENT,1,300000));
    for(unsigned round=0;round<6;round++) {
        echoy_arrange_edge_t edges[]={{0,1},{1,2}};
        assert(echoy_band_set_edges(b,edges,round%2?0:2,1000000+round*3000000));
        echoy_band_job_t *j=echoy_band_take_job(b);assert(j);
        echoy_arrange_job_t before=j->arrangement;
        assert(echoy_band_prepare_job(j)&&echoy_band_prepare_pickup_job(j));
        assert(!memcmp(&before,&j->arrangement,sizeof before));
        for(int i=0;i<3;i++)if(j->banks[i])assert(j->banks[i]->pickup.count<=2);
        assert(echoy_band_finish_job(b,j,1000000+round*3000000));
        echoy_band_tick(b,3900000+round*3000000);
        assert(!memcmp(&root,&echoy_arrange_get_tile(b->arrange,0)->current.music,sizeof root));
    }
    struct bank *retained=b->voices[0].prepared;
    assert(echoy_band_action(b,0,ECHOY_ACT_PHRASE,2,22000000));
    echoy_band_job_t *failed=echoy_band_take_job(b);assert(failed);
    assert(echoy_band_prepare_job(failed));fail_pickup=true;
    assert(!echoy_band_prepare_pickup_job(failed));
    assert(!echoy_band_finish_job(b,failed,22100000));
    assert(b->voices[0].prepared==retained);
    echoy_band_delete(b);
}

static void published_transition(unsigned role, unsigned phrase, uint16_t next_seed)
{
    echoy_band_t *a=make(role,phrase,.8,0x50,false),*b=make(role,phrase,.8,0x50,true);
    echoy_control_view_t view;assert(echoy_band_read(a,0,&view));int64_t start=view.state.start_us,commit=0;
    echoy_device_pcm_t pa,pb;echoy_device_pcm_reset(&pa);echoy_device_pcm_reset(&pb);
    echoy_pickup_pcm_t pi;echoy_pickup_pcm_reset(&pi);
    size_t differences=0;
    unsigned head=0;while(!(next_seed&(1u<<head)))head++;
    for(size_t offset=0;offset<180000;offset+=240) {
        int64_t at=start+(int64_t)(offset*1000000/24000);
        if(offset==24000) {
            echoy_band_t *bands[]={a,b};
            for(unsigned k=0;k<2;k++) {
                assert(set_seed(bands[k],0,next_seed));
                echoy_band_job_t *j=echoy_band_take_job(bands[k]);assert(j);
                assert(echoy_band_prepare_job(j)&&echoy_band_voice_for_speaker_job(j)&&
                    echoy_band_calibrate_job(j,echoy_office_role_gains)&&echoy_band_prepare_pickup_job(j));
                assert(echoy_band_finish_job(bands[k],j,at));
            }
            const echoy_arrange_pending_t *pending=echoy_arrange_get_pending(a->arrange);assert(pending);
            commit=pending->at_us;assert(commit==echoy_arrange_get_pending(b->arrange)->at_us);
        }
        float raw[240],base[240],intro[240];uint8_t gate[240];int16_t x[240],y[240];
        echoy_band_render(a,at,raw,240);echoy_band_render_layers(b,at,base,intro,gate,240);
        assert(!memcmp(raw,base,sizeof raw));echoy_device_pcm_stats_t stats;
        assert(echoy_device_pcm_convert(&pa,raw,x,240,&stats)&&echoy_device_pcm_convert(&pb,base,y,240,&stats));
        assert(echoy_device_pcm_add_pickup(&pi,intro,gate,y,240));
        for(size_t k=0;k<240;k++)if(commit) {
            int64_t elapsed=at+(int64_t)(k*1000000/24000)-commit;
            if(elapsed>=0 && elapsed%2500000 >= head*156250)differences+=x[k]!=y[k];
        }
    }
    printf("published transition role=%u phrase=%u next=%x PCM differences=%zu\n",role,phrase,next_seed,differences);fflush(stdout);
    assert(!differences);echoy_band_delete(a);echoy_band_delete(b);
}

/* The v1.1.0 audition pool, shuffle history, 10 Hz gate and stale-role guard
 * must remain independent of the local phrase pickup. Compare actual samples. */
static void audition_compatibility(void)
{
    for(unsigned role=0;role<4;role++) {
        echoy_band_t *a=make(role,1,.8,0x50,false),*b=make(role,1,.8,0x50,true);
        for(unsigned tap=0;tap<120;tap++) {
            int64_t at=300000+tap*20000;
            assert(echoy_band_action(a,0,ECHOY_ACT_TAP,role+1,at));
            assert(echoy_band_action(b,0,ECHOY_ACT_TAP,role+1,at));
            struct voice *va=&a->voices[0],*vb=&b->voices[0];
            assert(va->preview_rng==vb->preview_rng && va->preview_next_at==vb->preview_next_at);
            assert(!memcmp(va->preview_order,vb->preview_order,sizeof va->preview_order));
            assert(!memcmp(va->preview_left,vb->preview_left,sizeof va->preview_left));
            float original[480],base[480],intro[480];uint8_t gate[480];
            echoy_band_render(a,at,original,480);echoy_band_render_layers(b,at,base,intro,gate,480);
            assert(!memcmp(original,base,sizeof original));
        }
        uint32_t rng=b->voices[0].preview_rng;int64_t next=b->voices[0].preview_next_at;
        assert(echoy_band_action(b,0,ECHOY_ACT_TAP,(role+1)%4+1,2800000));
        assert(b->voices[0].preview_rng==rng && b->voices[0].preview_next_at==next);
        echoy_band_delete(a);echoy_band_delete(b);
    }
    puts("PASS v1.1.0 audition samples, shuffle, 10 Hz input gate and stale-role rejection are unchanged with pickup enabled");
}
int main(void)
{
    audition_compatibility();
    for(unsigned r=0;r<4;r++)for(unsigned p=1;p<=3;p++) {
        published_transition(r,p,5);published_transition(r,p,0x8000);
    }
    if(getenv("PICKUP_TRANSITION_ONLY"))return 0;
    const uint16_t masks[]={0,5,2,0x50,0x500,0x8000};
    const double densities[]={0,.49,.5,1};
    unsigned cases=0;
    for(unsigned r=0;r<4;r++)for(unsigned p=1;p<=3;p++)
    for(unsigned d=0;d<4;d++)for(unsigned m=0;m<6;m++) {
        comparison(r,p,densities[d],masks[m]);cases++;
    }
    stale_and_chain();
    printf("PASS %u office-profile real scheduler/PCM pairs, two loops each; exact base and post-cutoff PCM, audible pickup, unchanged plans, stale role/seed/cancel rejection and repeated join/detach\n",cases);
}
