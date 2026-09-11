// SPDX-License-Identifier: Apache-2.0
#include "music_internal.h"

static void add(echoy_motif_t *out,int step,echoy_kind_t kind,double vel,double note,double dur,bool accent)
{ out->events[out->count++]=music_event(step,kind,vel,note,dur,accent); }

static void solo(const echoy_music_input_t *in, echoy_motif_t *out)
{
    const double a=in->activity;
    const int v=in->phrase, duplicate=in->duplicate, notes[]={62,69,65,72,67,74,69,65};
    if(v==1){
        if(in->role==ECHOY_PULSE){
            add(out,0,ECHOY_KICK,.88,38,.24,true); add(out,8,ECHOY_KICK,.75,38,.22,true);
            add(out,4,ECHOY_RIM,.66,50,.10,true); add(out,12,ECHOY_RIM,.73,50,.1,true);
            for(int s=2;s<16;s+=2) if(s%4!=0 || a>.65) add(out,s,ECHOY_HAT,.2+a*.22,84,.05,false);
            if(a>.7) add(out,15,ECHOY_RIM,.36,53,.08,false);
        }else if(in->role==ECHOY_VOICE){
            const int steps[]={0,3,7,10,14}; int count=(int)fmin(5,3+floor(a*4));
            for(int i=0;i<count;i++) add(out,steps[i],ECHOY_TONE,.52+(i%3==0?.15:0)+a*.12,notes[(i+(in->bar%4>=2?2:0))%8]+(duplicate%2?12:0),.32+a*.2,true);
            if(a>.82) add(out,15,ECHOY_TONE,.32,77,.13,false);
        }else{
            const int steps[]={1,6,11,15}; int count=(int)fmin(4,3+floor(a*5));
            for(int i=0;i<count;i++) add(out,steps[i],ECHOY_GRAIN,.32+a*.28,notes[(i+3+duplicate)%8]+12,.14+a*.2,i%2==0);
            if(a>.72){const int extra[]={3,9,13}; for(int i=0;i<3;i++)add(out,extra[i],ECHOY_GRAIN,.25,86,.13,false);}
        }
    }else if(in->role==ECHOY_PULSE){
        const int s2[]={0,3,7,10,14},s3[]={0,6,11}; const int *steps=v==2?s2:s3; int count=v==2?5:3;
        for(int i=0;i<count;i++) add(out,steps[i],i%3==0?ECHOY_KICK:ECHOY_RIM,i%3==0?.78:.59,i%3==0?38:50,i%3==0?.23:.09,true);
        if(a>.42 && v==2){const int extra[]={2,9,15}; for(int i=0;i<3;i++)add(out,extra[i],ECHOY_HAT,.27,84,.04,false);}
        if(a>.72 && v==3)add(out,14,ECHOY_RIM,.31,53,.08,false);
    }else if(in->role==ECHOY_VOICE){
        const int s2[]={1,4,6,9,13},s3[]={0,6,12}; const int *steps=v==2?s2:s3; int count=v==2?(int)fmin(5,4+floor(a*2)):3;
        for(int i=0;i<count;i++)add(out,steps[i],ECHOY_TONE,.53+(i==0?.12:0),notes[(i*(v==2?2:1)+(v==2?2:4))%8]+(duplicate%2?12:0),v==2?.16+a*.13:.45+a*.34,true);
    }else{
        const int s2[]={2,9,14},s3[]={0,5,10}; const int *steps=v==2?s2:s3;
        for(int i=0;i<3;i++)add(out,steps[i],ECHOY_GRAIN,v==2?.38:.42,notes[(i+(v==2?1:4)+duplicate)%8]+12,v==2?.09+a*.08:.32+a*.3,i%2==0);
        if(v==2 && a>.76)add(out,15,ECHOY_GRAIN,.22,86,.1,false);
    }
    size_t n=0; const double expression=a-role_initial(in->role);
    for(size_t i=0;i<out->count;i++){
        echoy_event_t e=out->events[i]; e.expression=expression; e.dur*=1+expression*.45;
        if(a<.26 && (!e.accent || i%2!=0))continue;
        out->events[n++]=e;
    }
    out->count=n; sort_events(out); n=0;
    for(size_t i=0;i<out->count;i++){
        echoy_event_t e=out->events[i]; bool seen=false;
        for(size_t j=0;j<n;j++)if(out->events[j].step==e.step && out->events[j].kind==e.kind)seen=true;
        if(!seen){e.vel=music_clamp(e.vel,.05,1);out->events[n++]=e;}
    }
    out->count=n;
}

bool echoy_music_compose(const echoy_music_input_t *input, echoy_motif_t *out)
{
    if(!out)return false;
    if(input && input->left==out)return false;
    memset(out,0,sizeof *out);
    if(!input || input->role<ECHOY_PULSE || input->role>ECHOY_HARMONY || input->phrase<1 || input->phrase>3 ||
       input->duplicate<0 || input->duplicate>2 || input->bar<0 || !isfinite(input->bpm) || input->bpm<=0 ||
       (input->left && input->left->count>ECHOY_MAX_EVENTS))return false;
    echoy_music_input_t in=*input; in.activity=music_clamp(in.activity,0,1);
    if(in.left)for(size_t i=0;i<in.left->count;i++){
        const echoy_event_t *e=&in.left->events[i];
        if(e->notes_count>ECHOY_MAX_PITCHES || e->harmony_count>ECHOY_MAX_PITCHES || e->kind<ECHOY_KICK || e->kind>ECHOY_CHORD)return false;
    }
    if(in.role==ECHOY_HARMONY)music_harmony(&in,out);
    else if(in.left && in.left->count)music_response(&in,out);
    else solo(&in,out);
    return true;
}
