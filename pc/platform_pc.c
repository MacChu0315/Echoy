// SPDX-License-Identifier: Apache-2.0
/* Development-only fixed-event transport. No JSON, geometry or host dependency
 * enters the core/control libraries. One process is one firmware owner. */
#include "echoy_control.h"
#include "echoy_view.h"
#include "echoy_audio.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_frame.h"
static echoy_control_t control;
static echoy_view_t *view;
static echoy_audio_t *audio;
static bool output_fault,fail_next,sync_fault;
static int64_t role_due,load_due,boot_due,now;
static uint32_t boot=1;
static uint64_t uid;
static unsigned rejected;
/* One interactive catch-up publication is bounded to 250 ms. */
#define PCM_FRAMES 12000
static int16_t pcm[PCM_FRAMES*2];static size_t pcm_frames;
static struct {uint64_t to;size_t n;uint8_t bytes[100];} packets[128];static size_t packet_count;
static const char *frame_path;
static echoy_result_t event(echoy_action_t type,double value){
    echoy_device_t *d=&control.core.devices[0];
    echoy_input_t e={.type=type,.device=0,.at_us=now,.boot_id=type==ECHOY_BOOT?boot:d->boot_id,.generation=type==ECHOY_SYNC_FAIL?control.core.sync.generation:d->role_generation,.value=value};
    echoy_result_t r=echoy_dispatch(&control.core,&e);if(r!=ECHOY_OK)rejected++;return r;
}
static bool send_packet(void *ctx,uint64_t to,const uint8_t *bytes,size_t n){
    (void)ctx;
    /* Lost PREPARE traffic is a development fault, never successful sync. */
    if(sync_fault&&bytes[3]!=ECHOY_HELLO&&bytes[3]!=ECHOY_HEARTBEAT)return true;
    if(n>100||packet_count==128)return false;
    packets[packet_count].to=to;packets[packet_count].n=n;memcpy(packets[packet_count++].bytes,bytes,n);return true;
}
static void recover(void){
    output_fault=fail_next=sync_fault=false;role_due=load_due=0;
    (void)event(ECHOY_ROLE_REQUEST,control.core.devices[0].role);
    (void)event(ECHOY_DATA_LOADING,0);(void)event(ECHOY_DATA_FAULT,0);(void)event(ECHOY_OUTPUT_FAULT,0);
}
static void emit(void *ctx,echoy_ui_event_t e){
    (void)ctx;
    /* Fake readiness is an adapter event, not a second role state machine. */
    if(e.action==UI_ROLE_SELECT){(void)event(ECHOY_ROLE_REQUEST,e.value);role_due=control.core.devices[0].preparing_role>=0?now+650000:0;}
    else if(e.action==UI_CANCEL_LOAD){role_due=load_due=0;(void)event(ECHOY_ROLE_CANCEL,0);(void)event(ECHOY_DATA_LOADING,0);echoy_view_route(view,UI_ROLE);}
    else if(e.action==UI_RECOVER){recover();echoy_view_route(view,UI_PERFORM);}
    else if(!echoy_control_enqueue(&control,e))rejected++;
}
static void power(bool on){
    if(!on){(void)event(ECHOY_POWER_OFF,0);role_due=load_due=boot_due=0;return;}
    if(control.core.devices[0].lifecycle!=ECHOY_OFF||!control.core.devices[0].battery)return;
    echoy_settings_t settings;echoy_settings_capture(&settings,&control.core.devices[0],control.english);
    int battery=control.core.devices[0].battery;
    boot++;echoy_control_init(&control,uid,boot-1,boot+50,&settings,send_packet,NULL);
    (void)event(ECHOY_POWER_OFF,0);(void)event(ECHOY_BATTERY,battery);(void)event(ECHOY_BOOT,0);
    echoy_wire_state_t initial=control.protocol.committed;initial.members[0].boot=boot;
    echoy_protocol_init(&control.protocol,uid,boot,boot+50,control.protocol.send,control.protocol.prepare,control.protocol.commit,control.protocol.ctx);
    control.protocol.committed=initial;control.installed_key=control.protocol.session;
    output_fault=false;boot_due=now+380000;echoy_view_route(view,UI_PERFORM);
    (void)echoy_audio_init(audio,echoy_audio_size(),now);
}
static void tick(int64_t at){
    now=at;echoy_device_t *d=&control.core.devices[0];
    if(boot_due&&now>=boot_due){boot_due=0;(void)event(ECHOY_BOOT_READY,0);}
    if(role_due&&now>=role_due){role_due=0;(void)event(fail_next?ECHOY_ROLE_FAILED:ECHOY_ROLE_READY,0);fail_next=false;}
    if(load_due&&now>=load_due){load_due=0;(void)event(ECHOY_DATA_LOADING,0);}
    if(d->lifecycle==ECHOY_READY)echoy_control_tick(&control,now,output_fault);else echoy_tick(&control.core,now);
    if(sync_fault&&control.core.sync.stage==ECHOY_SYNC_PREPARING){
        /* Development injection fails the protocol owner too, so diagnostics
         * and product feedback observe the same failure until renegotiation. */
        control.protocol.degraded=true;control.protocol.syncing=true;control.protocol.pending.active=false;
        (void)event(ECHOY_SYNC_FAIL,0);
    }
    echoy_audio_control(audio,0,d->clock_version,d->activity,d->volume,d->transport!=ECHOY_PLAYING||d->lifecycle!=ECHOY_READY||d->data_fault||d->loading);
    echoy_audio_output_fault(audio,output_fault);
    if(!echoy_audio_plan(audio,&control.core.plan,1))rejected++;
    int64_t cursor=echoy_audio_presentation_us(audio);
    size_t count=now>cursor?(size_t)((now-cursor)*48/1000):0;
    while(count){size_t n=count>256?256:count;int16_t block[512];echoy_audio_pcm(audio,block,n);
        if(pcm_frames+n>PCM_FRAMES){memmove(pcm,pcm+(pcm_frames+n-PCM_FRAMES)*2,(PCM_FRAMES-n)*4);pcm_frames=PCM_FRAMES-n;}
        memcpy(pcm+pcm_frames*2,block,n*4);pcm_frames+=n;count-=n;
    }
}
static void reply(bool frame){
    echoy_snapshot_t s;echoy_snapshot(&control.core,&s);echoy_device_t *d=&s.devices[0];
    /* Full core plans retain audible transitions across multiple T/F inputs. */
    if(frame){echoy_view_update(view,&s,0);echoy_view_locale(view,control.english);echoy_view_frame(view,now);}
    if(frame){lv_refr_now(lv_display_get_default());FILE *f=fopen(frame_path,"wb");if(!f)exit(3);fprintf(f,"P6\n480 480\n255\n");if(fwrite(pixels,1,sizeof pixels,f)!=sizeof pixels||fclose(f))exit(3);}
    static const char *roles[]={"pulse","voice","dust","harmony"},*transports[]={"paused","waiting","playing"};
    printf("{\"revision\":%"PRIu64",\"time_us\":%"PRId64",\"uid\":%"PRIu64",\"boot\":%u,\"present\":%s,\"lifecycle\":%d,\"role\":\"%s\",\"phrase\":%d,\"activity\":%.8g,\"committedActivity\":%.8g,\"volume\":%.8g,\"battery\":%d,\"transport\":\"%s\",\"loading\":%s,\"rolePreparing\":%d,\"pendingPhrase\":%d,\"roleError\":%s,\"outputFault\":%s,\"dataFault\":%s,\"motifAvailable\":%s,\"syncFail\":%s,\"syncing\":%s,\"members\":%u,\"feedback\":\"%s\",\"rejected\":%u,\"edges\":[",
        s.revision,now,uid,boot,d->lifecycle!=ECHOY_OFF?"true":"false",d->lifecycle,roles[d->role],d->phrase,d->activity,d->committed_activity,d->volume,d->battery,transports[d->transport],d->loading?"true":"false",d->preparing_role,d->pending_phrase,d->role_error?"true":"false",d->output_fault?"true":"false",d->data_fault?"true":"false",d->motif_available?"true":"false",control.protocol.degraded?"true":"false",control.protocol.syncing?"true":"false",control.protocol.committed.member_count,echoy_feedback_key(&s,0),rejected);
    bool comma=false;for(int a=0;a<3;a++)for(int b=0;b<3;b++)if(s.committed_edges&(1u<<(a*3+b))){printf("%s[%"PRIu64",%"PRIu64"]",comma?",":"",s.devices[a].uid,s.devices[b].uid);comma=true;}
    printf("],\"packets\":[");for(size_t i=0;i<packet_count;i++){printf("%s[%"PRIu64",\"",i?",":"",packets[i].to);for(size_t j=0;j<packets[i].n;j++)printf("%02x",packets[i].bytes[j]);fputs("\"]",stdout);}packet_count=0;
    fputs("],\"pcm\":\"",stdout);if(frame){for(size_t i=0;i<pcm_frames*2;i++)printf("%02x%02x",(unsigned)pcm[i]&255,((uint16_t)pcm[i]>>8)&255);pcm_frames=0;}
    puts("\"}");fflush(stdout);
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    uid=strtoull(argv[1],NULL,10);if(uid<100||uid>102)return 2;frame_path=argv[2];
    echoy_settings_t settings;echoy_settings_defaults(&settings);settings.role=(uint8_t)(uid-100);settings.activity[settings.role]=settings.role==0?.58:settings.role==1?.56:.48;
    echoy_control_init(&control,uid,boot,boot+50,&settings,send_packet,NULL);
    static const int batteries[]={86,72,91};(void)event(ECHOY_BATTERY,batteries[uid-100]);
    audio=malloc(echoy_audio_size());if(!audio||!echoy_audio_init(audio,echoy_audio_size(),0))return 3;
    pc_display_init();lv_obj_t *screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0x20231e),0);lv_obj_set_style_bg_opa(screen,255,0);
    lv_obj_t *surface=lv_obj_create(screen);lv_obj_set_pos(surface,0,0);lv_obj_set_size(surface,480,480);
    view=echoy_view_create(surface,emit,NULL);if(!view)return 3;
    char line[512];
    while(fgets(line,sizeof line,stdin)){
        if(!strchr(line,'\n'))return 2;
        char op=0,hex[201]={0},tail;long long a=0,b=0,c=0;double value=0;int fields=sscanf(line,"%c %lld %lld %lld %c",&op,&a,&b,&c,&tail);bool ok=true,frame=false;
        if(op=='T'&&fields==2&&a>=now&&a-now<=10000)tick(a);
        else if(op=='P'&&fields==4&&a>=0&&a<=3&&b>=-480&&b<=960&&c>=-480&&c<=960){echoy_view_update(view,&control.core,0);echoy_view_pointer(view,(echoy_pointer_t)a,(int)b,(int)c);tick(now);}
        else if(op=='C'&&fields==4&&a>=0&&b>=0&&(c==0||c==1))echoy_control_physical(&control,a,b,c,now);
        else if(op=='B'&&fields==2&&(a==0||a==1))power(a);
        else if(op=='L'&&fields==2&&(a==0||a==1))control.english=a;
        else if(op=='S'&&fields==1)frame=true;
        else if(op=='U'&&sscanf(line,"%c %lld %lf %c",&op,&a,&value,&tail)==3&&a>=0&&a<=UI_CANCEL_LOAD&&isfinite(value)&&value>=0&&value<=3){emit(NULL,(echoy_ui_event_t){(echoy_ui_action_t)a,value});tick(now);}
        else if(op=='F'&&sscanf(line,"%c %lld %lf %c",&op,&a,&value,&tail)==3&&isfinite(value)&&value>=0&&value<=100){
            switch(a){case 0:(void)event(ECHOY_DATA_LOADING,value);load_due=value?now+2600000:0;break;
            case 1:fail_next=value;break;case 2:output_fault=value;(void)event(ECHOY_OUTPUT_FAULT,value);break;
            case 3:(void)event(ECHOY_DATA_FAULT,value);break;case 4:sync_fault=value;break;
            case 5:(void)event(ECHOY_BATTERY,value);if(!value)power(false);break;
            case 6:recover();break;case 7:(void)event(ECHOY_MOVE,value);break;default:ok=false;}
            tick(now);
        }else if(op=='R'&&sscanf(line,"%c %lld %lld %200s %c",&op,&a,&b,hex,&tail)==4){
            size_t n=strlen(hex);uint8_t bytes[100];if(n%2||n>200)ok=false;
            for(size_t i=0;ok&&i<n/2;i++){unsigned v;if(sscanf(hex+2*i,"%2x",&v)!=1)ok=false;else bytes[i]=(uint8_t)v;}
            if(ok&&!echoy_control_receive(&control,a,b,bytes,n/2,now,now))rejected++;
        }else ok=false;
        if(!ok)return 2;
        reply(frame);
    }
    echoy_view_destroy(view);lv_deinit();free(audio);return ferror(stdin)?2:0;
}
