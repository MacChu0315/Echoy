// SPDX-License-Identifier: Apache-2.0
#include "music_internal.h"
#include <stdlib.h>

bool music_pitched(const echoy_event_t *e)
{ return (e->kind==ECHOY_TONE || e->kind==ECHOY_GRAIN || e->kind==ECHOY_CHORD) && isfinite(e->note); }

bool music_analyze(const echoy_motif_t *m,double bpm,music_analysis_t *q)
{
    memset(q,0,sizeof *q);if(!m)return false;
    const double step_seconds=60/bpm/4;
    /* JS localeCompare for the six ASCII kind strings, stable within each kind. */
    const int rank[]={3,4,2,5,1,0};
    for(size_t i=0;i<m->count;i++){
        echoy_event_t e=m->events[i];
        if(!isfinite(e.step)||e.step<0||e.step>=16||!isfinite(e.dur)||e.dur<=0)continue;
        e.step=music_round(e.step);e.vel=music_clamp(e.vel,.05,1);
        analyzed_event_t n={e,music_clamp(e.dur/step_seconds,.25,e.kind==ECHOY_CHORD?16:6)};
        size_t j=q->count++;
        while(j && (q->events[j-1].event.step>e.step || (q->events[j-1].event.step==e.step && rank[q->events[j-1].event.kind]>rank[e.kind]))){q->events[j]=q->events[j-1];j--;}
        q->events[j]=n;
    }
    if(!q->count)return false;
    for(size_t i=0;i<q->count;i++){
        const analyzed_event_t *n=&q->events[i];const echoy_event_t *e=&n->event;
        const double strength=(.30+.70*e->vel)*(e->accent?1.12:1);
        /* JS may round 15.5 to index 16; it lies outside the 16 sampled cells. */
        if(e->step<16)q->onset[(int)e->step]+=strength;
        const double end=music_wrap(e->step+n->span,16);
        for(int s=0;s<16;s++){
            q->release[s]+=strength*fmax(0,1-music_distance(s,end)/1.6);
            double elapsed=music_wrap(s-e->step,16);
            if(elapsed<n->span)q->held[s]+=strength*(1-.25*elapsed/n->span);
        }
        q->mean_velocity+=e->vel;q->mean_span+=n->span;
    }
    for(int s=0;s<16;s++){
        q->onset[s]=1-exp(-q->onset[s]);q->release[s]=1-exp(-q->release[s]);q->held[s]=1-exp(-q->held[s]);q->space[s]=1-q->held[s];
    }
    q->first=(int)q->events[0].event.step;q->mean_velocity/=q->count;q->mean_span/=q->count;return true;
}

const analyzed_event_t *music_preceding(const music_analysis_t *q,int step,bool pitched)
{
    const analyzed_event_t *best=NULL;if(!q)return NULL;
    for(size_t i=0;i<q->count;i++){
        const analyzed_event_t *n=&q->events[i];if(pitched&&!music_pitched(&n->event))continue;
        if(!best || music_wrap(step-n->event.step,16)<music_wrap(step-best->event.step,16))best=n;
    }return best;
}

typedef struct { int slots[5]; size_t count; int delay; } shape_t;
static const shape_t shapes[3][3]={
    {{{0,4,8,12},4,2},{{0,3,7,10,14},5,1},{{0,6,11},3,3}},
    {{{0,4,8,12},4,2},{{1,4,6,9,13},5,3},{{0,6,12},3,4}},
    {{{1,6,11,15},4,1},{{2,9,14},3,2},{{0,5,10},3,4}}
};
static void resize_slots(const shape_t *shape,int *slots,size_t count)
{
    if(count<=shape->count){for(size_t i=0;i<count;i++)slots[i]=shape->slots[i*shape->count/count];return;}
    memcpy(slots,shape->slots,shape->count*sizeof *slots);size_t n=shape->count;
    while(n<count){
        sort_steps(slots,n);int gap=-1;size_t at=0;
        for(size_t i=0;i<n;i++){int span=wrap16(slots[(i+1)%n]-slots[i]);if(!span)span=16;if(span>gap){gap=span;at=i;}}
        slots[n++]=wrap16(slots[at]+(gap/2>1?gap/2:1));
    }sort_steps(slots,n);
}
static double profile(const music_analysis_t *q,const shape_t *shape,echoy_role_t role,int variant,int s)
{
    double answer=q->onset[wrap16(s-shape->delay)];
    if(role==ECHOY_PULSE)return answer*.95+q->release[s]*.55+q->space[s]*.20;
    if(role==ECHOY_VOICE)return answer*.70+q->release[s]*.65+q->space[s]*(variant==3?.72:.42);
    return answer*.45+q->release[s]*.90+(variant==3?q->held[s]*.30:q->space[s]*.45);
}
static void pick_steps(const music_analysis_t *q,const shape_t *shape,echoy_role_t role,int variant,int *steps,size_t count)
{
    int targets[7];bool used[16]={0};resize_slots(shape,targets,count);
    for(size_t i=0;i<count;i++){
        int target=wrap16(targets[i]+q->first+shape->delay),best=-1;double best_score=-INFINITY;
        for(int shift=-2;shift<=2;shift++){
            int s=wrap16(target+shift);if(used[s])continue;double crowd=0;
            for(size_t j=0;j<i;j++)if(music_distance(s,steps[j])<2)crowd+=.40;
            double score=profile(q,shape,role,variant,s)-abs(shift)*.19-crowd+(shift==0?.025:0);
            if(score>best_score){best_score=score;best=s;}
        }
        if(best<0)for(int j=0;j<16;j++)if(!used[wrap16(target+j)]){best=wrap16(target+j);break;}
        used[best]=true;steps[i]=best;
    }sort_steps(steps,count);
}
static double pitch(const music_analysis_t *q,int step,int delay,size_t index,int v,int duplicate,echoy_role_t role,int bar)
{
    static const int palette[]={62,64,65,67,69,70,72,74};
    const analyzed_event_t *cue=music_preceding(q,wrap16(step-delay),true);
    int octave=role==ECHOY_DUST?12:duplicate%2?12:0;
    if(cue && cue->event.harmony_count){
        double tones[ECHOY_MAX_PITCHES];size_t n=0;
        for(size_t i=0;i<cue->event.harmony_count;i++){
            double x=cue->event.harmony[i];if(!isfinite(x))continue;x=62+music_wrap(x-62,12);
            bool seen=false;for(size_t j=0;j<n;j++)if(tones[j]==x)seen=true;
            if(!seen){size_t j=n++;while(j&&tones[j-1]>x){tones[j]=tones[j-1];j--;}tones[j]=x;}
        }
        if(n)return tones[(index+(v==2?1:v==3?2:0))%n]+octave;
    }
    size_t degree=0;
    if(cue){
        double folded=62+music_wrap(cue->event.note-62,12);
        for(size_t i=0;i<8;i++)if(fabs(palette[i]-folded)<fabs(palette[degree]-folded))degree=i;
        degree=(degree+(v==2?2:v==3?0:1)+(index%3==2?1:0))%8;
    }else degree=(index*(v==2?2:1)+(v==3?4:0)+(bar%4>=2?2:0))%8;
    return palette[degree]+octave;
}

