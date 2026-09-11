// SPDX-License-Identifier: Apache-2.0
#include "echoy_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int emitted;static echoy_ui_event_t last;
static void emit(void *ctx,echoy_ui_event_t e){(void)ctx;emitted++;last=e;}
static lv_obj_t *find(lv_obj_t *p,const char *text){for(uint32_t i=0;i<lv_obj_get_child_count(p);i++){lv_obj_t *o=lv_obj_get_child(p,i);if(lv_obj_check_type(o,&lv_label_class)&&!lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN)&&!strcmp(lv_label_get_text(o),text))return o;}return NULL;}
static void transition_cases(echoy_view_t *v,echoy_snapshot_t *s){
    for(int target=0;target<5;target++){
        s->devices[0].lifecycle=ECHOY_READY;s->devices[0].loading=false;s->devices[0].volume_open=false;echoy_view_route(v,UI_PERFORM);echoy_view_update(v,s,0);
        echoy_view_pointer(v,UI_DOWN,100,230);echoy_view_pointer(v,UI_MOVE,100,220);assert(last.action==UI_SET_ACTIVITY);int before=emitted;
        if(target==0)s->devices[0].lifecycle=ECHOY_BOOTING;
        if(target==1)s->devices[0].loading=true;
        if(target==2)s->devices[0].volume_open=true;
        if(target==3)echoy_view_route(v,UI_ROLE);
        if(target==4)echoy_view_route(v,UI_STATUS);
        if(target<3)echoy_view_update(v,s,0);
        assert(emitted==before+1&&last.action==UI_FINISH_ACTIVITY);
        before=emitted;echoy_view_pointer(v,UI_MOVE,100,100);echoy_view_pointer(v,UI_UP,100,100);echoy_view_pointer(v,UI_CANCEL,100,100);assert(emitted==before);
    }
    for(int target=0;target<4;target++){
        s->devices[0].lifecycle=ECHOY_READY;s->devices[0].loading=false;s->devices[0].volume_open=true;echoy_view_route(v,UI_PERFORM);echoy_view_update(v,s,0);
        echoy_view_pointer(v,UI_DOWN,220,380);assert(last.action==UI_SET_VOLUME);int before=emitted;
        if(target==0)s->devices[0].lifecycle=ECHOY_BOOTING;
        if(target==1)s->devices[0].loading=true;
        if(target==2)s->devices[0].volume_open=false;
        if(target==3)echoy_view_route(v,UI_ROLE);
        if(target<3)echoy_view_update(v,s,0);
        echoy_view_pointer(v,UI_MOVE,400,380);echoy_view_pointer(v,UI_UP,400,380);assert(emitted==before);
    }
}
static void opacity_cases(echoy_view_t *v,echoy_snapshot_t *s,lv_obj_t *screen){
    echoy_view_route(v,UI_PERFORM);s->devices[0].transport=ECHOY_PAUSED;echoy_view_update(v,s,0);lv_obj_update_layout(screen);lv_obj_t *plot=NULL;
    for(uint32_t i=0;i<lv_obj_get_child_count(screen);i++){lv_obj_t *o=lv_obj_get_child(screen,i);if(lv_obj_get_x(o)==16&&lv_obj_get_y(o)==154&&lv_obj_get_width(o)==342&&lv_obj_get_height(o)==218)plot=o;}
    assert(plot);assert(lv_obj_get_style_opa_layered(plot,0)==115);
    s->devices[0].transport=ECHOY_PLAYING;echoy_view_update(v,s,0);assert(lv_obj_get_style_opa_layered(plot,0)==255);
    s->devices[0].transport=ECHOY_WAITING;echoy_view_update(v,s,0);assert(lv_obj_get_style_opa_layered(plot,0)==115);
    s->devices[0].transport=ECHOY_PAUSED;
}
int main(int argc,char **argv){
    lv_init();lv_display_create(480,480);lv_obj_t *screen=lv_screen_active();echoy_view_t *v=echoy_view_create(screen,emit,NULL);assert(v);
    echoy_snapshot_t s;const uint64_t ids[]={1,2,3};echoy_core_init(&s,ids);s.devices[0].lifecycle=ECHOY_READY;s.devices[0].motif_available=true;s.devices[0].link_online=true;s.devices[0].role=ECHOY_HARMONY;s.devices[0].phrase=2;s.devices[0].activity=.5;s.devices[0].committed_activity=.5;s.devices[0].session_id=1;s.sessions[0]=(echoy_session_t){.id=1,.members=1};
    if(argc>1){if(!strcmp(argv[1],"opacity"))opacity_cases(v,&s,screen);else transition_cases(v,&s);echoy_view_destroy(v);lv_deinit();return 0;}
    opacity_cases(v,&s,screen);transition_cases(v,&s);s.devices[0].lifecycle=ECHOY_READY;s.devices[0].loading=false;s.devices[0].volume_open=false;echoy_view_route(v,UI_PERFORM);
    echoy_snapshot_t before=s;echoy_view_update(v,&s,0);assert(find(screen,"和声"));assert(find(screen,"分解"));uint32_t count=lv_obj_get_child_count(screen);
    for(int i=0;i<100;i++){echoy_view_locale(v,i%2);echoy_view_update(v,&s,0);echoy_view_frame(v,i*33334);assert(lv_obj_get_child_count(screen)==count);}
    assert(!memcmp(&s,&before,sizeof(s)));echoy_view_locale(v,true);lv_obj_update_layout(screen);lv_obj_t *title=find(screen,"HARMONY");assert(title);assert(lv_obj_get_style_text_font(title,0)==echoy_font(32,true));assert(lv_obj_get_x(title)==35&&lv_obj_get_y(title)==42);
    s.devices[0].battery=4;echoy_view_update(v,&s,0);assert(find(screen,"Low"));echoy_view_locale(v,false);assert(find(screen,"请充电"));
    s.devices[0].pending_role=1;s.devices[0].pending_phrase=3;echoy_view_update(v,&s,0);assert(find(screen,"和声")&&find(screen,"分解"));assert(find(screen,"声线 · 长线 待生效"));
    echoy_view_route(v,UI_ROLE);assert(find(screen,"当前：和声 · 分解"));
    echoy_view_route(v,UI_PERFORM);echoy_view_pointer(v,UI_DOWN,270,60);echoy_view_pointer(v,UI_UP,270,60);assert(last.action==UI_VOLUME_OPEN);
    int calls=emitted;echoy_view_pointer(v,UI_DOWN,100,426);echoy_view_pointer(v,UI_UP,100,426);assert(emitted==calls);assert(find(screen,"音量"));
    echoy_view_pointer(v,UI_DOWN,400,250);echoy_view_pointer(v,UI_UP,400,250);assert(last.action==UI_VOLUME_CLOSE);
    s.devices[0].pending_role=-1;s.devices[0].pending_phrase=0;
    echoy_view_route(v,UI_ROLE);s.devices[0].preparing_role=1;echoy_view_update(v,&s,0);assert(find(screen,"正在准备 声线"));s.devices[0].preparing_role=-1;echoy_view_update(v,&s,0);assert(find(screen,"角色与乐句"));
    s.devices[0].volume_open=true;echoy_view_route(v,UI_PERFORM);echoy_view_update(v,&s,0);calls=emitted;uint32_t key=LV_KEY_ENTER;lv_obj_send_event(screen,LV_EVENT_KEY,&key);assert(emitted==calls);key=LV_KEY_RIGHT;lv_obj_send_event(screen,LV_EVENT_KEY,&key);assert(last.action==UI_SET_VOLUME);key=LV_KEY_ESC;lv_obj_send_event(screen,LV_EVENT_KEY,&key);assert(last.action==UI_VOLUME_CLOSE);
    s.devices[0].lifecycle=ECHOY_BOOTING;echoy_view_update(v,&s,0);calls=emitted;echoy_view_pointer(v,UI_DOWN,100,400);echoy_view_pointer(v,UI_UP,100,400);assert(emitted==calls);
    echoy_view_destroy(v);lv_deinit();puts("UI snapshots, current/pending, locale preservation, low battery, object reuse, modal: PASS");
}
