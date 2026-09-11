// SPDX-License-Identifier: Apache-2.0
#include "echoy_touch_delivery.h"
#include "echoy_control.h"
#include "echoy_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    bool busy, locked;
    echoy_view_t *view;
    echoy_control_t control;
    unsigned activity, volume, finished, plays;
} fixture_t;
static bool lock(void *ctx){fixture_t *f=ctx;if(f->busy)return false;assert(!f->locked);f->locked=true;return true;}
static void unlock(void *ctx){fixture_t *f=ctx;assert(f->locked);f->locked=false;}
static void pointer(void *ctx,echoy_pointer_t phase,int x,int y){fixture_t *f=ctx;assert(f->locked);echoy_view_pointer(f->view,phase,x,y);}
static void emit(void *ctx,echoy_ui_event_t e){
    fixture_t *f=ctx;
    if(e.action==UI_SET_ACTIVITY)f->activity++;
    if(e.action==UI_SET_VOLUME)f->volume++;
    if(e.action==UI_FINISH_ACTIVITY)f->finished++;
    if(e.action==UI_PLAY)f->plays++;
    assert(echoy_control_enqueue(&f->control,e));
}
static void run(echoy_pointer_t release,bool volume,bool flush_before_next){
    fixture_t f={0};echoy_touch_delivery_t delivery={0};
    echoy_touch_delivery_ops_t ops={lock,unlock,pointer,&f};
    echoy_control_init(&f.control,123,7,9,NULL,NULL,NULL);
    f.view=echoy_view_create(lv_screen_active(),emit,&f);assert(f.view);
    f.control.core.devices[0].volume_open=volume;
    echoy_view_update(f.view,&f.control.core,0);
    int x=volume?200:100,y=volume?380:260;
    echoy_touch_delivery_send(&delivery,&ops,UI_DOWN,x,y);
    echoy_touch_delivery_send(&delivery,&ops,UI_MOVE,x+20,y-10);
    echoy_control_tick(&f.control,1000,false);
    double activity=f.control.core.devices[0].activity,level=f.control.core.devices[0].volume;
    unsigned before_activity=f.activity,before_volume=f.volume;
    assert(volume?before_volume==2:before_activity==1);

    f.busy=true;
    echoy_touch_delivery_send(&delivery,&ops,release,x,y);
    for(int i=0;i<4;i++)echoy_touch_delivery_flush(&delivery,&ops);
    assert(delivery.processed==2&&delivery.dropped==1&&delivery.cleanups==0&&delivery.cancel_pending);
    /* An attempted new gesture while the lock is still busy also cannot
     * consume or clear the pending cancellation. */
    echoy_touch_delivery_send(&delivery,&ops,UI_DOWN,330,425);
    assert(delivery.processed==2&&delivery.dropped==2&&delivery.cleanups==0&&delivery.cancel_pending);
    f.busy=false;
    if(flush_before_next){
        echoy_touch_delivery_flush(&delivery,&ops);
        echoy_touch_delivery_flush(&delivery,&ops);
    }
    /* A volume gesture leaves its modal open; dismiss it only after the
     * retained cancellation has been applied, without editing the view. */
    if(volume){
        echoy_touch_delivery_send(&delivery,&ops,UI_DOWN,400,250);
        echoy_touch_delivery_send(&delivery,&ops,UI_UP,400,250);
    }
    echoy_touch_delivery_send(&delivery,&ops,UI_DOWN,330,425);
    echoy_touch_delivery_send(&delivery,&ops,UI_UP,330,425);
    echoy_control_tick(&f.control,2000,false);
    assert(f.plays==1);
    assert(f.control.core.devices[0].transport==ECHOY_WAITING);
    assert(delivery.cleanups==1&&!delivery.cancel_pending);
    assert(delivery.processed==(volume?6u:4u)&&delivery.dropped==2);
    assert(f.activity==before_activity&&f.volume==before_volume);
    assert(f.finished==(volume?0u:1u));
    assert(f.control.core.devices[0].activity==activity&&f.control.core.devices[0].volume==level);
    echoy_touch_delivery_flush(&delivery,&ops);assert(delivery.cleanups==1);
    echoy_view_destroy(f.view);
}
int main(void){
    lv_init();lv_display_create(480,480);
    for(int release=UI_UP;release<=UI_CANCEL;release++)
        for(int volume=0;volume<2;volume++)
            for(int flush=0;flush<2;flush++)run((echoy_pointer_t)release,volume,flush);
    lv_deinit();puts("PASS dropped UP/CANCEL, repeated lock contention, cleanup once, next Play and unchanged activity/volume");
}
