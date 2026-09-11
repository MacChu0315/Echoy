// SPDX-License-Identifier: Apache-2.0
#include "echoy_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static echoy_audio_t *make(void) {
    void *p=malloc(echoy_audio_size()); assert(p);
    echoy_audio_t *a=echoy_audio_init(p,echoy_audio_size(),1000000);assert(a);return a;
}
static echoy_event_t note(int kind) {
    echoy_event_t e={.kind=(echoy_kind_t)kind,.vel=.8,.note=62,.dur=.22,.max_duration=2.45};
    if(kind==ECHOY_CHORD){e.has_notes=true;e.notes_count=3;e.notes[0]=62;e.notes[1]=65;e.notes[2]=69;}
    return e;
}
static void single_events(void) {
    double energy[6]={0};
    for(int k=0;k<6;k++) {
        echoy_audio_t *a=make();echoy_event_t e=note(k);float out[512];
        /* 1,005,354 us rounds to sample 257: one sample inside block two. */
        assert(echoy_audio_schedule(a,0,0,1005354,&e));
        for(int block=0;block<750;block++) {
            echoy_audio_render(a,out,256);
            for(int i=0;i<256;i++) {
                int sample=block*256+i;
                assert(isfinite(out[2*i]) && fabsf(out[2*i])<=1);
                assert(out[2*i]==out[2*i+1]);
                if(sample<257)assert(out[2*i]==0);
                if(sample>150000)assert(fabsf(out[2*i])<.00002f);
                energy[k]+=out[2*i]*out[2*i];
            }
        }
        assert(energy[k]>.001);
        assert(echoy_audio_stats(a).started==1);
        assert(echoy_audio_stats(a).active==0);
        free(a);
    }
    for(int i=0;i<6;i++)for(int j=i+1;j<6;j++)assert(fabs(energy[i]-energy[j])>.001);
}

