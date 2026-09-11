// SPDX-License-Identifier: Apache-2.0
#include "echoy_audio.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
#define NOISE_LENGTH 96000
#define DELAY_LENGTH 15000
#define TABLE_LENGTH 4096
#define FLOOR_GAIN .00001f

typedef struct { float b0,b1,b2,a1,a2,z1,z2; } filter_t;
typedef struct {
    bool used; int device; echoy_event_t event; int64_t target;
} pending_t;
typedef struct {
    bool used,wet,chord; int device; echoy_kind_t kind;
    unsigned age,end,attack,hold,total,stop;
    float gain,peak,sustain,attack_mul,release_mul;
    float phase[3],frequency[3],level[3]; unsigned pitches,triangle_bank[3];
    float frequency_mul,filter_hz,filter_mul,filter_end,filter_q,warmth;
    unsigned sweep_end,noise_offset;
    filter_t filter;
} voice_t;
typedef struct {
    uint64_t version,boot,session; int64_t bar; bool seen,muted;
    float expression,volume,gain,color_db;
    float delay[DELAY_LENGTH]; unsigned delay_pos;
    filter_t damp,color;
} channel_t;
struct echoy_audio {
    int64_t epoch; echoy_audio_stats_t stats;
    voice_t voices[ECHOY_AUDIO_VOICES];pending_t pending[ECHOY_AUDIO_PENDING];
    channel_t channels[ECHOY_MAX_DEVICES];float noise[NOISE_LENGTH],sine[TABLE_LENGTH+1];
    float triangle[5][TABLE_LENGTH+1];
    float master,target_master;
    unsigned plan_mask;
};
static float clampf(float x,float lo,float hi){return fmaxf(lo,fminf(hi,x));}
static bool valid_device(int d){return d>=0&&d<ECHOY_MAX_DEVICES;}
static float midi(double n){return (float)(440*pow(2,(n-69)/12));}
/* Web Audio low/highpass Q is dB, bandpass Q is linear. */
static void filter_set(filter_t *f,int type,float hz,float q)
{
    float w=(float)(2*PI/ECHOY_AUDIO_RATE)*clampf(hz,10,22000),c=cosf(w),s=sinf(w);
    float alpha=s/(2*(type==2?q:powf(10,q/20))),a0=1+alpha;
    float b=type==1?(1+c)/2:(1-c)/2;
    f->b0=(type==2?alpha:b)/a0;f->b1=(type==2?0:type==1?-2*b:2*b)/a0;
    f->b2=(type==2?-alpha:b)/a0;f->a1=-2*c/a0;f->a2=(1-alpha)/a0;
}
static void shelf_set(filter_t *f,float db)
{
    float a=powf(10,db/40),w=(float)(2*PI*1000/ECHOY_AUDIO_RATE);
    float c=cosf(w),beta=sinf(w)*sqrtf(2*a),a0=(a+1)-(a-1)*c+beta;
    f->b0=a*((a+1)+(a-1)*c+beta)/a0;f->b1=-2*a*((a-1)+(a+1)*c)/a0;
    f->b2=a*((a+1)+(a-1)*c-beta)/a0;
    f->a1=2*((a-1)-(a+1)*c)/a0;f->a2=((a+1)-(a-1)*c-beta)/a0;
}
static float filtered(filter_t *f,float x)
{
    float y=f->b0*x+f->z1;f->z1=f->b1*x-f->a1*y+f->z2;f->z2=f->b2*x-f->a2*y;
    /* Avoid denormal stalls in the long, otherwise inaudible feedback tail. */
    if(fabsf(f->z1)<1e-20f)f->z1=0;
    if(fabsf(f->z2)<1e-20f)f->z2=0;
    return y;
}
static float sine(const echoy_audio_t *a,float p)
{
    p-=floorf(p);float f=p*TABLE_LENGTH;unsigned i=(unsigned)f;
    return a->sine[i]+(a->sine[i+1]-a->sine[i])*(f-i);
}
static float triangle(const echoy_audio_t *a,float p,unsigned bank)
{
    p-=floorf(p);float f=p*TABLE_LENGTH;unsigned i=(unsigned)f;
    return a->triangle[bank][i]+(a->triangle[bank][i+1]-a->triangle[bank][i])*(f-i);
}
size_t echoy_audio_size(void){return sizeof(echoy_audio_t);}
echoy_audio_t *echoy_audio_init(void *storage,size_t size,int64_t epoch)
{
    if(!storage||size<sizeof(echoy_audio_t)||epoch<0)return NULL;
    echoy_audio_t *a=storage;memset(a,0,sizeof *a);a->epoch=epoch;a->master=a->target_master=.55f;
    uint32_t seed=1187;
    for(unsigned i=0;i<NOISE_LENGTH;i++){seed=(uint32_t)((uint64_t)seed*16807%2147483647);a->noise[i]=(float)((double)seed/2147483647*2-1);}
    for(unsigned i=0;i<=TABLE_LENGTH;i++)a->sine[i]=sinf((float)(i*2*PI/TABLE_LENGTH));
    for(unsigned bank=0;bank<5;bank++)for(unsigned i=0;i<=TABLE_LENGTH;i++){
        float x=0;unsigned max=(1u<<(bank+1))-1;
        for(unsigned n=1;n<=max;n+=2)x+=sine(a,(float)i/TABLE_LENGTH*n)*(n%4==1?1.f:-1.f)/(n*n);
        a->triangle[bank][i]=x*(float)(8/(PI*PI));
    }
    for(unsigned i=0;i<3;i++){channel_t *c=&a->channels[i];c->volume=.7f;c->gain=1;c->bar=-1;filter_set(&c->damp,0,2900,1);shelf_set(&c->color,0);}
    return a;
}
static bool target_sample(const echoy_audio_t *a,long double us,int64_t *sample)
{
    long double n=roundl((us-a->epoch)*ECHOY_AUDIO_RATE/1000000);
    if(!isfinite(n)||n<INT64_MIN||n>INT64_MAX)return false;
    *sample=(int64_t)n;return true;
}
static bool valid_event(const echoy_event_t *e)
{
    if(!e||e->kind<ECHOY_KICK||e->kind>ECHOY_CHORD||!isfinite(e->dur)||e->dur<0||e->dur>8||
       !isfinite(e->vel)||e->vel<0||e->vel>1||!isfinite(e->expression)||fabs(e->expression)>1||
       !isfinite(e->max_duration)||e->max_duration<0||e->max_duration>8||e->notes_count>3)return false;
    if(e->kind>=ECHOY_TONE && (!isfinite(e->note)||e->note<0||e->note>127)&&!(e->kind==ECHOY_CHORD&&e->has_notes))return false;
    if(e->kind==ECHOY_CHORD&&e->has_notes){if(!e->notes_count)return false;for(size_t i=0;i<e->notes_count;i++)if(!isfinite(e->notes[i])||e->notes[i]<0||e->notes[i]>127)return false;}
    return true;
}
static bool enqueue(echoy_audio_t *a,int device,int64_t sample,const echoy_event_t *e)
{
    if(sample<a->stats.cursor){a->stats.missed++;return true;}
    for(unsigned i=0;i<ECHOY_AUDIO_PENDING;i++)if(!a->pending[i].used){a->pending[i]=(pending_t){true,device,*e,sample};a->stats.pending++;return true;}
    a->stats.capacity_errors++;return false;
}
bool echoy_audio_schedule(echoy_audio_t *a,int d,uint64_t version,int64_t us,const echoy_event_t *e)
{
    int64_t sample;
    if(!a||!valid_device(d)||!valid_event(e)||version!=a->channels[d].version||!target_sample(a,us,&sample))return false;
    return enqueue(a,d,sample,e);
}
static void cancel(echoy_audio_t *a,int d)
{
    for(unsigned i=0;i<ECHOY_AUDIO_PENDING;i++)if(a->pending[i].used&&a->pending[i].device==d){a->pending[i].used=false;a->stats.cancelled++;a->stats.pending--;}
    for(unsigned i=0;i<ECHOY_AUDIO_VOICES;i++)if(a->voices[i].used&&a->voices[i].device==d){voice_t *v=&a->voices[i];unsigned end=v->age+1440;if(end<v->stop)v->stop=end;}
}
void echoy_audio_control(echoy_audio_t *a,int d,uint64_t version,double expr,double volume,bool mute)
{
    if(!a||!valid_device(d)||!isfinite(expr)||!isfinite(volume))return;
    channel_t *c=&a->channels[d];
    if(version<c->version)return;
    if(version!=c->version||(mute&&!c->muted)){cancel(a,d);c->seen=false;}
    c->version=version;c->expression=clampf((float)expr,-1,1);c->volume=clampf((float)volume,0,1);c->muted=mute;
}
void echoy_audio_master(echoy_audio_t *a,double volume){if(a&&isfinite(volume))a->target_master=clampf((float)volume,0,.85f);}
bool echoy_audio_plan(echoy_audio_t *a,const echoy_bar_plan_t *plan,unsigned mask)
{
    static const double initial[]={.58,.56,.48,.50};
    if(!a||!plan||plan->count>3||mask>7)return false;
    /* Validate the complete message before changing clocks or adding notes. */
    unsigned seen=0;
    for(size_t i=0;i<plan->count;i++){
        const echoy_plan_entry_t *p=&plan->devices[i];
        if(!valid_device(p->device)||(seen&(1u<<p->device))||p->role>ECHOY_HARMONY||p->role<ECHOY_PULSE||p->bar<0||p->bar>INT64_MAX/ECHOY_BAR_US-1||
           p->events.count>ECHOY_MAX_EVENTS||p->rate_ppm< -10000||p->rate_ppm>10000||!isfinite(p->activity)||!isfinite(p->volume))return false;
        if((mask&(1u<<p->device))&&a->channels[p->device].boot==p->boot_id&&p->clock_version<a->channels[p->device].version)return false;
        seen|=1u<<p->device;
        for(size_t j=0;j<p->events.count;j++)if(!valid_event(&p->events.events[j])||!isfinite(p->events.events[j].step)||p->events.events[j].step<0||p->events.events[j].step>=16)return false;
    }
    /* A plan is a complete snapshot. Removed devices must not keep their old
     * sources or future notes just because there is no entry to visit. */
    unsigned present=seen&mask,removed=a->plan_mask&~present;
    for(int d=0;d<ECHOY_MAX_DEVICES;d++)if(removed&(1u<<d)){
        channel_t *c=&a->channels[d];cancel(a,d);c->muted=true;c->seen=false;
    }
    a->plan_mask=present;
    bool ok=true;
    for(size_t i=0;i<plan->count;i++){
        const echoy_plan_entry_t *p=&plan->devices[i];int d=p->device;if(!(mask&(1u<<d)))continue;
        channel_t *c=&a->channels[d];
        if(c->boot!=p->boot_id||c->session!=p->session_id){cancel(a,d);c->seen=false;c->boot=p->boot_id;c->session=p->session_id;c->version=p->clock_version;}
        echoy_audio_control(a,d,p->clock_version,p->activity-initial[p->role],p->volume,p->transport!=ECHOY_PLAYING||!p->motif_available||(p->motif.count&&!p->events.count));
        if(c->muted||(c->seen&&p->bar<=c->bar))continue;
        if(ECHOY_AUDIO_PENDING-a->stats.pending<p->events.count){a->stats.capacity_errors++;ok=false;continue;}
        c->seen=true;c->bar=p->bar;
        for(size_t j=0;j<p->events.count;j++){
            const echoy_event_t *e=&p->events.events[j];int64_t sample;
            long double us=p->epoch_us+((long double)p->bar*ECHOY_BAR_US+e->step*(ECHOY_BAR_US/16.0L))/(1+p->rate_ppm/1000000.0L);
            if(!target_sample(a,us,&sample)||!enqueue(a,d,sample,e))ok=false;
        }
    }
    return ok;
}
static unsigned samples(float sec){return (unsigned)lroundf(sec*ECHOY_AUDIO_RATE);}
static void start(echoy_audio_t *a,const pending_t *p)
{
    voice_t *v=NULL;
    for(unsigned i=0;i<ECHOY_AUDIO_VOICES;i++){
        voice_t *candidate=&a->voices[i];
        /* Sources ending on this sample no longer consume a slot, even though
         * this sample's mixing pass has not visited them yet. */
        if(candidate->used&&candidate->age>=candidate->stop){candidate->used=false;a->stats.active--;}
        if(!candidate->used){v=candidate;break;}
    }
    if(!v){a->stats.capacity_errors++;return;}
    memset(v,0,sizeof *v);v->used=true;v->device=p->device;v->kind=p->event.kind;
    const echoy_event_t *e=&p->event;float expr=a->channels[p->device].expression,stretch=1+expr*.45f;
    float dur=(float)(e->dur?e->dur:.2)*stretch/(float)(1+e->expression*.45);
    float attack=.014f,release=dur+.16f,end=dur+.23f;v->peak=(float)e->vel*.3f;
    v->pitches=1;v->frequency[0]=midi(isfinite(e->note)?e->note:62);v->level[0]=1;
    switch(e->kind){
    case ECHOY_KICK:
        attack=.004f;release=.28f*stretch;end=.34f*stretch;v->peak=(float)e->vel*.72f;v->frequency[0]=128;
        v->sweep_end=samples(.095f*stretch);v->frequency_mul=powf(47.f/128,1.f/v->sweep_end);break;
    case ECHOY_RIM:
        attack=.001f;release=.095f*stretch;end=.13f*stretch;v->peak=(float)e->vel*.34f;
        v->pitches=3;v->frequency[0]=238;v->frequency[1]=517;v->frequency[2]=1034;v->level[0]=.7f;v->level[1]=.23f;v->level[2]=.1f;break;
    case ECHOY_HAT:
        attack=.001f;release=.055f*stretch;end=.08f*stretch;v->peak=(float)e->vel*.16f;v->noise_offset=9600;
        filter_set(&v->filter,1,6500*(1+expr*.22f),1);break;
    case ECHOY_TONE:
        v->wet=true;attack=.012f;release=dur+.38f;end=dur+.46f;v->peak=(float)e->vel*.29f;
        v->pitches=3;v->frequency[1]=v->frequency[0]*2;v->frequency[2]=v->frequency[0]*3;v->level[1]=.22f;v->level[2]=.07f;
        v->filter_hz=2400;v->filter_end=700;v->filter_q=.45f;v->sweep_end=24000;break;
    case ECHOY_GRAIN:
        v->wet=true;v->noise_offset=31200;filter_set(&v->filter,2,v->frequency[0]*1.7f,5.5f);break;
    case ECHOY_CHORD:{
        bool broken=e->articulation==ECHOY_ART_BROKEN,short_note=e->articulation==ECHOY_ART_SHORT,answer=e->articulation==ECHOY_ART_ANSWER;
        v->chord=v->wet=true;v->pitches=e->has_notes?(unsigned)e->notes_count:1;
        bool pickup=answer&&v->pitches==1;
        float total=fmaxf(short_note||answer?.025f:.07f,fminf(dur,(float)(e->max_duration?e->max_duration:2.45)));
        attack=fminf(total*.25f,broken?.015f:short_note?.008f:answer?(pickup?.010f:.018f):.075f);
        release=fminf(total*(short_note?.42f:answer?.38f:.32f),broken?.12f:short_note?.075f:answer?.10f:.23f);
        v->hold=samples(fmaxf(attack+.001f,total-release));v->total=samples(total);end=total+.01f;
        v->peak=fmaxf(.0001f,(float)e->vel*(broken||pickup?.26f:.32f)/sqrtf((float)v->pitches));
        v->sustain=v->peak*(broken?.64f:short_note?.46f:answer?.60f:.82f);v->warmth=broken?.15f:.22f;
        v->filter_hz=(broken?2900:short_note?2700:answer?2350:1750)*(1+expr*.4f);
        v->filter_end=(broken?1700:short_note?1050:answer?1250:1100)*(1+expr*.3f);v->filter_q=.48f;v->sweep_end=v->total;
        for(unsigned j=0;j<v->pitches;j++)v->frequency[j]=midi(e->has_notes?e->notes[j]:e->note);
        break;}
    }
    v->attack=samples(attack);if(!v->attack)v->attack=1;v->end=samples(end);v->stop=v->end;v->gain=FLOOR_GAIN;
    v->peak=fmaxf(v->peak,.0001f);v->attack_mul=powf(v->peak/FLOOR_GAIN,1.f/v->attack);
    if(!v->chord)v->total=v->attack+samples(release);
    unsigned release_samples=v->chord?v->total-v->hold:v->total-v->attack;
    v->release_mul=powf(FLOOR_GAIN/(v->chord?v->sustain:v->peak),1.f/release_samples);
    if(v->filter_hz){filter_set(&v->filter,0,v->filter_hz,v->filter_q);v->filter_mul=powf(v->filter_end/v->filter_hz,16.f/v->sweep_end);}
    for(unsigned i=0;i<v->pitches;i++)for(unsigned bank=1;bank<5;bank++)if(v->frequency[i]*((1u<<(bank+1))-1)<ECHOY_AUDIO_RATE/2)v->triangle_bank[i]=bank;
    a->stats.started++;a->stats.active++;if(a->stats.active>a->stats.peak_voices)a->stats.peak_voices=a->stats.active;
}
static float voice_sample(echoy_audio_t *a,voice_t *v)
{
    if(v->age>=v->stop){v->used=false;a->stats.active--;return 0;}
    float sum=0;
    if(v->kind==ECHOY_HAT||v->kind==ECHOY_GRAIN){
        unsigned offset=v->age+v->noise_offset;float noise=offset<NOISE_LENGTH?a->noise[offset]:0;
        sum=filtered(&v->filter,noise);
        if(v->kind==ECHOY_GRAIN)sum+=.19f*sine(a,v->phase[0]);
    }else{
        for(unsigned i=0;i<v->pitches;i++){
            float fundamental=v->frequency[i]<ECHOY_AUDIO_RATE/2?sine(a,v->phase[i]):0;
            sum+=v->chord?fundamental+v->warmth*triangle(a,v->phase[i],v->triangle_bank[i]):fundamental*v->level[i];
        }
        if(v->filter_hz){
            if(v->age&&v->age%16==0){v->filter_hz=v->age>=v->sweep_end?v->filter_end:v->filter_hz*v->filter_mul;filter_set(&v->filter,0,v->filter_hz,v->filter_q);}
            sum=filtered(&v->filter,sum);
        }
    }
    float gain=v->gain;
    if(v->chord){
        if(v->age<v->attack)gain=FLOOR_GAIN+(v->peak-FLOOR_GAIN)*v->age/v->attack;
        else if(v->age<=v->hold)gain=v->peak+(v->sustain-v->peak)*(v->age-v->attack)/(v->hold-v->attack);
        else if(v->age<v->total)gain=v->gain*v->release_mul;
        else gain=FLOOR_GAIN;
        v->gain=gain;
    }else if(v->age<v->attack)v->gain=fminf(v->peak,v->gain*v->attack_mul);
    else if(v->age<v->total)v->gain=fmaxf(FLOOR_GAIN,v->gain*v->release_mul);
    else v->gain=FLOOR_GAIN;
    for(unsigned i=0;i<v->pitches;i++){v->phase[i]+=v->frequency[i]/ECHOY_AUDIO_RATE;v->phase[i]-=floorf(v->phase[i]);}
    if(v->kind==ECHOY_KICK&&v->age<v->sweep_end)v->frequency[0]*=v->frequency_mul;
    v->age++;return sum*gain;
}
static float next_sample(echoy_audio_t *a)
{
    if(a->stats.pending)for(unsigned i=0;i<ECHOY_AUDIO_PENDING;i++){
        pending_t *p=&a->pending[i];if(!p->used||p->target>a->stats.cursor)continue;
        if(p->target<a->stats.cursor)a->stats.missed++;else if(!a->channels[p->device].muted)start(a,p);
        p->used=false;a->stats.pending--;
    }
    float dry[3]={0},send[3]={0};
    for(unsigned i=0;i<ECHOY_AUDIO_VOICES;i++)if(a->voices[i].used){voice_t *v=&a->voices[i];float x=voice_sample(a,v);dry[v->device]+=x;if(v->wet)send[v->device]+=x;}
    float sum=0;
    for(unsigned i=0;i<3;i++){
        channel_t *c=&a->channels[i];float wet=filtered(&c->damp,c->delay[c->delay_pos]);
        c->delay[c->delay_pos]=send[i]+.18f*wet;if(++c->delay_pos==DELAY_LENGTH)c->delay_pos=0;
        c->gain+=( (c->muted?0:c->volume/.70f)-c->gain)*.0011567387f; /* 18 ms */
        c->color_db+=(c->expression*10-c->color_db)*.0008329862f; /* 25 ms */
        if(a->stats.cursor%64==0)shelf_set(&c->color,c->color_db);
        sum+=(.5f*filtered(&c->color,dry[i])+.2f*wet)*c->gain;
    }
    a->master+=(a->target_master-a->master)*.0008329862f;
    sum*=a->master*.7071067811865475f; /* Center pan: identical stereo channels. */
    if(fabsf(sum)>1)a->stats.clipped_samples++;
    a->stats.cursor++;return a->stats.output_fault?0:clampf(sum,-1,1);
}
void echoy_audio_render(echoy_audio_t *a,float *stereo,size_t frames)
{
    if(!a||!stereo)return;
    for(size_t i=0;i<frames;i++)stereo[2*i]=stereo[2*i+1]=next_sample(a);
}
void echoy_audio_pcm(echoy_audio_t *a,int16_t *stereo,size_t frames)
{
    if(!a||!stereo)return;
    for(size_t i=0;i<frames;i++){float x=next_sample(a);stereo[2*i]=stereo[2*i+1]=(int16_t)lroundf(x*32767);}
}
int64_t echoy_audio_presentation_us(const echoy_audio_t *a)
{
    if(!a)return 0;
    long double us=a->epoch+(long double)a->stats.cursor*1000000/ECHOY_AUDIO_RATE;
    return us>INT64_MAX?INT64_MAX:(int64_t)roundl(us);
}
void echoy_audio_skip_to(echoy_audio_t *a,int64_t us)
{
    int64_t target;if(!a||!target_sample(a,us,&target)||target<=a->stats.cursor)return;
    for(unsigned i=0;i<ECHOY_AUDIO_PENDING;i++)if(a->pending[i].used&&a->pending[i].target<target){a->pending[i].used=false;a->stats.pending--;a->stats.missed++;}
    /* An output gap is not synthesized in a tight catch-up loop. Expire active
     * sources and local tails; caller must use this only for a DAC discontinuity. */
    for(unsigned i=0;i<ECHOY_AUDIO_VOICES;i++)a->voices[i].used=false;
    a->stats.active=0;a->stats.cursor=target;
    for(unsigned i=0;i<3;i++){memset(a->channels[i].delay,0,sizeof a->channels[i].delay);a->channels[i].damp.z1=a->channels[i].damp.z2=0;}
}
void echoy_audio_output_fault(echoy_audio_t *a,bool fault){if(a)a->stats.output_fault=fault;}
echoy_audio_stats_t echoy_audio_stats(const echoy_audio_t *a){return a?a->stats:(echoy_audio_stats_t){0};}
