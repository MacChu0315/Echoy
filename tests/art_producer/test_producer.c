#define _GNU_SOURCE
#include "echoy_art_producer.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "device_fakes.h"
#endif

static pthread_t ui_thread;
static pthread_mutex_t gate_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_changed=PTHREAD_COND_INITIALIZER;
static bool gate_closed;
static atomic_uint entered, finished;
static unsigned allocation_calls, fail_allocation, live_allocations;
static size_t allocated_bytes;
static void *allocations[16];
static bool fail_thread, fail_join;
static unsigned push_calls, push_mode;
static uint16_t expected[300*204];
struct borrowed { const void *pixels; esp_gsp_canvas_release_cb_t release; void *ctx; uint64_t hash; bool active; };
static struct borrowed held[16];
static unsigned held_count;

double seconds(void) { struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC,&t)); return t.tv_sec+t.tv_nsec*1e-9; }
static void pause_ms(unsigned ms) { struct timespec t={ms/1000,(long)(ms%1000)*1000000}; nanosleep(&t,NULL); }
static uint64_t hash(const void *bytes) { const unsigned char *p=bytes;uint64_t h=1469598103934665603ULL;for(unsigned i=0;i<300*204*2;i++)h=(h^p[i])*1099511628211ULL;return h; }
void __real_free(void *p);
void __wrap_free(void *p) { for(unsigned i=0;i<16;i++) if(p && allocations[i]==p) { allocations[i]=NULL;--live_allocations;break; } __real_free(p); }
static void *storage(void *ctx,size_t size) {
    assert(ctx==&allocation_calls);
    if(++allocation_calls==fail_allocation)return NULL;
    void *p=malloc(size);assert(p);
    for(unsigned i=0;i<16;i++)if(!allocations[i]){allocations[i]=p;live_allocations++;allocated_bytes+=size;return p;}
    abort();
}
int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int __wrap_pthread_create(pthread_t *t,const pthread_attr_t *a,void *(*f)(void *),void *p) {
    if(fail_thread)return EAGAIN;
    return __real_pthread_create(t,a,f,p);
}
int __real_pthread_timedjoin_np(pthread_t,void **,const struct timespec *);
int __wrap_pthread_timedjoin_np(pthread_t t,void **p,const struct timespec *d) {
    if(fail_join){fail_join=false;return EINVAL;}
    return __real_pthread_timedjoin_np(t,p,d);
}
void __real_echoy_art_draw(const esp_gsp_canvas_surface_t *,void *);
void __wrap_echoy_art_draw(const esp_gsp_canvas_surface_t *s,void *ctx) {
    assert(!pthread_equal(ui_thread,pthread_self()));
    echoy_art_frame_t before;memcpy(&before,ctx,sizeof(before));
    echoy_plan_t plan={0};if(before.plan)plan=*before.plan;
    atomic_fetch_add(&entered,1);
    pthread_mutex_lock(&gate_lock);pthread_cond_broadcast(&gate_changed);
    while(gate_closed)pthread_cond_wait(&gate_changed,&gate_lock);
    pthread_mutex_unlock(&gate_lock);
    assert(!memcmp(&before,ctx,sizeof(before)));
    if(before.plan)assert(!memcmp(&plan,before.plan,sizeof(plan)));
    __real_echoy_art_draw(s,ctx);atomic_fetch_add(&finished,1);
}
esp_gsp_err_t esp_gsp_canvas_try_push(esp_gsp_handle_t gsp,uint16_t bind,const void *pixels,
        size_t stride,esp_gsp_canvas_release_cb_t release,void *ctx) {
    assert(pthread_equal(ui_thread,pthread_self()));assert(gsp==(void *)1 && bind==7 && stride==600);
    push_calls++;
    if(push_mode==1)return ESP_GSP_ERR_TIMEOUT;
    if(push_mode==2)return ESP_GSP_ERR_INVALID_ARG;
    if(push_mode==3){release(ctx);return ESP_GSP_OK;}
    assert(held_count<16);
    held[held_count++]=(struct borrowed){pixels,release,ctx,hash(pixels),true};
    return ESP_GSP_OK;
}
static void gate(bool closed) {pthread_mutex_lock(&gate_lock);gate_closed=closed;pthread_cond_broadcast(&gate_changed);pthread_mutex_unlock(&gate_lock);}
static void wait_entered(unsigned n) {double end=seconds()+3;while(atomic_load(&entered)<n){assert(seconds()<end);pause_ms(1);}}
static void wait_rendered(echoy_art_producer_t *p,unsigned n) {double end=seconds()+3;echoy_art_producer_stats_t s;do{echoy_art_producer_get_stats(p,&s);assert(seconds()<end);pause_ms(1);}while(s.rendered<n);}
static bool publish(echoy_art_producer_t *p,uint32_t epoch) {bool yes=false;assert(echoy_art_producer_publish_ready(p,(void *)1,7,epoch,&yes)==ESP_GSP_OK);return yes;}
static void wait_publish(echoy_art_producer_t *p,uint32_t epoch) {double end=seconds()+3;while(!publish(p,epoch)){assert(seconds()<end);pause_ms(1);}}
static void release(unsigned i) {assert(held[i].active);assert(hash(held[i].pixels)==held[i].hash);held[i].active=false;held[i].release(held[i].ctx);}
static void request(echoy_art_producer_t *p,const echoy_art_frame_t *f,unsigned epoch) {double end=seconds()+3;while(!echoy_art_producer_request(p,f,epoch)){assert(seconds()<end);pause_ms(1);}}
static echoy_art_producer_t *create(void) {
    echoy_art_producer_t *p=NULL;
    echoy_art_producer_config_t c={.storage_alloc=storage,.storage_ctx=&allocation_calls};
    assert(echoy_art_producer_create(&c,&p)==ESP_GSP_OK && p);
    assert(allocation_calls==4 && allocated_bytes<512*1024);
    return p;
}
static void reset(void) {assert(live_allocations==0);memset(held,0,sizeof(held));held_count=push_calls=push_mode=allocation_calls=fail_allocation=0;allocated_bytes=0;fail_thread=fail_join=false;atomic_store(&entered,0);atomic_store(&finished,0);gate(false);
#ifdef ESP_PLATFORM
    producer_test_external_check_fail(0);
#endif
}
static echoy_plan_t plan(void) {return (echoy_plan_t){.count=2,.events={{.step=0,.note=60,.duration=.3,.layer=ECHOY_ANCHOR},{.step=4,.note=67,.duration=.2,.layer=ECHOY_SUPPORT}}};}
static echoy_art_frame_t frame(echoy_plan_t *p) {return (echoy_art_frame_t){.plan=p,.role=ECHOY_VOICE,.width=300,.height=204,.active=true,.energy=.8f,.progress=.05};}
static uint64_t expected_hash(echoy_art_frame_t f) {esp_gsp_canvas_surface_t s={.pixels=expected,.width=300,.height=204,.stride_bytes=600,.pixel_format=ESP_GSP_CANVAS_PIXEL_RGB565};f.workspace=NULL;__real_echoy_art_draw(&s,&f);return hash(expected);}
static void dispose(echoy_art_producer_t **p) {assert(echoy_art_producer_stop(*p,1000)==ESP_GSP_OK);for(unsigned i=0;i<held_count;i++)if(held[i].active)release(i);assert(echoy_art_producer_destroy(p)==ESP_GSP_OK && !*p);assert(live_allocations==0);}

