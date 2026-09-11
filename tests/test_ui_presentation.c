// SPDX-License-Identifier: Apache-2.0
#include "echoy_view.h"
#include "echoy_wire.h"
#include "../pc/ui_frame.h"
#include <assert.h>
#include <string.h>
static uint8_t actual[sizeof pixels],expected[sizeof pixels],first[sizeof pixels];
static echoy_core_t core;
static void action(echoy_action_t type,double value,int64_t at){
 echoy_input_t e={.type=type,.device=0,.value=value,.at_us=at,.boot_id=1,.generation=core.devices[0].role_generation};
 assert(echoy_dispatch(&core,&e)==ECHOY_OK);
}
static void render(lv_obj_t *screen,echoy_view_t *v,uint8_t *out){
 lv_screen_load(screen);echoy_view_frame(v,core.time_us);lv_refr_now(lv_display_get_default());memcpy(out,pixels,sizeof pixels);
}
static void compare_plot(void){
 for(int y=180;y<320;y++)assert(!memcmp(actual+(y*480+45)*3,expected+(y*480+45)*3,285*3));
}
static void role_case(echoy_role_t role){
 uint64_t ids[]={100,0,0};echoy_core_init(&core,ids);action(ECHOY_BOOT,0,0);action(ECHOY_BOOT_READY,0,0);
 if(role!=ECHOY_PULSE){action(ECHOY_ROLE_REQUEST,role,0);action(ECHOY_ROLE_READY,0,0);}
 lv_obj_t *a=lv_obj_create(NULL),*b=lv_obj_create(NULL);
 echoy_view_t *v=echoy_view_create(a,NULL,NULL),*reference=echoy_view_create(b,NULL,NULL);
 action(ECHOY_PLAY_TOGGLE,0,0);echoy_view_update(v,&core,0);echoy_view_update(reference,&core,0);
 echoy_tick(&core,80000);assert(core.plan.devices[0].events.count);
 echoy_view_update(v,&core,0);echoy_view_update(reference,&core,0);
 const echoy_event_t hit=core.plan.devices[0].events.events[0];assert(hit.step==0);
 echoy_view_hit(reference,&hit,75000);render(a,v,actual);render(b,reference,expected);compare_plot();memcpy(first,actual,sizeof first);
 /* No new onset before step 1: repeated snapshots must retain the onset time,
  * so the pulse ring/voice energy decays rather than restarting every frame. */
 echoy_tick(&core,180000);echoy_view_update(v,&core,0);echoy_view_update(reference,&core,0);
 render(a,v,actual);render(b,reference,expected);compare_plot();assert(memcmp(first,actual,sizeof first));
 if(role==ECHOY_VOICE){
  echoy_snapshot_t old=core;
  /* A late snapshot must not replay its previous note or clock generation. */
  echoy_tick(&core,800000);echoy_view_update(v,&core,0);echoy_view_update(reference,&core,0);
  const echoy_event_t *latest=NULL;int64_t at=0;
  for(size_t i=0;i<core.plan.devices[0].events.count;i++){
   const echoy_event_t *e=&core.plan.devices[0].events.events[i];int64_t onset=75000+(int64_t)(e->step*156250);
   if(onset<=800000){latest=e;at=onset;}
  }
  assert(latest&&latest->note!=hit.note);echoy_view_hit(reference,latest,at);
  render(a,v,actual);render(b,reference,expected);compare_plot();memcpy(first,actual,sizeof first);
  echoy_view_update(v,&old,0);render(a,v,actual);assert(!memcmp(first,actual,sizeof first));
  /* Install through the wire/core boundary. At a different clock generation
   * in the same bar, elapsed notes must not reappear as newly played notes. */
  echoy_wire_state_t state={.revision=2,.reference_uid=100,.member_count=1,
   .members={{.uid=100,.boot=1,.role=ECHOY_VOICE,.phrase=1,.activity=560,.motif_available=true,.transport=ECHOY_PLAYING}}};
  assert(echoy_core_install(&core,0,&state,core.devices[0].session_id,75000,0,800000)==ECHOY_OK);
  echoy_view_update(v,&core,0);render(a,v,actual);assert(memcmp(first,actual,sizeof first));
  /* Old clock with a newer timestamp is still stale. */
  old.time_us=800001;memcpy(first,actual,sizeof first);echoy_view_update(v,&old,0);render(a,v,actual);assert(!memcmp(first,actual,sizeof first));
  action(ECHOY_PAUSE,0,800000);echoy_tick(&core,2574000);echoy_view_update(v,&core,0);
  action(ECHOY_PLAY_TOGGLE,0,2574000);echoy_tick(&core,2600000); /* Waiting for the next beat. */
  assert(core.devices[0].transport==ECHOY_WAITING);echoy_view_update(v,&core,0);
  echoy_tick(&core,3250000);echoy_view_update(v,&core,0);echoy_view_update(reference,&core,0);
  latest=&core.plan.devices[0].events.events[0];assert(core.plan.devices[0].bar==1);
  /* Pausing through the bar and resuming after step zero cannot replay it. */
  assert(core.devices[0].transport==ECHOY_PLAYING);assert(core.plan.devices[0].motif.count);
 }
 /* Output faults, zero volume and pause all suppress actual hit presentation
  * without erasing the motif. Compare against a fresh quiet view each time. */
 for(int mode=0;mode<3;mode++){
  action(mode==0?ECHOY_OUTPUT_FAULT:mode==1?ECHOY_VOLUME:ECHOY_PAUSE,mode==0?1:0,core.time_us);
  echoy_tick(&core,core.time_us+220000);echoy_view_update(v,&core,0);
  assert(core.plan.devices[0].motif.count);lv_obj_t *blank=lv_obj_create(NULL);echoy_view_t *quiet=echoy_view_create(blank,NULL,NULL);
  echoy_view_update(quiet,&core,0);render(a,v,actual);render(blank,quiet,expected);compare_plot();
  echoy_view_destroy(quiet);lv_screen_load(a);lv_obj_delete(blank);
  if(mode==0)action(ECHOY_OUTPUT_FAULT,0,core.time_us);
  if(mode==1)action(ECHOY_VOLUME,.7,core.time_us);
 }
 echoy_view_destroy(reference);echoy_view_destroy(v);
 lv_screen_load(lv_obj_create(NULL));lv_obj_delete(a);lv_obj_delete(b);
}
int main(void){pc_display_init();role_case(ECHOY_PULSE);role_case(ECHOY_VOICE);lv_deinit();return 0;}
