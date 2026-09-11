// SPDX-License-Identifier: Apache-2.0
#include "echoy_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_fixtures.inc"
static uint8_t pixels[480*480*3],drawbuf[480*480*3];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
    int width=a->x2-a->x1+1;
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){
        size_t dst=(y*480+x)*3,src=((y-a->y1)*width+x-a->x1)*3;
        pixels[dst]=p[src+2];pixels[dst+1]=p[src+1];pixels[dst+2]=p[src];
    }
    lv_display_flush_ready(d);
}
static bool labels_fit(lv_obj_t *surface){
    for(uint32_t i=0;i<lv_obj_get_child_count(surface);i++){
        lv_obj_t *o=lv_obj_get_child(surface,i);if(lv_obj_has_flag(o,LV_OBJ_FLAG_HIDDEN)||!lv_obj_check_type(o,&lv_label_class))continue;
        lv_point_t size;lv_text_get_size(&size,lv_label_get_text(o),lv_obj_get_style_text_font(o,0),lv_obj_get_style_text_letter_space(o,0),lv_obj_get_style_text_line_space(o,0),lv_obj_get_width(o),LV_TEXT_FLAG_NONE);
        if(size.y>lv_obj_get_height(o)){fprintf(stderr,"Clipped label: %s (%d > %d)\n",lv_label_get_text(o),(int)size.y,(int)lv_obj_get_height(o));return false;}
    }
    return true;
}
static echoy_snapshot_t snapshot(const ui_fixture_t *f){
    echoy_snapshot_t s;const uint64_t ids[]={1,2,3};echoy_core_init(&s,ids);s.time_us=10000000;
    echoy_device_t *d=&s.devices[0];d->lifecycle=ECHOY_READY;d->role=f->role;d->phrase=f->phrase;d->activity=f->activity;d->committed_activity=f->committed;d->volume=f->volume;d->battery=f->battery;d->motif_available=true;d->session_id=1;d->link_online=true;
    d->transport=!strcmp(f->state,"playing")?ECHOY_PLAYING:!strcmp(f->state,"waiting")?ECHOY_WAITING:ECHOY_PAUSED;
    d->volume_open=!strcmp(f->state,"volume");d->loading=!strcmp(f->state,"loading");d->preparing_role=!strcmp(f->state,"role-preparing")?3:-1;d->role_error=!strcmp(f->state,"role-error");d->data_fault=!strcmp(f->state,"data-error");d->output_fault=!strcmp(f->state,"audio-error");
    if(!strcmp(f->state,"booting"))d->lifecycle=ECHOY_BOOTING;
    s.sessions[0]=(echoy_session_t){.id=1,.members=1,.leader=0,.epoch_us=10000000};
    s.plan.count=1;s.plan.devices[0]=(echoy_plan_entry_t){.device=0,.source=-1,.motif=f->motif};
    const char *b=f->branch;
    if(!strcmp(b,"motif-recovery"))d->motif_available=false;
    if(!strcmp(b,"play-preparing"))d->play_preparing=true;
    if(!strcmp(b,"sync-failed")||!strcmp(b,"syncing")||!strcmp(b,"sync-waiting")){s.sync.members=3;s.sync.stage=!strcmp(b,"sync-failed")?ECHOY_SYNC_FAILED:!strcmp(b,"syncing")?ECHOY_SYNC_PREPARING:ECHOY_SYNC_WAITING;}
    if(!strcmp(b,"pending-role"))d->pending_role=3;
    if(!strcmp(b,"pending-phrase"))d->pending_phrase=2;
    if(!strcmp(b,"moving"))d->moving=true;
    if(!strcmp(b,"contact-confirming"))s.contacts[0][1]=(echoy_contact_t){.present=true,.uncertain=true,.since_us=s.time_us};
    if(!strcmp(b,"arrangement-pending"))s.sessions[0].arrangement_pending=true;
    if(!strcmp(b,"notice"))d->notice_until_us=s.time_us+1100000;
    if(!strcmp(b,"link-split")||!strcmp(b,"link-hold")){d->link_online=false;d->lost_split=!strcmp(b,"link-split");}
    if(!strcmp(b,"normal-source")||!strcmp(b,"paused-source")){s.plan.devices[0].source=1;s.sessions[0].members=3;}
    if(!strcmp(b,"normal-shared-solo")||!strcmp(b,"paused-shared"))s.sessions[0].members=3;
    if(!strcmp(b,"normal-leader")){s.plan.count=2;s.plan.devices[1].device=1;s.plan.devices[1].source=0;s.sessions[0].members=3;}
    return s;
}
int main(int argc,char **argv){
    if(argc!=3&&argc!=4){fprintf(stderr,"usage: %s locale/case output.ppm\n",argv[0]);return 2;}
    const ui_fixture_t *f=NULL;for(size_t i=0;i<sizeof(fixtures)/sizeof(fixtures[0]);i++)if(!strcmp(fixtures[i].id,argv[1]))f=&fixtures[i];if(!f)return 2;
    lv_init();lv_display_t *display=lv_display_create(480,480);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB888);lv_display_set_buffers(display,drawbuf,NULL,sizeof(drawbuf),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);
    lv_obj_t *screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0x20231e),0);lv_obj_set_style_bg_opa(screen,255,0);
    lv_obj_t *surface=lv_obj_create(screen);lv_obj_set_pos(surface,0,0);lv_obj_set_size(surface,480,480);
    echoy_view_t *v=echoy_view_create(surface,NULL,NULL);echoy_view_motion(v,true);echoy_snapshot_t s=snapshot(f);echoy_view_update(v,&s,0);echoy_view_locale(v,!strncmp(f->id,"en/",3));echoy_view_route(v,f->route);lv_obj_update_layout(screen);if(!labels_fit(surface))return 4;lv_refr_now(display);
    if(argc==4){FILE *meta=fopen(argv[3],"wb");if(!meta)return 3;for(int y=0;y<480;y++)for(int x=0;x<480;x++){echoy_ui_event_t e=echoy_view_hit_test(v,x,y);int action=e.action==UI_ROLE_SELECT?100+(int)e.value:e.action==UI_PHRASE_SELECT?110+(int)e.value:(int)e.action;fputc(action,meta);}if(fclose(meta))return 3;}
    FILE *out=fopen(argv[2],"wb");if(!out)return 3;fprintf(out,"P6\n480 480\n255\n");size_t written=fwrite(pixels,1,sizeof(pixels),out);int close=fclose(out);echoy_view_destroy(v);lv_deinit();return written==sizeof(pixels)&&close==0?0:3;
}