static void immutable_latest_and_no_starvation(void) {
    reset();echoy_art_producer_t *p=create();echoy_art_producer_set_epoch(p,1);
    echoy_plan_t notes=plan();echoy_art_frame_t f=frame(&notes);uint64_t first=expected_hash(f);
    f.workspace=(void *)1; /* A producer must use its independent workspace. */
    gate(true);request(p,&f,1);wait_entered(1);
    unsigned controls=0;
    for(unsigned i=0;i<20;i++){f.progress=.1+i*.001;f.energy=.7f-i*.01f;request(p,&f,1);controls++;}
    uint64_t latest=expected_hash(f);notes.events[0].note=100;f.energy=0;
    pause_ms(200);assert(controls==20 && atomic_load(&finished)==0);
    gate(false);wait_publish(p,1);assert(held[0].hash==first);
    wait_publish(p,1);assert(held[1].hash==latest);
    echoy_art_producer_stats_t s;echoy_art_producer_get_stats(p,&s);assert(s.published==2 && s.coalesced>=19);
#ifdef ESP_PLATFORM
    assert(s.worker_stack_min>0 && s.worker_stack_min<12288);
#else
    assert(s.worker_stack_min==0);
#endif
    dispose(&p);puts("PASS: blocked real worker leaves requests/control free; immutable latest mailbox and no animation starvation");
}
static void generation_and_final_frame(void) {
    reset();echoy_art_producer_t *p=create();echoy_plan_t n=plan();echoy_art_frame_t f=frame(&n);
    echoy_art_producer_set_epoch(p,10);gate(true);request(p,&f,10);wait_entered(1);
    echoy_art_producer_set_epoch(p,11);assert(!echoy_art_producer_request(p,&f,10));
    f.active=false;f.energy=0;f.role=ECHOY_HARMONY;uint64_t final=expected_hash(f);request(p,&f,11);
    gate(false);wait_publish(p,11);assert(held_count==1 && held[0].hash==final);
    echoy_art_producer_stats_t s;echoy_art_producer_get_stats(p,&s);assert(s.stale>=1);
    release(0);request(p,&f,11);wait_rendered(p,s.rendered+1);
    echoy_art_producer_set_epoch(p,12);assert(!publish(p,12));assert(held_count==1);
    dispose(&p);puts("PASS: recording/role generation invalidates old in-flight and ready output; final paused frame survives");
}
static void queue_errors_and_borrowed_buffers(void) {
    reset();echoy_art_producer_t *p=create();echoy_plan_t n=plan();echoy_art_frame_t f=frame(&n);
    echoy_art_producer_set_epoch(p,2);request(p,&f,2);wait_rendered(p,1);
    bool yes=true;push_mode=1;assert(echoy_art_producer_publish_ready(p,(void*)1,7,2,&yes)==ESP_GSP_ERR_TIMEOUT && !yes);
    push_mode=2;assert(echoy_art_producer_publish_ready(p,(void*)1,7,2,&yes)==ESP_GSP_ERR_INVALID_ARG && !yes);
    push_mode=0;wait_publish(p,2);f.energy=.4f;request(p,&f,2);wait_publish(p,2);
    assert(held_count==2);unsigned done=atomic_load(&finished);f.energy=.2f;request(p,&f,2);pause_ms(25);assert(atomic_load(&finished)==done);
    release(0);wait_rendered(p,done+1);wait_publish(p,2);assert(held_count==3);
    assert(echoy_art_producer_destroy(&p)==ESP_GSP_ERR_INVALID_STATE && p);
    assert(echoy_art_producer_stop(p,1000)==ESP_GSP_OK);
    assert(echoy_art_producer_destroy(&p)==ESP_GSP_ERR_INVALID_STATE && p);
    release(1);release(2);assert(echoy_art_producer_destroy(&p)==ESP_GSP_OK && !p && !live_allocations);
    puts("PASS: queue-full/errors retain ready; two borrowed buffers are immutable; delayed release after stop is safe");
}
static void synchronous_release_and_stop_retry(void) {
    reset();echoy_art_producer_t *p=create();echoy_plan_t n=plan();echoy_art_frame_t f=frame(&n);
    echoy_art_producer_set_epoch(p,3);request(p,&f,3);wait_rendered(p,1);push_mode=3;wait_publish(p,3);
    gate(true);f.energy=.3f;request(p,&f,3);wait_entered(2);
    assert(echoy_art_producer_stop(p,1)==ESP_GSP_ERR_TIMEOUT);
    assert(!echoy_art_producer_request(p,&f,3));assert(echoy_art_producer_destroy(&p)==ESP_GSP_ERR_INVALID_STATE && p);
#ifdef ESP_PLATFORM
    producer_test_hold_suspend(true);gate(false);producer_test_wait_suspend_pending();
    assert(echoy_art_producer_stop(p,1)==ESP_GSP_ERR_TIMEOUT && p);
    producer_test_hold_suspend(false);
#else
    gate(false);fail_join=true;assert(echoy_art_producer_stop(p,1000)==ESP_GSP_ERR_INVALID_STATE && p);
#endif
    assert(echoy_art_producer_stop(p,1000)==ESP_GSP_OK);assert(echoy_art_producer_stop(p,0)==ESP_GSP_OK);
    assert(echoy_art_producer_destroy(&p)==ESP_GSP_OK && !p && !live_allocations);
    puts("PASS: synchronous release, active-worker timeout and failed join preserve retryable ownership");
}
static void initialization_failures(void) {
    for(unsigned fail=1;fail<=4;fail++){
        reset();fail_allocation=fail;echoy_art_producer_t *p=(void*)1;
        echoy_art_producer_config_t c={storage,&allocation_calls};
        assert(echoy_art_producer_create(&c,&p)==ESP_GSP_ERR_NO_MEM && !p && !live_allocations);
    }
    reset();fail_thread=true;echoy_art_producer_t *p=(void*)1;echoy_art_producer_config_t c={storage,&allocation_calls};
    assert(echoy_art_producer_create(&c,&p)==ESP_GSP_ERR_NO_MEM && !p && !live_allocations);
    assert(echoy_art_producer_create(NULL,&p)==ESP_GSP_ERR_INVALID_ARG && !p);
#ifdef ESP_PLATFORM
    for(unsigned fail=1;fail<=4;fail++){
        reset();producer_test_external_check_fail(fail);
        assert(echoy_art_producer_create(&c,&p)==ESP_GSP_ERR_NO_MEM && !p && !live_allocations);
    }
#endif
    puts("PASS: each allocation and worker-creation failure releases partial construction without fallback");
}
static void *release_later(void *unused) { (void)unused;pause_ms(10);release(0);return NULL; }
static void concurrent_final_release(void) {
    reset();echoy_art_producer_t *p=create();echoy_plan_t n=plan();echoy_art_frame_t f=frame(&n);
    echoy_art_producer_set_epoch(p,9);request(p,&f,9);wait_publish(p,9);
    assert(echoy_art_producer_stop(p,1000)==ESP_GSP_OK);
    pthread_t releasing;assert(!pthread_create(&releasing,NULL,release_later,NULL));
    double end=seconds()+3;
    while(p){esp_gsp_err_t error=echoy_art_producer_destroy(&p);assert(error==ESP_GSP_OK || error==ESP_GSP_ERR_INVALID_STATE);assert(seconds()<end);if(p)pause_ms(1);}
    assert(!pthread_join(releasing,NULL));assert(!live_allocations);
    puts("PASS: a concurrent final GSP release completes before successful destruction");
}
static void pixels_all_roles(void) {
    reset();echoy_art_producer_t *p=create();echoy_plan_t n=plan();echoy_art_frame_t f=frame(&n);
    for(unsigned role=0;role<4;role++)for(unsigned state=0;state<3;state++){
        unsigned epoch=1+role*3+state;
        echoy_art_producer_set_epoch(p,epoch);
        f.role=role;f.active=state==1;f.empty=state==2;f.anchors_only=state==1;
        f.plan=state==2?NULL:&n;f.progress=.375;f.energy=state==1?.7f:0;
        uint64_t wanted=expected_hash(f);request(p,&f,epoch);wait_publish(p,epoch);
        assert(held[held_count-1].hash==wanted);release(held_count-1);
    }
    echoy_art_producer_stats_t s;echoy_art_producer_get_stats(p,&s);
    assert(s.rendered==12 && s.published==12 && s.draw_max_us>0);
    dispose(&p);puts("PASS: completed RGB565 frames match isolated rasterization for all roles, capture and empty states");
}
int main(void) {ui_thread=pthread_self();immutable_latest_and_no_starvation();generation_and_final_frame();queue_errors_and_borrowed_buffers();synchronous_release_and_stop_retry();initialization_failures();concurrent_final_release();pixels_all_roles();return 0;}
