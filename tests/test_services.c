// SPDX-License-Identifier: Apache-2.0
#include "echoy_services.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint8_t blob[64];static size_t length;static int error,writes,allocations,captures,releases;
static int rd(void *c,uint8_t *b,size_t *n){(void)c;if(error)return error;assert(*n>=length);memcpy(b,blob,length);*n=length;return 0;}
static int wr(void *c,const uint8_t *b,size_t n){(void)c;writes++;if(error)return error;memcpy(blob,b,n);length=n;return 0;}
static bool capture(void *c,uint8_t **p,size_t *n){
 (void)c;if(error==1)return false;*p=malloc(8);assert(*p);*n=8;allocations++;captures++;
 for(int i=0;i<8;i++)(*p)[i]=(uint8_t)(42+captures+i);
 return error!=2; /* Failure after backend allocation must still release it. */
}
static void release(void *c,uint8_t *p){(void)c;free(p);allocations--;releases++;}
int main(void){
 echoy_settings_t s;echoy_settings_defaults(&s);assert(s.role==ECHOY_PULSE&&s.phrase==1&&s.phrases[3]==2&&s.locale==0);
 echoy_storage_t st={.read=rd,.write=wr};echoy_settings_defaults(&st.saved);
 s.role=3;s.phrase=2;s.phrases[3]=2;s.volume=.4;assert(echoy_storage_stage(&st,&s,0));
 assert(echoy_storage_flush(&st,499999)&&writes==0);assert(echoy_storage_flush(&st,500000)&&writes==1);
 echoy_storage_t restored={.read=rd,.write=wr};assert(echoy_storage_load(&restored));assert(restored.saved.role==3&&fabs(restored.saved.volume-.4)<.0001);
 echoy_core_t c;echoy_core_init(&c,NULL);c.devices[0].transport=ECHOY_PLAYING;c.devices[0].pending_role=2;
 assert(echoy_settings_restore(&restored.saved,&c.devices[0]));assert(c.devices[0].transport==ECHOY_PAUSED&&c.devices[0].pending_role==-1&&c.devices[0].session_id==0);
 blob[0]=2;assert(!echoy_storage_load(&restored));assert(restored.saved.role==0&&restored.failures==1);
 error=17;assert(!echoy_storage_load(&restored));s.volume=.3;assert(echoy_storage_stage(&st,&s,1000000));assert(!echoy_storage_flush(&st,1500000)&&st.dirty&&st.last_error==17);
 s.volume=NAN;assert(!echoy_storage_stage(&st,&s,2000000));error=0;assert(echoy_storage_flush(&st,2500000)&&!st.dirty);
 echoy_battery_t b={0};assert(!echoy_battery_observe(&b,17,0)&&!b.valid&&!b.shutdown_requested);
 assert(echoy_battery_observe(&b,0,14)&&b.low&&b.percent==14);assert(!echoy_battery_observe(&b,17,0)&&b.percent==14&&!b.shutdown_requested);
 assert(!echoy_battery_observe(&b,0,255)&&b.percent==14);assert(echoy_battery_observe(&b,0,0)&&b.shutdown_requested);
 echoy_mirror_t m={.capture=capture,.release=release};uint8_t out[16];size_t n;
 for(int i=0;i<100;i++){
  int previous=captures,freed=releases;assert(echoy_mirror_begin(&m));assert(captures==previous+1);
  uint8_t expected[8];for(int j=0;j<8;j++)expected[j]=(uint8_t)(42+captures+j);
  assert(!echoy_mirror_begin(&m));
  /* Iris may read the tail first, then the head, then repeat arbitrary chunks.
   * The end offset is not a signal to advance to another capture. */
  assert(echoy_mirror_read(&m,6,out,16,&n)&&n==2&&!memcmp(out,expected+6,2));
  assert(echoy_mirror_read(&m,0,out,3,&n)&&n==3&&!memcmp(out,expected,3));
  assert(echoy_mirror_read(&m,3,out,4,&n)&&n==4&&!memcmp(out,expected+3,4));
  assert(echoy_mirror_read(&m,6,out,16,&n)&&n==2&&!memcmp(out,expected+6,2));
  assert(echoy_mirror_read(&m,0,out,16,&n)&&n==8&&!memcmp(out,expected,8));
  assert(echoy_mirror_read(&m,2,out,2,&n)&&n==2&&!memcmp(out,expected+2,2));
  assert(!echoy_mirror_read(&m,9,out,16,&n)&&n==0);
  assert(captures==previous+1&&releases==freed&&allocations==1);
  echoy_mirror_end(&m);echoy_mirror_end(&m);assert(allocations==0&&releases==freed+1);
  assert(!echoy_mirror_read(&m,0,out,16,&n));
 }
 error=1;assert(!echoy_mirror_begin(&m)&&allocations==0);
 error=2;int freed=releases;assert(!echoy_mirror_begin(&m)&&allocations==0&&releases==freed+1&&!m.pixels);
 error=0;assert(echoy_mirror_begin(&m));echoy_mirror_end(&m);assert(allocations==0);return 0;
}