static void boundary_and_clock(void) {
    echoy_audio_t *a=make();echoy_event_t e=note(ECHOY_KICK);float out[512];
    assert(echoy_audio_schedule(a,0,0,1005354,&e));
    echoy_audio_render(a,out,256);assert(echoy_audio_stats(a).started==0);
    echoy_audio_render(a,out,1);assert(echoy_audio_stats(a).started==0);
    echoy_audio_render(a,out,1);assert(echoy_audio_stats(a).started==1);
    assert(echoy_audio_schedule(a,0,0,1100000,&e));
    echoy_audio_control(a,0,1,0,.7,false);
    assert(echoy_audio_stats(a).pending==0&&echoy_audio_stats(a).cancelled==1);
    assert(!echoy_audio_schedule(a,0,0,1100000,&e));
    assert(echoy_audio_schedule(a,0,1,1000000,&e));
    assert(echoy_audio_stats(a).missed==1);
    assert(echoy_audio_schedule(a,0,1,1200000,&e));
    echoy_audio_skip_to(a,2000000);
    assert(echoy_audio_stats(a).missed==2&&echoy_audio_stats(a).pending==0);
    free(a);
}
static void control_and_independent_tails(void) {
    echoy_audio_t *a=make(),*b=make();echoy_event_t e=note(ECHOY_TONE);float x[512],y[512];
    assert(echoy_audio_schedule(a,1,0,1000000,&e));assert(echoy_audio_schedule(b,1,0,1000000,&e));
    for(int i=0;i<180;i++){
        if(i==20)echoy_audio_control(a,0,1,1,0,true);
        echoy_audio_render(a,x,256);echoy_audio_render(b,y,256);assert(memcmp(x,y,sizeof x)==0);
    }
    double tail=0;for(int i=0;i<100;i++){echoy_audio_render(a,x,256);for(int j=0;j<512;j++)tail+=x[j]*x[j];}
    assert(tail>1e-9);free(a);free(b);
    a=make();assert(echoy_audio_schedule(a,0,0,1000000,&e));echoy_audio_render(a,x,1);
    echoy_audio_control(a,0,0,1,.7,false);
    for(int i=0;i<130;i++)echoy_audio_render(a,x,256);
    /* Expression changed after onset must not extend this tone beyond .68s. */
    assert(echoy_audio_stats(a).active==0);
    assert(echoy_audio_schedule(a,0,0,1700000,&e));
    while(echoy_audio_stats(a).cursor<67200)echoy_audio_render(a,x,256);
    assert(echoy_audio_stats(a).active==1);free(a);
}
static echoy_bar_plan_t plan(void) {
    echoy_bar_plan_t p={.count=1};echoy_plan_entry_t *d=&p.devices[0];
    d->device=0;d->epoch_us=1000000;d->role=ECHOY_HARMONY;d->activity=.5;d->volume=.7;
    d->transport=ECHOY_PLAYING;d->motif_available=true;d->events.count=1;
    d->events.events[0]=note(ECHOY_CHORD);d->events.events[0].dur=2;
    d->motif=d->events;return p;
}
static void immutable_plan_and_empty_bar(void) {
    echoy_audio_t *a=make();echoy_bar_plan_t p=plan(),before=p;float x[512];
    assert(echoy_audio_plan(a,&p,1));assert(echoy_audio_plan(a,&p,1));
    assert(memcmp(&p,&before,sizeof p)==0);assert(echoy_audio_stats(a).pending==1);
    echoy_audio_render(a,x,256);assert(echoy_audio_stats(a).active==1);
    p.devices[0].bar=1;p.devices[0].events.count=p.devices[0].motif.count=0;
    assert(echoy_audio_plan(a,&p,1));
    for(int i=0;i<10;i++)echoy_audio_render(a,x,256);
    /* A rest in the next bar must not truncate a previous chord's tail. */
    assert(echoy_audio_stats(a).active==1);
    echoy_audio_output_fault(a,true);echoy_audio_render(a,x,256);
    for(int i=0;i<512;i++)assert(x[i]==0);
    echoy_audio_output_fault(a,false);echoy_audio_render(a,x,256);
    double energy=0;for(int i=0;i<512;i++)energy+=x[i]*x[i];assert(energy>0);
    free(a);
}
static void rejects_old_plan_and_rounds_clock_mapping(void) {
    echoy_audio_t *a=make();echoy_bar_plan_t p=plan();float x[512];
    p.devices[0].clock_version=2;p.devices[0].rate_ppm=1000;p.devices[0].events.events[0].step=1;
    assert(echoy_audio_plan(a,&p,1));
    p.devices[0].clock_version=1;
    assert(!echoy_audio_plan(a,&p,1));
    while(echoy_audio_stats(a).cursor<7492){unsigned n=7492-(unsigned)echoy_audio_stats(a).cursor;if(n>256)n=256;echoy_audio_render(a,x,n);}
    echoy_audio_render(a,x,1);assert(echoy_audio_stats(a).started==0);
    echoy_audio_render(a,x,1);assert(echoy_audio_stats(a).started==1);
    free(a);
}
static void reference_noise_and_gain_smoothing(void) {
    echoy_audio_t *a=make();echoy_event_t e=note(ECHOY_HAT);float x[512],y[512];
    assert(echoy_audio_schedule(a,0,0,1000000,&e));echoy_audio_render(a,x,256);
    /* Pinned Chromium single-hat reference: fixed seed AND .2s buffer offset. */
    const int positions[]={48,72,96,128,160,192,224};
    const float expected[]={.00072498288f,.00494161062f,-.00035494126f,.01638413966f,.00560586760f,.01524173748f,-.00353551772f};
    for(int i=0;i<7;i++)assert(fabsf(x[positions[i]*2]-expected[i])<.00002f);
    free(a);a=make();echoy_audio_t *b=make();e=note(ECHOY_TONE);
    assert(echoy_audio_schedule(a,0,0,1000000,&e));assert(echoy_audio_schedule(b,0,0,1000000,&e));
    echoy_audio_render(a,x,256);echoy_audio_render(b,y,256);
    echoy_audio_control(a,0,0,0,0,true);
    echoy_audio_render(a,x,1);echoy_audio_render(b,y,1);
    assert(x[0]!=0&&fabsf(x[0]-y[0])<.001f);
    for(int i=0;i<100;i++)echoy_audio_render(a,x,256);
    assert(echoy_audio_stats(a).active==0);
    free(a);free(b);
}
static void chord_normalization(void) {
    double energy[2]={0};float x[512];
    for(int j=0;j<2;j++){
        echoy_audio_t *a=make();echoy_event_t e=note(ECHOY_CHORD);e.notes_count=j?3:1;
        e.notes[0]=e.notes[1]=e.notes[2]=62;
        assert(echoy_audio_schedule(a,0,0,1000000,&e));
        for(int block=0;block<100;block++){echoy_audio_render(a,x,256);for(int i=0;i<512;i++)energy[j]+=x[i]*x[i];}
        free(a);
    }
    assert(energy[1]/energy[0]>2.98&&energy[1]/energy[0]<3.02);
}
static void capacity_and_noise(void) {
    echoy_audio_t *a=make(),*b=make();echoy_event_t e=note(ECHOY_GRAIN);float x[512],y[512];
    assert(echoy_audio_schedule(a,0,0,1000000,&e));assert(echoy_audio_schedule(b,0,0,1000000,&e));
    for(int i=0;i<300;i++){echoy_audio_render(a,x,256);echoy_audio_render(b,y,256);assert(memcmp(x,y,sizeof x)==0);}
    free(a);free(b);a=make();e=note(ECHOY_CHORD);e.dur=2.45;
    for(int i=0;i<64;i++)assert(echoy_audio_schedule(a,i%3,0,1000000,&e));
    echoy_audio_render(a,x,256);assert(echoy_audio_stats(a).peak_voices==64);
    assert(echoy_audio_stats(a).started==64&&!echoy_audio_stats(a).capacity_errors);
    for(int i=0;i<500;i++)echoy_audio_render(a,x,256);
    assert(echoy_audio_stats(a).active==0);free(a);
}
int main(void) {
    single_events();boundary_and_clock();control_and_independent_tails();
    immutable_plan_and_empty_bar();rejects_old_plan_and_rounds_clock_mapping();reference_noise_and_gain_smoothing();chord_normalization();capacity_and_noise();
    puts("PASS DSP events, sample scheduling, expression, tail isolation, immutable plans, 64 voices");return 0;
}
