#include "echoy_band.h"
#include "echoy_device_pcm.h"
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FRAMES 24000
static void prepare(echoy_band_t *b,int64_t at){
 echoy_band_job_t *j=echoy_band_take_job(b);if(j){assert(echoy_band_prepare_job(j));assert(echoy_band_finish_job(b,j,at));}
 echoy_band_tick(b,at+50000);
}
static uint64_t hash(const float *pcm){
 const unsigned char *p=(const unsigned char *)pcm;uint64_t h=1469598103934665603ULL;
 for(unsigned i=0;i<7200*sizeof(float);i++)h=(h^p[i])*1099511628211ULL;
 return h;
}
static void clips_retain_real_voiced_samples(void){
 const float gains[4]={1.6f,.78f,.0634f,.8f};
 for(unsigned speaker=0;speaker<2;speaker++)for(unsigned role=0;role<4;role++){
  float gain=speaker?gains[role]:1;echoy_audio_preview_pool_t pool;
  assert(echoy_audio_render_preview_pool(role,speaker,gain,&pool)==ECHOY_AUDIO_OK);
  echoy_plan_t plan;assert(echoy_compose_mask(ECHOY_PREVIEW_SEED,role,1,.5,&plan));
  echoy_audio_loops_t *loops=NULL;assert(echoy_audio_prepare(&plan,role==ECHOY_PULSE,&loops)==ECHOY_AUDIO_OK);
  if(speaker)assert(echoy_audio_voice_for_speaker(&plan,loops));
  unsigned kinds=0,pitches[128]={0},pitch_count=0;
  for(unsigned clip=0;clip<ECHOY_PREVIEW_COUNT;clip++){
   echoy_audio_buffer_t *b=pool.phrase;size_t start=pool.source_frames[clip],span=pool.short_frames[clip];
   assert(b && b->frames==60000 && span>384 && span<=12000 && start+span<=loops->frames);
   bool found=false;
   for(unsigned e=0;e<plan.count;e++){
    size_t at=(size_t)ceil(plan.events[e].step*.15625*ECHOY_AUDIO_SAMPLE_RATE);
    if(at==start){found=true;kinds|=1u<<plan.events[e].kind;
     int note=plan.events[e].note;if(note>=0 && note<128 && !pitches[note]++){++pitch_count;}}
    if(at>start)assert(start+span<=at);
   }
   assert(found);
   for(size_t i=0;i<b->frames;i++)assert(b->samples[i]==loops->samples[ECHOY_AUDIO_FULL][i]*gain);
  }
  if(role==ECHOY_PULSE)assert((kinds&7)==7);
  else assert(pitch_count>=4);
  echoy_audio_free_preview_pool(&pool);echoy_audio_free_loops(loops);
 }
 puts("PASS full palettes retain original normalized/voiced samples and8 valid onset/grain bounds covering role sounds");
}
static void rapid_taps_remain_bounded(void){
 const float gains[4]={1.6f,.78f,.0634f,.8f};
 for(unsigned role=0;role<4;role++){
  echoy_band_t *b=echoy_band_create_configured(true,gains);assert(b);
  assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,role,0));prepare(b,0);
  echoy_device_pcm_t converter;echoy_device_pcm_reset(&converter);
  float samples[720];int16_t pcm[720];
  for(unsigned i=0;i<32;i++){
   int64_t at=10000000+(int64_t)i*30000;
   assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,at));
   echoy_band_render(b,at,samples,720);
   echoy_device_pcm_stats_t stats;assert(echoy_device_pcm_convert(&converter,samples,pcm,720,&stats));
   assert(!stats.clipped);for(unsigned n=0;n<720;n++)assert(abs(pcm[n])<=19660);
  }
  assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,11000000));
  echoy_band_render(b,11000000,samples,720);
  for(unsigned n=0;n<720;n++)assert(samples[n]==0);
  echoy_control_view_t v;assert(echoy_band_read(b,ECHOY_TILE_A,&v));
  assert(!v.state.seed && !v.state.pending_seed && !v.state.continuous);
  echoy_band_delete(b);
 }
 puts("PASS rapid random taps retain device PCM ceiling, keep seed empty and Pause clears all snippets");
}
int main(void){
 clips_retain_real_voiced_samples();rapid_taps_remain_bounded();
 for(unsigned speaker=0;speaker<2;speaker++)for(unsigned role=0;role<4;role++){
  const float gains[4]={1.6f,.78f,.0634f,.8f};
  uint64_t empty[24];
  for(unsigned recorded=0;recorded<2;recorded++){
   echoy_band_t *b=echoy_band_create_configured(speaker,speaker?gains:NULL);assert(b);
   assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,role,0));
   if(recorded)assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_EXAMPLE,0,0));
   prepare(b,0);echoy_band_tick(b,3000000);
   echoy_control_view_t before,after;assert(echoy_band_read(b,ECHOY_TILE_A,&before));
   float *pcm=calloc(FRAMES,sizeof(float));assert(pcm);
   uint64_t previous=0,round[8];
   for(unsigned tap=0;tap<24;tap++){
    int64_t at=10000000+(int64_t)tap*1000000;
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,at));
    echoy_band_render(b,at,pcm,FRAMES);
    uint64_t value=hash(pcm);assert(value!=previous);previous=value;
    for(unsigned j=0;j<tap%8;j++)assert(round[j]!=value);
    round[tap%8]=value;
    if(recorded)assert(value==empty[tap]);else empty[tap]=value;
    double energy=0;for(unsigned k=0;k<FRAMES;k++)energy+=pcm[k]*pcm[k];assert(energy>1e-5);
    assert(echoy_band_read(b,ECHOY_TILE_A,&after));
    assert(!memcmp(&before.plan,&after.plan,sizeof(before.plan)));
    assert(before.state.seed==after.state.seed && before.state.generation==after.state.generation);
    assert(before.state.phase==after.state.phase && !after.state.continuous);
   }
   assert(memcmp(empty,empty+8,8*sizeof(*empty)));
   free(pcm);echoy_band_delete(b);
  }
 }
 puts("PASS every role draws8 different clips per round without adjacent repeats, independent of stored phrase and without state edits");
}
