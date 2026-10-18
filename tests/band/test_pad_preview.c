#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FRAMES 24000
static void settle(echoy_band_t *b,int64_t at)
{
    echoy_band_job_t *job=echoy_band_take_job(b);
    if(job){assert(echoy_band_prepare_job(job));assert(echoy_band_finish_job(b,job,at));}
    echoy_band_tick(b,at+50000);
}
static void recording_feedback_is_unchanged(void)
{
    echoy_band_t *a=echoy_band_create(),*b=echoy_band_create();assert(a&&b);
    const float gains[4]={1.6f,.78f,.0634f,.8f};
    assert(echoy_band_prepare_previews(a,true,gains));
    assert(echoy_band_prepare_previews(b,true,gains));
    assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,0));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,0));
    echoy_control_view_t v;assert(echoy_band_read(a,ECHOY_TILE_A,&v));
    int64_t at=v.state.capture_at_us+100000;
    assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_TAP,0,at));
    float *tap=calloc(FRAMES,sizeof(float)),*base=calloc(FRAMES,sizeof(float));assert(tap&&base);
    echoy_band_render(a,at,tap,FRAMES);echoy_band_render(b,at,base,FRAMES);
    echoy_audio_buffer_t *cue=NULL;assert(echoy_audio_render_cue(ECHOY_AUDIO_TAP,&cue)==ECHOY_AUDIO_OK);
    for(unsigned i=0;i<FRAMES;i++){
        float expected=i>=72 && i-72<cue->frames ? cue->samples[i-72]*.65f*.45f : 0;
        assert(fabsf(tap[i]-base[i]-expected)<1e-6f);
    }
    assert(echoy_band_read(a,ECHOY_TILE_A,&v) && v.state.pending_seed);
    free(tap);free(base);echoy_audio_free_buffer(cue);echoy_band_delete(a);echoy_band_delete(b);
    puts("PASS recording retains the exact original tap and timing cues");
}
static void preview_pause_and_failure(void)
{
    echoy_band_t *b=echoy_band_create();assert(b);
    const float bad[4]={1,1,NAN,1};
    assert(!echoy_band_prepare_previews(b,true,bad));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,0));
    assert(!echoy_band_prepare_previews(b,true,NULL)); /* Borrowed cue remains valid. */
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,1000));
    float pcm[240];echoy_band_render(b,3000,pcm,240);
    for(unsigned i=0;i<240;i++)assert(pcm[i]==0);
    echoy_band_delete(b);
    puts("PASS failed preparation preserves previews; scheduled previews fence replacement; Pause cancels");
}
static void random_preview_keeps_actual_phrase_unchanged(void)
{
    const float gains[4]={1.6f,.78f,.0634f,.8f};
    for(unsigned role=0;role<4;role++)for(unsigned phrase=1;phrase<=3;phrase++){
        echoy_band_t *b=echoy_band_create_configured(true,gains);assert(b);
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,role,0));
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PHRASE,phrase,0));
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_EXAMPLE,0,0));
        echoy_band_job_t *job=echoy_band_take_job(b);assert(job);
        assert(echoy_band_prepare_job(job));assert(echoy_band_voice_for_speaker_job(job));
        assert(echoy_band_calibrate_job(job,gains));assert(echoy_band_finish_job(b,job,0));
        echoy_band_tick(b,50000);
        echoy_control_view_t before,after;assert(echoy_band_read(b,ECHOY_TILE_A,&before));
        assert(before.state.phase==ECHOY_PREVIEW);
        float original[60000],trial[24000],again[60000];
        echoy_band_render(b,before.state.start_us,original,60000);
        echoy_band_tick(b,3000000);assert(echoy_band_read(b,ECHOY_TILE_A,&before));
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,3100000));
        echoy_band_render(b,3100000,trial,24000);
        assert(echoy_band_read(b,ECHOY_TILE_A,&after));
        assert(!memcmp(&before.plan,&after.plan,sizeof(before.plan)));
        assert(before.state.seed==after.state.seed && before.state.generation==after.state.generation);
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_AUDITION,0,5100000));
        echoy_band_tick(b,5100000);assert(echoy_band_read(b,ECHOY_TILE_A,&after));
        assert(after.state.phase==ECHOY_PREVIEW);
        echoy_band_render(b,after.state.start_us,again,60000);
        assert(!memcmp(original,again,sizeof(original)));
        echoy_band_delete(b);
    }
    puts("PASS random preview leaves actual phrase playback bit-identical across all12 expressions");
}
static void random_preview_keeps_recorded_leading_rest(void)
{
    echoy_band_t *b=echoy_band_create();assert(b);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_VOICE,0));settle(b,0);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,100000));
    echoy_control_view_t v;assert(echoy_band_read(b,ECHOY_TILE_A,&v));
    int64_t capture=v.state.capture_at_us,end=capture+2510000;
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,capture+1500000));
    echoy_band_tick(b,end);settle(b,end);
    assert(echoy_band_read(b,ECHOY_TILE_A,&v) && v.state.phase==ECHOY_PREVIEW);
    echoy_plan_t saved=v.plan;assert(saved.source[0].step>0);
    double first=16;for(size_t i=0;i<saved.count;i++)if(saved.events[i].step<first)first=saved.events[i].step;
    size_t offset=(size_t)ceil(first*.15625*ECHOY_AUDIO_SAMPLE_RATE);
    assert(offset>=24000 && offset+18000<=60000);
    float silence[24000],expected[18000],actual[24000];
    int64_t start=v.state.start_us;
    echoy_band_render(b,start,silence,24000);
    for(unsigned i=0;i<24000;i++)assert(silence[i]==0);
    echoy_band_render(b,start+(int64_t)(offset*1000000/ECHOY_AUDIO_SAMPLE_RATE),expected,18000);
    echoy_band_tick(b,start+3000000);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,start+3100000));
    echoy_band_render(b,start+3100000,actual,24000);
    double energy=0;for(unsigned i=0;i<24000;i++)energy+=actual[i]*actual[i];assert(energy>1e-5);
    assert(echoy_band_read(b,ECHOY_TILE_A,&v) && !memcmp(&saved,&v.plan,sizeof(saved)));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_AUDITION,0,start+5100000));
    echoy_band_tick(b,start+5100000);assert(echoy_band_read(b,ECHOY_TILE_A,&v));
    echoy_band_render(b,v.state.start_us,silence,24000);
    for(unsigned i=0;i<24000;i++)assert(silence[i]==0);
    echoy_band_render(b,v.state.start_us+(int64_t)(offset*1000000/ECHOY_AUDIO_SAMPLE_RATE),actual,18000);
    assert(!memcmp(actual,expected,sizeof(expected)));
    echoy_band_delete(b);
    puts("PASS random preview keeps the recorded leading silence, plan and original playback intact");
}
int main(void)
{
    random_preview_keeps_actual_phrase_unchanged();random_preview_keeps_recorded_leading_rest();
    recording_feedback_is_unchanged();preview_pause_and_failure();
    for(unsigned speaker=0;speaker<2;speaker++){
    float *sounds[4];
    for(unsigned role=0;role<4;role++){
        sounds[role]=calloc(FRAMES,sizeof(float));assert(sounds[role]);
        echoy_band_t *b=echoy_band_create();assert(b);
        const float gains[4]={1.6f,.78f,.0634f,.8f};
        if(speaker)assert(echoy_band_prepare_previews(b,true,gains));
        settle(b,0);
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,role,100000));settle(b,100000);
        echoy_control_view_t before,after;
        assert(echoy_band_read(b,ECHOY_TILE_A,&before));
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,300000));
        echoy_band_render(b,300000,sounds[role],FRAMES);
        assert(echoy_band_read(b,ECHOY_TILE_A,&after));
        assert(before.state.seed==after.state.seed && before.state.pending_seed==after.state.pending_seed);
        assert(before.state.generation==after.state.generation && before.state.phase==after.state.phase);
        assert(!after.state.continuous && !memcmp(&before.plan,&after.plan,sizeof(before.plan)));
        double energy=0;for(unsigned i=0;i<FRAMES;i++)energy+=sounds[role][i]*sounds[role][i];
        assert(energy>0.00001);
        for(unsigned prior=0;prior<role;prior++)assert(memcmp(sounds[prior],sounds[role],FRAMES*sizeof(float)));
        echoy_band_delete(b);
    }
    for(unsigned role=0;role<4;role++)free(sounds[role]);
    }
    puts("PASS reference and speaker-profile local pad previews use four different roles without recording or starting playback");
}
