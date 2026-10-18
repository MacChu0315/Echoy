#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static echoy_band_t *ready(void){
 const float gains[4]={1.6f,.78f,.0634f,.8f};
 echoy_band_t *b=echoy_band_create_configured(true,gains);assert(b);
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_VOICE,0));
 echoy_band_job_t *j=echoy_band_take_job(b);assert(j&&echoy_band_prepare_job(j));
 assert(echoy_band_finish_job(b,j,0));echoy_band_tick(b,50000);return b;
}
static double energy(const float *s,unsigned a,unsigned b){double e=0;for(unsigned i=a;i<b;i++)e+=s[i]*s[i];return e;}
static void first_and_slow(void){
 echoy_band_t *b=ready();float *pcm=calloc(36000,sizeof(float));assert(pcm);
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,1000000));
 echoy_band_render(b,1000000,pcm,36000);
 assert(energy(pcm,7200,7900)>1e-5); /* First reply still sounds at300ms. */
 for(unsigned i=8500;i<36000;i++)assert(pcm[i]==0);
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,2500000));
 echoy_band_render(b,2500000,pcm,36000);
 assert(energy(pcm,14400,26400)>1e-5); /* Slow tap gives a real phrase, not one onset. */
 for(unsigned i=29000;i<36000;i++)assert(pcm[i]==0);
 echoy_control_view_t v;assert(echoy_band_read(b,ECHOY_TILE_A,&v));assert(!v.state.seed&&!v.state.continuous);
 free(pcm);echoy_band_delete(b);puts("PASS first tap is medium; slow taps give bounded long phrases without seed/playback changes");
}
static void slow_to_fast(void){
 echoy_band_t *a=ready(),*control=ready(),*fresh=ready();echoy_band_t *bands[]={a,control,fresh};
 float first[24000],before[2400],actual[7200],old[7200],new_only[7200];
 for(unsigned i=0;i<3;i++){
  assert(echoy_band_action(bands[i],ECHOY_TILE_A,ECHOY_ACT_TAP,0,1000000));
  echoy_band_render(bands[i],1000000,first,24000);
  assert(echoy_band_action(bands[i],ECHOY_TILE_A,ECHOY_ACT_TAP,0,2500000));
  echoy_band_render(bands[i],2500000,before,2400);
 }
 assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_TAP,0,2600000));
 assert(echoy_band_action(fresh,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,2600000));
 assert(echoy_band_action(fresh,ECHOY_TILE_A,ECHOY_ACT_TAP,0,2600000));
 echoy_band_render(a,2600000,actual,7200);echoy_band_render(control,2600000,old,7200);
 echoy_band_render(fresh,2600000,new_only,7200);
 assert(!memcmp(actual,old,73*sizeof(float))); /* No sudden cut before the new onset. */
 for(unsigned i=72;i<456;i++)assert(fabs(actual[i]-new_only[i]-old[i]*(1-(i-72)/384.0))<1e-6);
 assert(!memcmp(actual+456,new_only+456,(1500-456)*sizeof(float)));
 for(unsigned i=2000;i<7200;i++)assert(actual[i]==0);
 assert(energy(old,2400,7200)>1e-5);
 for(unsigned i=0;i<3;i++)echoy_band_delete(bands[i]);
 puts("PASS a fast tap gives a short fragment and fades the previous long phrase over16ms");
}
static void queued_taps_and_reset(void){
 echoy_band_t *b=ready();float pcm[24000];
 for(unsigned i=0;i<256;i++)assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,0));
 echoy_band_render(b,0,pcm,24000);
 assert(energy(pcm,72,1800)>1e-5);
 assert(energy(pcm,7200,7900)>1e-5);
 for(unsigned i=8500;i<24000;i++)assert(pcm[i]==0);
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,1000000));
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,1000000));
 echoy_band_render(b,1000000,pcm,24000);
 assert(energy(pcm,7200,7900)>1e-5);for(unsigned i=8500;i<24000;i++)assert(pcm[i]==0);
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_HARMONY,2000000));
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,2000000));
 echoy_band_render(b,2000000,pcm,24000);
 assert(energy(pcm,7200,7900)>1e-8);for(unsigned i=8500;i<24000;i++)assert(pcm[i]==0);
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_VOICE,3000000));
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_HARMONY,3000000));
 assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,3000000));
 echoy_band_render(b,3000000,pcm,24000);
 assert(energy(pcm,7200,7900)>1e-8);for(unsigned i=8500;i<24000;i++)assert(pcm[i]==0);
 echoy_band_delete(b);
 puts("PASS equal-time excess taps preserve the first reply; Pause and changing role restart with medium reply");
}
static void windows_match_real_palette(void){
 echoy_band_t *b=ready();const float gains[4]={1.6f,.78f,.0634f,.8f};
 echoy_audio_preview_pool_t pool;assert(echoy_audio_render_preview_pool(ECHOY_VOICE,true,gains[1],&pool)==ECHOY_AUDIO_OK);
 float *pcm=calloc(36000,sizeof(float));assert(pcm);bool wrapped=false;
 for(unsigned tap=0;tap<16;tap++){
  int64_t at=1000000+(int64_t)tap*1500000;size_t frames=tap?28800:8400;
  assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,at));echoy_band_render(b,at,pcm,36000);
  bool matched=false;
  for(unsigned choice=0;choice<ECHOY_PREVIEW_COUNT;choice++){
   bool same=true;
   for(size_t i=0;i<frames;i++){
    double fade=i<48?i/48.0:i+384>=frames?(frames-1-i)/384.0:1;
    float expected=(float)(pool.phrase->samples[(pool.source_frames[choice]+i)%60000]*fade*.65*.45);
    if(fabsf(pcm[72+i]-expected)>1e-7f){same=false;break;}
   }
   if(same){matched=true;wrapped|=pool.source_frames[choice]+frames>60000;}
  }
  assert(matched);for(unsigned i=0;i<72;i++)assert(pcm[i]==0);
  for(size_t i=72+frames;i<36000;i++)assert(pcm[i]==0);
 }
 assert(wrapped);free(pcm);echoy_audio_free_preview_pool(&pool);echoy_band_delete(b);
 puts("PASS medium/long windows preserve real palette pitch/samples through end-of-bar wrap and stop exactly");
}
static void input_rate_and_role_guard(void){
 echoy_band_t *a=ready(),*control=ready();float actual[2400],expected[2400];
 /* Accepted100ms taps and extra1ms inputs must produce exactly the same PCM,
  * without advancing the random bag or shortening any accepted fragment. */
 for(unsigned block=0;block<10;block++){
  int64_t at=1000000+block*100000;
  assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_TAP,0,at));
  assert(echoy_band_action(control,ECHOY_TILE_A,ECHOY_ACT_TAP,0,at));
  for(unsigned k=1;k<100;k++)assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_TAP,0,at+k*1000));
  echoy_band_render(a,at,actual,2400);echoy_band_render(control,at,expected,2400);
  assert(!memcmp(actual,expected,sizeof(actual)));
 }
 assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,3000000));
 /* A UI tap tagged with the old role cannot sound after a queued role edit. */
 assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_HARMONY,3000000));
 assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_TAP,ECHOY_VOICE+1,3000000));
 echoy_band_render(a,3000000,actual,2400);assert(energy(actual,0,2400)==0);
 assert(echoy_band_action(a,ECHOY_TILE_A,ECHOY_ACT_TAP,ECHOY_HARMONY+1,3100000));
 echoy_band_render(a,3100000,actual,2400);assert(energy(actual,0,2400)>0);
 echoy_band_delete(a);echoy_band_delete(control);
 puts("PASS1kHz input matches10Hz PCM exactly; stale visible-role tap is silent");
}
int main(void){input_rate_and_role_guard();first_and_slow();slow_to_fast();queued_taps_and_reset();windows_match_real_palette();}
