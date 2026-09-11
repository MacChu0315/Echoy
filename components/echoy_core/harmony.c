// SPDX-License-Identifier: Apache-2.0
#include "music_internal.h"
#include <stdlib.h>

typedef struct { int notes[3]; } chord_t;
typedef struct { int chord; int notes[3]; } voicing_t;
#include "harmony_voicings.inc"
typedef struct { int step; bool closing; } plan_entry_t;
typedef struct { plan_entry_t entries[6]; size_t count; } plan_t;

static void pitch_evidence(const music_analysis_t *q,double weights[12])
{
    memset(weights,0,12*sizeof *weights);if(!q)return;
    for(size_t i=0;i<q->count;i++){
        const analyzed_event_t *n=&q->events[i];const echoy_event_t *e=&n->event;
        if(e->kind!=ECHOY_TONE && e->kind!=ECHOY_GRAIN && e->kind!=ECHOY_CHORD)continue;
        const double *pitches=e->has_harmony?e->harmony:e->has_notes?e->notes:&e->note;
        size_t count=e->has_harmony?e->harmony_count:e->has_notes?e->notes_count:1,valid=0;
        for(size_t j=0;j<count;j++)if(isfinite(pitches[j]))valid++;
        if(!valid)continue;
        double weight=(.3+.7*e->vel)*(e->accent?1.15:1)*sqrt(n->span)/valid;
        for(size_t j=0;j<count;j++)if(isfinite(pitches[j])){
            double pc=music_wrap(pitches[j],12);
            /* Non-integer JS array properties do not enter its 12-cell reduce. */
            if(pc==floor(pc))weights[(int)pc]+=weight;
        }
    }
}
static double fit_chord(int chord,const double weights[12])
{
    double total=0,score=0;for(int i=0;i<12;i++)total+=weights[i];if(!total)return 0;
    for(int n=0;n<12;n++)if(weights[n]){
        bool match=false,near=false;
        for(int j=0;j<3;j++){
            int p=chords[chord].notes[j]%12;if(p==n)match=true;
            if(fmin(music_wrap(n-p,12),music_wrap(p-n,12))==1)near=true;
        }
        if(match)score+=weights[n];else if(near)score-=weights[n]*.16;
    }return score/total;
}
static int choose(const music_analysis_t *q)
{
    double weights[12];pitch_evidence(q,weights);int best=0;double score=fit_chord(0,weights);
    for(int i=1;i<6;i++){double rank=fit_chord(i,weights);if(rank>score){best=i;score=rank;}}
    return best;
}
static void voicing(int chord,int duplicate,int tones[3])
{
    memcpy(tones,chords[chord].notes,3*sizeof *tones);
    if(duplicate%2){tones[0]+=12;sort_steps(tones,3);}
}
static double center(const music_analysis_t *q)
{
    if(!q)return .45;
    double sum=0,vel=0;for(size_t i=0;i<q->count;i++){sum+=q->events[i].event.step*q->events[i].event.vel;vel+=q->events[i].event.vel;}
    return sum/vel/15;
}
static size_t broken_positions(const music_analysis_t *q,double a,int *steps)
{
    static const int sparse[]={0,6,12},busy[]={0,2,5,7,10,12,14},normal[]={0,3,6,10,13};
    const int *targets=a<.25?sparse:a>.80?busy:normal;size_t count=a<.25?3:a>.80?7:5;
    bool used[16]={0};const int deltas[]={0,-1,1};
    for(size_t i=0;i<count;i++){
        int target=targets[i],best=target;
        if(q){
            target=wrap16(target+q->first+1);best=target;double score=-INFINITY;
            for(int j=0;j<3;j++){
                int delta=deltas[j],s=wrap16(target+delta);if(used[s])continue;
                double v=q->release[s]*.65+q->space[s]*.45+q->onset[wrap16(s-1)]*.3-abs(delta)*.16;
                if(v>score){best=s;score=v;}
            }used[best]=true;
        }steps[i]=best;
    }sort_steps(steps,count);return count;
}
static echoy_event_t chord_event(int step,const int *notes,size_t count,const int tones[3],int chord,echoy_articulation_t art,double vel,double dur,double deadline,bool accent,double expression)
{
    echoy_event_t e=music_event(step,ECHOY_CHORD,vel,notes[count-1],dur,accent);
    e.notes_count=count;e.harmony_count=3;e.has_notes=e.has_harmony=true;
    for(size_t i=0;i<count;i++)e.notes[i]=notes[i];
    for(size_t i=0;i<3;i++)e.harmony[i]=tones[i];
    e.chord=chord;e.articulation=art;e.max_duration=deadline;e.expression=expression;return e;
}
static void broken(const music_analysis_t *q,double a,const int tones[3],int selected,double step_time,echoy_motif_t *out)
{
    int slots[7];size_t count=broken_positions(q,a,slots);const int order[]={0,1,2,1,0,2,1};
    double expression=a-.5,mean=q?q->mean_velocity:.55,span=q?q->mean_span:1.7,c=center(q);
    for(size_t i=0;i<count;i++){
        int step=slots[i],next=i+1<count?slots[i+1]:16;double deadline=fmax(.08,(next-step)*step_time-.05);
        double grace=.20+.18*a+(span-1)*.014+c*.025;
        /* Harmony's clamp is max(lo,min(hi,x)), unlike Response's clamp. */
        double dur=fmax(.07,fmin(deadline,grace*(1+expression*.45)));
        int note=tones[order[i%7]];double vel=music_clamp(.57+(mean-.55)*.10+(c-.45)*.022+(i==0?.035:0),.35,.78);
        out->events[out->count++]=chord_event(step,&note,1,tones,selected,ECHOY_ART_BROKEN,vel,dur,deadline,i==0||i==count/2,expression);
    }
}
static plan_t short_plan(const music_analysis_t *q,double a)
{
    static const int sparse[]={1,7,12},busy[]={1,4,7,10,13},normal[]={1,5,9,13};
    const int *cells=a<.25?sparse:a>.78?busy:normal;size_t count=a<.25?3:a>.78?5:4;int steps[5];size_t n=0;
    for(size_t i=0;i<count;i++){
        int target=wrap16(cells[i]+(q?q->first:0)),best=-1;double score=-INFINITY;
        for(int delta=-2;delta<=2;delta++){
            int s=wrap16(target+delta);bool close=false;for(size_t j=0;j<n;j++)if(music_distance(steps[j],s)<2)close=true;
            if(close)continue;
            double musical=q?q->release[s]*.85+q->space[s]*.5+q->onset[wrap16(s-1)]*.55-q->onset[s]*.26:0;
            double rank=musical-abs(delta)*.16;if(rank>score){best=s;score=rank;}
        }if(best>=0)steps[n++]=best;
    }
    sort_steps(steps,n);plan_t plan={0};for(size_t i=0;i<n;i++)plan.entries[plan.count++]=(plan_entry_t){steps[i],true};return plan;
}
static plan_t answer_plan(const music_analysis_t *q,double a)
{
    const int targets[]={a<.30?10:4,11};int starts[2];size_t count=a<.30?1:2,n=0;bool long_phrase=a>.74;
    for(size_t i=0;i<count;i++){
        int best=-1;double score=-INFINITY;
        for(int s=1;s<=(long_phrase?12:13);s++){
            bool close=false;for(size_t j=0;j<n;j++)if(abs(starts[j]-s)<6)close=true;if(close)continue;
            double space=q?(q->space[s]+q->space[s+1]+q->space[s+2])/3:0;
            double musical=q?q->release[s]*1.10+space*.78+q->onset[wrap16(s-1)]*.18-q->onset[s]*.5:0;
            double rank=musical-abs(s-targets[i])*.12;if(rank>score){best=s;score=rank;}
        }if(best>=0)starts[n++]=best;
    }
    sort_steps(starts,n);plan_t plan={0};
    for(size_t i=0;i<n;i++){
        plan.entries[plan.count++]=(plan_entry_t){starts[i],false};
        if(long_phrase)plan.entries[plan.count++]=(plan_entry_t){starts[i]+1,false};
        plan.entries[plan.count++]=(plan_entry_t){starts[i]+(long_phrase?3:2),true};
    }return plan;
}
static double voicing_score(const voicing_t *c,const double fits[6],double target,const int reference[3],const int anchor[3],const voicing_t *previous)
{
    int movement=0;for(int j=0;j<3;j++)movement+=abs(c->notes[j]-reference[j]);
    return fits[c->chord]*8-fabs(c->notes[2]-target)*.90-movement*.11-abs(c->notes[0]-anchor[0])*.035+(previous&&c->chord==previous->chord?.35:0);
}
static void short_motion(const music_analysis_t *q,const plan_t *plan,int selected,int duplicate,voicing_t out[6])
{
    int reg=duplicate%2?3:0,anchor[3];voicing(selected,duplicate,anchor);int base_top=chords[selected].notes[2]+reg;
    const int c3[]={0,5,0},c4[]={0,4,7,0},c5[]={0,4,7,4,0},fallback[]={0,4,0};
    const int *contour=plan->count==3?c3:plan->count==4?c4:plan->count==5?c5:fallback;
    double global[12];pitch_evidence(q,global);
    for(size_t i=0;i<plan->count;i++){
        int step=plan->entries[i].step;const analyzed_event_t *cue=music_preceding(q,step,true);
        music_analysis_t local={0};
        if(cue)for(size_t j=0;j<q->count;j++){
            const analyzed_event_t *e=&q->events[j];
            if(music_pitched(&e->event)&&(e==cue||music_wrap(step-e->event.step,16)<e->span))local.events[local.count++]=*e;
        }
        double weights[12],fits[6],best_fit=-INFINITY;pitch_evidence(local.count?&local:NULL,weights);
        for(int c=0;c<6;c++){fits[c]=cue?.78*fit_chord(c,weights)+.22*fit_chord(c,global):c==selected?1:-1;if(fits[c]>best_fit)best_fit=fits[c];}
        double folded=cue?cue->event.note+12*music_round((base_top-cue->event.note)/12):base_top;
        double target=base_top*.35+folded*.65+contour[i]*.65;
        const voicing_t *previous=i?&out[i-1]:NULL,*first=i?&out[0]:NULL;
        const int *reference=previous?previous->notes:anchor;const voicing_t *best=NULL;double score=-INFINITY;
        /* Iteration order breaks the remaining exact ties, as JS stable sort does. */
        for(int pass=0;pass<2&&!best;pass++)for(size_t j=0;j<sizeof short_voicings/sizeof short_voicings[0];j++){
            const voicing_t *c=&short_voicings[j];int top=c->notes[2];
            if(top<60+reg||top>77+reg||(!pass&&fits[c->chord]<best_fit-.18))continue;
            if(previous&&(top==previous->notes[2]||abs(top-previous->notes[2])>7))continue;
            if(i==plan->count-1&&first&&abs(top-first->notes[2])>7)continue;
            double rank=voicing_score(c,fits,target,reference,anchor,previous);
            if(!best||rank>score||(rank==score&&(top<best->notes[2]||(top==best->notes[2]&&c->notes[0]<best->notes[0])))){best=c;score=rank;}
        }
        out[i]=*best;
    }
}
void music_harmony(const echoy_music_input_t *in,echoy_motif_t *out)
{
    music_analysis_t analysis;const music_analysis_t *q=music_analyze(in->left,in->bpm,&analysis)?&analysis:NULL;
    double a=in->activity,step_time=60/music_clamp(in->bpm,40,200)/4;int selected=choose(q),tones[3];voicing(selected,in->duplicate,tones);
    if(in->phrase==2){broken(q,a,tones,selected,step_time,out);return;}
    plan_t plan=in->phrase==1?short_plan(q,a):answer_plan(q,a);voicing_t motion[6];
    if(in->phrase==1)short_motion(q,&plan,selected,in->duplicate,motion);
    double expression=a-.5,mean=q?q->mean_velocity:.55,span=q?q->mean_span:1.7,c=center(q);
    for(size_t i=0;i<plan.count;i++){
        int step=plan.entries[i].step,next=i+1<plan.count?plan.entries[i+1].step:16;bool closing=plan.entries[i].closing;
        double deadline=fmax(.045,(next-step)*step_time-.035);
        const analyzed_event_t *cue=music_preceding(q,step,false);double cue_span=cue?cue->span:span,space=q?q->space[step]:.7;
        double base=in->phrase==1?step_time*(.78+.35*a)+fmin(cue_span,4)*.012+space*.014:
            closing?step_time*(1.04+.34*a)+fmin(cue_span,4)*.015+space*.015:
            step_time*(.55+.25*a)+fmin(cue_span,4)*.007;
        double dur=fmax(.035,fmin(fmin(deadline,in->phrase==1?.30:closing?.36:.19),base+(c-.45)*.018));
        const int *event_tones=in->phrase==1?motion[i].notes:tones;int event_chord=in->phrase==1?motion[i].chord:selected;
        int note=event_tones[(i+1)%3];const int *notes=closing?event_tones:&note;
        double vel=music_clamp((closing?.56:.47)+(mean-.55)*.10+((cue?cue->event.vel:mean)-.55)*.065+(c-.45)*.022,.33,.75);
        out->events[out->count++]=chord_event(step,notes,closing?3:1,event_tones,event_chord,in->phrase==1?ECHOY_ART_SHORT:ECHOY_ART_ANSWER,vel,dur,deadline,closing,expression);
    }
}