void music_response(const echoy_music_input_t *in,echoy_motif_t *out)
{
    music_analysis_t q;if(!music_analyze(in->left,in->bpm,&q))return;
    const echoy_role_t role=in->role;const int v=in->phrase;const double a=in->activity,expression=a-role_initial(role);
    const shape_t *shape=&shapes[role][v-1];
    size_t count=(size_t)music_clamp(shape->count+music_round(expression*4),1,7);int steps[7];
    pick_steps(&q,shape,role,v,steps,count);
    for(size_t i=0;i<count;i++){
        const int step=steps[i];const analyzed_event_t *cue=music_preceding(&q,wrap16(step-shape->delay),false);
        int gap=wrap16(steps[(i+1)%count]-step);if(!gap)gap=16;bool accent=i==0||i==count/2;
        double dynamics=(cue->event.vel-.55)*.10+(q.mean_velocity-.55)*.08+(q.mean_span-1.2)*.012;
        echoy_event_t e;
        if(role==ECHOY_PULSE){bool kick=i==0||(v!=2&&i==count/2);e=music_event(step,kick?ECHOY_KICK:ECHOY_RIM,music_clamp((kick?.78:.60)+dynamics,.30,.95),kick?38:50,kick?.22:.09,accent);}
        else{
            double note=pitch(&q,step,shape->delay,i,v,in->duplicate,role,in->bar),base,dur,vel;
            if(role==ECHOY_VOICE){
                base=v==2?.16+a*.13:v==3?.45+a*.34:.21+a*.14;
                dur=music_clamp(base+(cue->span-1)*.022+q.space[step]*.035,.12,fmin(1.05,gap*60/in->bpm/4+.20));
                vel=music_clamp(.54+(accent?.10:0)+a*.06+dynamics,.25,.88);
            }else{
                base=v==2?.09+a*.08:v==3?.32+a*.30:.14+a*.20;
                dur=music_clamp(base+(cue->span-1)*.018+q.release[step]*.025,.08,fmin(.90,gap*60/in->bpm/4+.12));
                vel=music_clamp((v==2?.38:v==3?.42:.32+a*.28)+dynamics,.18,.70);
            }
            e=music_event(step,role==ECHOY_VOICE?ECHOY_TONE:ECHOY_GRAIN,vel,note,dur,accent);
        }
        out->events[out->count++]=e;
    }
    if(role==ECHOY_PULSE && a>.52){
        int hats=v==3?(a>.8?1:0):(a>.82?3:1),free_steps[16];size_t n=0;
        for(int s=0;s<16;s++){
            bool used=false;for(size_t i=0;i<out->count;i++)if(out->events[i].step==s)used=true;
            if(!used){
                double score=q.space[s]+q.release[s]*.35+(s%2?.12:0);size_t j=n++;
                while(j){int t=free_steps[j-1];double prior=q.space[t]+q.release[t]*.35+(t%2?.12:0);if(prior>=score)break;free_steps[j]=t;j--;}
                free_steps[j]=s;
            }
        }
        for(int i=0;i<hats;i++)out->events[out->count++]=music_event(free_steps[i],ECHOY_HAT,.20+a*.14,84,.05,false);
    }
    sort_events(out);
    for(size_t i=0;i<out->count;i++){out->events[i].expression=expression;out->events[i].dur*=1+expression*.45;}
}
