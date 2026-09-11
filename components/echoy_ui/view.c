// SPDX-License-Identifier: Apache-2.0
#include "echoy_view.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAPER 0xf1efe8
#define INK 0x242621
#define PI 3.14159265358979323846
#define POOL 112
struct echoy_view {
    lv_obj_t *screen,*objects[POOL],*labels[POOL],*plot; lv_group_t *focus; lv_timer_t *timer; lv_obj_t *progress; int progress_kind; bool reduced; uint32_t snapshot_tick; unsigned objects_used,labels_used;
    echoy_snapshot_t snapshot; int device; bool english;
    echoy_touch_t touch; echoy_route_t requested_route; int interaction_key; bool interaction_valid; echoy_ui_emit_fn emit; void *context;
    echoy_event_t hit; int64_t hit_us,now_us,last_frame; bool has_hit;
    bool presented; uint64_t hit_boot,hit_session,hit_clock; int64_t presented_us,hit_bar; int hit_device;
};
static const uint32_t colors[]={0xd95535,0x496da1,0x7b8452,0xaa7b3d};
static const char *roles[]={"pulse","voice","dust","harmony"};
static const char *icons[]={"◉","≋","⠿","≡"};
static const char *tr(echoy_view_t *v,const char *key){return echoy_ui_text(key,v->english);}
static const char *phrase(echoy_view_t *v,int role,int value){char k[32];snprintf(k,sizeof(k),"%s-%d",roles[role],value);return tr(v,k);}
static lv_obj_t *box(echoy_view_t *v,int x,int y,int w,int h,uint32_t color,int radius,int opa,uint32_t border,int bw){
    if(v->objects_used>=POOL)return NULL;
    lv_obj_t *o=v->objects[v->objects_used++];lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(o);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,opa,0);
    lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_radius(o,radius,0);lv_obj_set_style_border_color(o,lv_color_hex(border),0);lv_obj_set_style_border_width(o,bw,0);return o;
}
/* y is the CSS line box top. The mixed Latin/CJK fallback baseline is explicit. */
static lv_obj_t *label(echoy_view_t *v,int x,int y,int w,int h,int size,bool bold,uint32_t color,const char *s,lv_text_align_t align){
    if(v->labels_used>=POOL)return NULL;
    lv_obj_t *o=v->labels[v->labels_used++];lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_text_font(o,echoy_font(size,bold),0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_align(o,align,0);
    lv_obj_set_style_text_line_space(o,0,0);lv_obj_set_style_text_letter_space(o,0,0);lv_label_set_text(o,s);return o;
}
#define LEFT LV_TEXT_ALIGN_LEFT
#define CENTER LV_TEXT_ALIGN_CENTER
#define RIGHT LV_TEXT_ALIGN_RIGHT
static void finish(echoy_view_t *v){for(unsigned i=v->objects_used;i<POOL;i++)lv_obj_add_flag(v->objects[i],LV_OBJ_FLAG_HIDDEN);for(unsigned i=v->labels_used;i<POOL;i++)lv_obj_add_flag(v->labels[i],LV_OBJ_FLAG_HIDDEN);}
static const echoy_plan_entry_t *plan(echoy_view_t *v){for(size_t i=0;i<v->snapshot.plan.count;i++)if(v->snapshot.plan.devices[i].device==v->device)return &v->snapshot.plan.devices[i];return NULL;}
static bool pending(const char *k){return !strcmp(k,"waiting")||!strcmp(k,"motif-recovery")||!strcmp(k,"play-preparing")||!strcmp(k,"syncing")||!strcmp(k,"sync-waiting")||!strncmp(k,"pending-",8)||!strcmp(k,"contact-confirming")||!strcmp(k,"arrangement-pending");}
static void render(echoy_view_t *v){
    echoy_device_t *d=&v->snapshot.devices[v->device];uint32_t accent=colors[d->role];char text[320],key[40];
    v->objects_used=0;v->labels_used=0;v->progress=NULL;v->progress_kind=0;
    const char *feedback=echoy_feedback_key(&v->snapshot,v->device);bool wait=pending(feedback);
    bool status=d->lifecycle==ECHOY_BOOTING||d->loading||d->preparing_role>=0||d->role_error||d->data_fault||d->output_fault;
    v->touch.loading=d->loading||d->preparing_role>=0;
    v->touch.route=status?UI_STATUS:v->requested_route;v->touch.blocked=d->lifecycle==ECHOY_BOOTING;
    if(status||v->touch.route!=UI_PERFORM)v->touch.modal=false;
    int interaction_key=v->touch.route|(v->touch.blocked<<2)|(v->touch.modal<<3)|(v->touch.loading<<4)|(v->device<<5);
    if(v->interaction_valid&&interaction_key!=v->interaction_key&&v->touch.capture){
        echoy_ui_event_t cancelled=echoy_touch(&v->touch,UI_CANCEL,0,0);
        if(cancelled.action!=UI_NONE&&v->emit)v->emit(v->context,cancelled);
        v->touch.activity=d->activity;v->touch.volume=d->volume;
    }
    v->interaction_key=interaction_key;v->interaction_valid=true;
    lv_obj_set_style_opa_layered(v->plot,d->transport==ECHOY_PLAYING?255:115,0);
    bool perform=v->touch.route==UI_PERFORM;
    if(perform)lv_obj_remove_flag(v->plot,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(v->plot,LV_OBJ_FLAG_HIDDEN);
    if(d->lifecycle==ECHOY_BOOTING){
        label(v,0,143,480,80,55,false,accent,icons[d->role],CENTER);label(v,0,255,480,38,22,false,0x78836b,tr(v,"preparing"),CENTER);
        box(v,148,302,184,2,0xd7decd,0,255,0,0);v->progress=box(v,148,302,101,2,accent,0,255,0,0);v->progress_kind=1;
    }else if(v->touch.route==UI_STATUS){
        bool loading=v->touch.loading;label(v,38,v->english?40:46,425,70,v->english?28:26,true,INK,tr(v,loading?"preparing":"attention"),LEFT);
        box(v,38,116,59,59,loading?0xe2e8ef:0xeee0ce,16,255,0,0);
        if(loading){box(v,52,131,30,30,0xe2e8ef,15,255,0xcbd5df,2);box(v,60,131,14,2,0x6382a0,1,255,0,0);}
        else label(v,38,124,59,46,30,false,0xbd633d,"!",CENTER);
        if(d->preparing_role>=0)snprintf(text,sizeof(text),v->english?"Preparing %s":"正在准备 %s",tr(v,roles[d->preparing_role]));
        else snprintf(text,sizeof(text),"%s",tr(v,loading?"loading-data":d->role_error?"error-role":d->data_fault?"error-data":"error-audio"));
        lv_obj_t *heading=label(v,v->english?30:38,196,v->english?420:404,48,v->english?25:27,true,INK,text,LEFT);lv_obj_set_style_text_letter_space(heading,-1,0);
        if(d->role_error)snprintf(text,sizeof(text),v->english?"Kept: %s · %s":"已保留 %s · %s",tr(v,roles[d->role]),phrase(v,d->role,d->phrase));
        else snprintf(text,sizeof(text),"%s",tr(v,loading?(d->preparing_role>=0?"loading-copy":"wait"):d->data_fault?"data-copy":"audio-copy"));
        lv_obj_t *copy=label(v,v->english?42:38,251,v->english?396:404,75,v->english?17:16,false,0x808b72,text,LEFT);lv_obj_set_style_text_line_space(copy,3,0);
        if(loading){box(v,38,324,404,3,0xdce2d6,0,255,0,0);v->progress=box(v,38,324,121,3,0x718bab,0,255,0,0);v->progress_kind=1;}
        box(v,36,362,408,84,0xe2e8d8,18,255,0,0);label(v,36,390,408,40,18,false,0x536546,tr(v,loading?"cancel":d->role_error?"keep":"retry"),CENTER);
    }else if(v->touch.route==UI_ROLE){
        label(v,20,33,84,46,26,false,INK,"←",CENTER);label(v,117,v->english?40:46,327,44,v->english?28:26,true,INK,tr(v,"choose"),LEFT);
        snprintf(text,sizeof(text),v->english?"Now: %s · %s":"当前：%s · %s",tr(v,roles[d->role]),phrase(v,d->role,d->phrase));label(v,119,84,333,30,14,false,0x8a937e,text,LEFT);
        int selected=d->pending_role>=0?d->pending_role:(int)d->role;int ph=d->pending_phrase?d->pending_phrase:selected==(int)d->role?d->phrase:d->phrase_memories[selected];
        for(int i=0;i<4;i++){
            int x=36+i*105;bool sel=i==selected;
            if(sel){box(v,x,119,93,101,colors[i],16,255,colors[i],1);box(v,x+1,120,91,96,0xf7f5ef,15,255,0,0);}
            else box(v,x,119,93,101,0xe6e8de,16,255,0,0);
            label(v,x,130,93,34,26,false,colors[i],icons[i],CENTER);label(v,x,164,93,27,i==3?12:15,true,INK,tr(v,roles[i]),CENTER);
            snprintf(key,sizeof(key),"%s-detail",roles[i]);label(v,x,187,93,25,13,false,0x7b836d,tr(v,key),CENTER);
        }
        snprintf(key,sizeof(key),"%s-title",roles[selected]);label(v,38,243,180,30,16,false,0x5c6652,tr(v,key),LEFT);label(v,220,243,222,28,14,false,0x8a927e,tr(v,"choose-phrase"),RIGHT);
        for(int i=0;i<3;i++){
            int x=36+i*140;bool sel=i+1==ph;box(v,x,276,128,98,sel?0xf7f5ec:0xe5e8dc,17,255,colors[selected],sel?1:0);
            snprintf(text,sizeof(text),"%02d",i+1);label(v,x,296,128,36,122,false,sel?colors[selected]:0x8e987f,text,CENTER);
            label(v,x+8,328,112,44,v->english?17:18,false,0x535e49,phrase(v,selected,i+1),CENTER);if(sel)label(v,x+102,283,18,25,16,false,colors[selected],"•",CENTER);
        }
        echoy_ui_feedback(&v->snapshot,v->device,v->english,text,sizeof(text));label(v,38,402,404,64,v->english?14:15,false,0x8a937c,text,LEFT);
    }else{
        int title_size=v->english&&d->role==ECHOY_HARMONY?32:46;int title_y=v->english&&d->role==ECHOY_HARMONY?42:35;
        lv_obj_t *title=label(v,35,title_y,205,60,title_size,true,accent,tr(v,roles[d->role]),LEFT);lv_obj_set_style_text_letter_space(title,v->english&&d->role==ECHOY_HARMONY?-1:-2,0);
        lv_obj_t *phrase_label=label(v,39,91,260,35,v->english?20:22,false,0x66705b,phrase(v,d->role,d->phrase),LEFT);lv_obj_set_style_text_letter_space(phrase_label,v->english?0:1,0);
        echoy_ui_feedback(&v->snapshot,v->device,v->english,text,sizeof(text));box(v,38,134,5,5,accent,3,255,0,0);label(v,51,127,387,44,15,false,wait?0xa07040:0x89917e,text,LEFT);
        if(wait){box(v,40,151,400,2,0xd8dacf,0,255,0,0);v->progress=box(v,40,151,!strcmp(feedback,"contact-confirming")?140:400,2,accent,0,166,0,0);v->progress_kind=!strcmp(feedback,"contact-confirming")?1:2;}
        /* Speaker icon and battery are drawn geometry, independent of font coverage. */
        snprintf(text,sizeof(text),"%d%%",d->battery);const char *battery_text=d->battery<15?tr(v,"low"):text;lv_point_t battery_size;lv_text_get_size(&battery_size,battery_text,echoy_font(113,false),0,0,100,LV_TEXT_FLAG_NONE);int battery_width=battery_size.x<32?32:battery_size.x;int battery_x=444-battery_width-32;uint32_t battery_color=d->battery<15?0xb44d30:0x7e8873;
        box(v,351,48,1,24,0xc4cabd,0,255,0,0);box(v,battery_x,53,24,14,PAPER,3,255,battery_color,1);box(v,battery_x+26,57,2,6,battery_color,1,255,0,0);box(v,battery_x+4,57,(int)(17*d->battery/100.),6,battery_color,1,255,0,0);label(v,444-battery_width,50,battery_width,24,113,false,battery_color,battery_text,RIGHT);
        label(v,360,154,54,30,v->english?15:16,false,0x647158,tr(v,"rich"),CENTER);label(v,360,348,54,30,v->english?15:16,false,0x647158,tr(v,"simple"),CENTER);
        box(v,385,188,3,150,0xd3d8c9,2,255,0,0);int y=(int)lround(338-150*v->touch.activity);box(v,385,y,3,338-y,accent,2,255,0,0);
        for(int i=0;i<10;i++)box(v,375,188+i*15,5,1,0xadb8a1,0,184,0,0);
        box(v,379,y-7,15,15,accent,8,255,accent,1);box(v,380,y-6,13,13,accent,7,255,PAPER,3);
        snprintf(text,sizeof(text),"%d%%",(int)lround(v->touch.activity*100));label(v,402,y-12,47,32,117,false,0x48513d,text,LEFT);
        bool playing=d->transport!=ECHOY_PAUSED||d->play_preparing;
        uint32_t playbg=d->transport==ECHOY_PLAYING?INK:d->transport==ECHOY_WAITING?0xe8decb:0xdedfd3;
        if(v->touch.pressed==UI_PLAY)playbg=0xc7cbbd;
        box(v,204,384,240,84,playbg,19,255,0,0);
        for(int button=0;button<2;button++){
            const char *symbol=button?(playing?"Ⅱ":"▶"):icons[d->role];const char *caption=tr(v,button?(playing?"pause":"play"):"role");int size=button?19:18;
            lv_point_t symbol_size,caption_size;lv_text_get_size(&symbol_size,symbol,echoy_font(18,false),0,0,200,0);lv_text_get_size(&caption_size,caption,echoy_font(size,false),0,0,240,0);
            int left=(button?204:36)+((button?240:144)-symbol_size.x-9-caption_size.x)/2;
            uint32_t color=button?(d->transport==ECHOY_PLAYING?0xf4f3e9:d->transport==ECHOY_WAITING?0x8b622f:INK):INK;
            label(v,left,416,symbol_size.x,40,18,false,color,symbol,LEFT);label(v,left+symbol_size.x+9,416,caption_size.x,42,size,false,color,caption,LEFT);
        }
        if(v->touch.modal){
            box(v,0,0,480,480,0x252d20,0,69,0,0);lv_obj_t *sheet=box(v,28,204,424,252,0xf4f3eb,29,255,0xd0d5c6,1);lv_obj_set_style_shadow_color(sheet,lv_color_hex(0x20291f),0);lv_obj_set_style_shadow_width(sheet,34,0);lv_obj_set_style_shadow_opa(sheet,51,0);lv_obj_set_style_shadow_offset_y(sheet,14,0);
            label(v,56,228,230,40,24,false,0x48513e,tr(v,"volume"),LEFT);label(v,359,229,84,50,30,false,0x89917e,"×",CENTER);
            snprintf(text,sizeof(text),"%d",(int)lround(v->touch.volume*100));lv_point_t number_size,percent_size;lv_text_get_size(&number_size,text,echoy_font(50,false),-2,0,366,0);lv_text_get_size(&percent_size,"%",echoy_font(19,false),0,0,80,0);int number_x=240-(number_size.x+6+percent_size.x)/2;
            lv_obj_t *number=label(v,number_x,275,number_size.x,65,50,false,0x495340,text,LEFT);lv_obj_set_style_text_letter_space(number,-2,0);label(v,number_x+number_size.x+6,301,percent_size.x,30,19,false,0x8b957c,"%",LEFT);
            box(v,57,380,366,4,0xccd3c0,2,255,0,0);int x=(int)lround(70+340*v->touch.volume);box(v,x-13,369,26,26,accent,13,255,0xa6b093,1);box(v,x-12,370,24,24,accent,12,255,0xf4f3eb,4);
            label(v,57,408,80,24,12,false,0x929d84,"0",LEFT);label(v,343,408,80,24,12,false,0x929d84,"100",RIGHT);
        }
    }
    int pressed=v->touch.pressed;
    if(pressed==UI_CHOOSE)box(v,36,384,144,84,INK,19,20,0,0);
    else if(pressed==UI_VOLUME_OPEN)box(v,253,18,84,84,INK,22,20,0,0);
    else if(pressed==UI_BACK)box(v,20,12,84,84,INK,20,20,0,0);
    else if(pressed>=100&&pressed<104)box(v,36+(pressed-100)*105,119,93,101,INK,16,20,0,0);
    else if(pressed>=110&&pressed<113)box(v,36+(pressed-110)*140,276,128,98,INK,17,20,0,0);
    else if(pressed==UI_RECOVER||pressed==UI_CANCEL_LOAD)box(v,36,362,408,84,INK,18,20,0,0);
    finish(v);lv_obj_invalidate(v->plot);
}
/* The following primitives port DuijuView.draw's logical 342x218 coordinate system. */
static void line(lv_layer_t *l,double x,double y,double x2,double y2,double width,uint32_t color,int opa){lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.p1.x=(int)lround(x);d.p1.y=(int)lround(y);d.p2.x=(int)lround(x2);d.p2.y=(int)lround(y2);d.width=(int)lround(width);d.color=lv_color_hex(color);d.opa=opa;d.round_start=1;d.round_end=1;lv_draw_line(l,&d);}
static void disc(lv_layer_t *l,double x,double y,double r,uint32_t color,int opa){lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=opa;d.radius=LV_RADIUS_CIRCLE;lv_area_t a={(int)lround(x-r),(int)lround(y-r),(int)lround(x+r)-1,(int)lround(y+r)-1};lv_draw_rect(l,&d,&a);}
static void circle(lv_layer_t *l,int x,int y,double r,int width,uint32_t color,int opa){lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.center.x=x;d.center.y=y;d.radius=(int)lround(r+width*.5);d.width=width;d.color=lv_color_hex(color);d.opa=opa;d.start_angle=0;d.end_angle=360;lv_draw_arc(l,&d);}
static void canvas_text(echoy_view_t *v,lv_layer_t *l,int x,int baseline,int w,int size,const char *s,uint32_t color,lv_text_align_t align){lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=echoy_font(size+100,false);d.color=lv_color_hex(color);d.text=s;d.text_local=1;d.align=align;lv_area_t a={x,baseline-(d.font->line_height-d.font->base_line),x+w-1,baseline+10};(void)v;lv_draw_label(l,&d,&a);}
static const echoy_event_t *at_step(const echoy_motif_t *m,int step,bool accent){if(m)for(size_t i=0;i<m->count;i++)if(m->events[i].step==step&&(!accent||m->events[i].accent))return &m->events[i];return NULL;}
static double random_grain(int n){double z=sin(n*127.1+311.7)*43758.5453;return z-floor(z);}
static void triangle(lv_layer_t *l,double x1,double y1,double x2,double y2,double x3,double y3,uint32_t color,int opa){lv_draw_triangle_dsc_t d;lv_draw_triangle_dsc_init(&d);d.p[0]=(lv_point_precise_t){x1,y1};d.p[1]=(lv_point_precise_t){x2,y2};d.p[2]=(lv_point_precise_t){x3,y3};d.color=lv_color_hex(color);d.opa=opa;lv_draw_triangle(l,&d);}
static void draw_plot(lv_event_t *event){
    echoy_view_t *v=lv_event_get_user_data(event);lv_layer_t *l=lv_event_get_layer(event);const echoy_device_t *d=&v->snapshot.devices[v->device];const echoy_plan_entry_t *p=plan(v);const echoy_motif_t *m=p?&p->motif:NULL;uint32_t accent=colors[d->role];
    double total=0;for(int i=0;i<3;i++)if(v->snapshot.sessions[i].id==d->session_id)total=(double)echoy_phase_steps(&v->snapshot.sessions[i],v->now_us);int step=(int)floor(total)%16;if(step<0)step+=16;
    bool active=d->transport==ECHOY_PLAYING&&!d->output_fault&&!d->data_fault&&!d->loading&&d->motif_available;
    double energy=active&&v->has_hit?fmax(0,1-(v->now_us-v->hit_us)/470000.):0;
    const int X=16,Y=154;
    if(d->role==ECHOY_PULSE){
        for(int r=39;r<=79;r+=20)circle(l,X+174,Y+88,r,1,0xd3d7c9,255);
        int hits=0;for(int i=0;i<16;i++){double theta=i/16.*2*PI-PI/2;bool hit=at_step(m,i,true)!=NULL;hits+=hit;line(l,X+174+cos(theta)*75,Y+88+sin(theta)*75,X+174+cos(theta)*(hit?88:82),Y+88+sin(theta)*(hit?88:82),hit?4:1.5,hit?accent:0xb9c0ae,255);if(active&&step==i)disc(l,X+174+cos(theta)*79,Y+88+sin(theta)*79,6,accent,255);}
        disc(l,X+174,Y+88,25+energy*4,0x363b30,255);disc(l,X+174,Y+88,6+energy*3,accent,255);if(energy>0)circle(l,X+174,Y+88,29+(1-energy)*45,1,accent,(int)(energy*.4*255));
        canvas_text(v,l,X+16,Y+73,90,10,tr(v,"perc"),0x9ca58e,LEFT);canvas_text(v,l,X+16,Y+89,90,10,"01—16",0x9ca58e,LEFT);canvas_text(v,l,X+220,Y+73,104,10,tr(v,active?"run":"ready"),0x9ca58e,RIGHT);char t[40];snprintf(t,sizeof(t),v->english?"%02d HITS":"%02d 重音",hits);canvas_text(v,l,X+220,Y+89,104,10,t,0x9ca58e,RIGHT);
    }else if(d->role==ECHOY_VOICE){
        const double amps[]={.58,.84,1,.9,.76,.5,.36};double frequency=((v->has_hit?v->hit.note:65)-50)/18.;
        for(int row=0;row<7;row++){double px=0,py=0;for(int x=28;x<=316;x+=2){double u=(x-28)/288.,envelope=pow(sin(u*PI),1.6),wave=sin(u*PI*(3+frequency)+row*.22)*envelope*(25+v->touch.activity*20)*amps[row],y=48+row*12+wave+energy*7*sin(u*PI*4);if(x>28)line(l,X+px,Y+py,X+x,Y+y,row==2?2:1.1,row==2||row==3?accent:row<2?0xa8b7bc:0xbec6bd,255);px=x;py=y;}}
        if(m)for(size_t i=0;i<m->count;i++){const echoy_event_t *ev=&m->events[i];if(!ev->accent)continue;double x=28+ev->step/15*288,y=143-fmod(ev->note-60,14)*1.7;line(l,X+x,Y+y,X+x,Y+162,1,0xc0c9be,255);disc(l,X+x,Y+y,active&&step==ev->step?5:2.5,accent,255);}
        canvas_text(v,l,X+24,Y+9,150,9,tr(v,"minor"),0x97a08c,LEFT);canvas_text(v,l,X+180,Y+9,140,9,tr(v,p&&p->source>=0?"response":"phrase"),0x97a08c,RIGHT);
    }else if(d->role==ECHOY_HARMONY){
        double now=fmod(fmod(total,16)+16,16);const echoy_event_t *current=m&&m->count?&m->events[0]:NULL;bool held=false;
        if(m)for(size_t i=0;i<m->count;i++){const echoy_event_t *e=&m->events[i];if(e->step<=now&&e->step+e->dur/.15625>now){current=e;held=active;}}
        double tones[]={50,57,65};if(current&&current->harmony_count)for(int i=0;i<3;i++)tones[i]=current->harmony[i<(int)current->harmony_count?i:0];
        for(int row=0;row<3;row++){double center=50+row*29,arch=16+fmod(tones[row],12)*.5+(held&&!v->reduced?sin(total*.24+row*.6)*2.5:0),ptop=0,pbottom=0;
            for(int x=34;x<=308;x+=2){double u=(x-34)/274.,curve=sin(PI*u),top=center-curve*arch+sin(u*PI*2+row*.3)*5,bottom=center+curve*(7+v->touch.activity*5)+sin(u*PI*2+row*.3)*5;
                if(x>34){triangle(l,X+x-2,Y+ptop,X+x,Y+top,X+x,Y+bottom,accent,held?26:14);triangle(l,X+x-2,Y+ptop,X+x,Y+bottom,X+x-2,Y+pbottom,accent,held?26:14);line(l,X+x-2,Y+ptop,X+x,Y+top,held?1.8:1.2,row==1?accent:0xb7a587,255);line(l,X+x-2,Y+pbottom,X+x,Y+bottom,held?1.8:1.2,row==1?accent:0xb7a587,255);}ptop=top;pbottom=bottom;}
            double x=60+row*108,u=(x-34)/274.,y=center-sin(PI*u)*arch+sin(u*PI*2+row*.3)*5;disc(l,X+x,Y+y,held?3.5:2.5,accent,255);
        }
        if(m)for(size_t i=0;i<m->count;i++){const echoy_event_t *e=&m->events[i];double x=28+e->step/16*290,len=fmax(4,fmin(16-e->step,e->dur/.15625)/16*290);for(size_t j=0;j<(e->notes_count?e->notes_count:1);j++)line(l,X+x,Y+152+j*5,X+x+len,Y+152+j*5,2,accent,active&&step>=e->step&&step<e->step+e->dur/.15625?204:92);}
        const char *phrases[]={"","staccato","arpeggio","answer"};canvas_text(v,l,X+24,Y+9,150,9,tr(v,"minor"),0x998d76,LEFT);canvas_text(v,l,X+180,Y+9,140,9,tr(v,phrases[d->phrase]),0x998d76,RIGHT);
    }else{
        for(int i=0;i<135;i++){double u=random_grain(i+1),z=random_grain(i+54),x=28+u*288,center=83+sin(u*7)*13,spread=35*sin(u*PI)+10,y=center+(z-.5)*spread*2;int s=(int)floor(u*16);bool ev=at_step(m,s,false)!=NULL,hot=active&&step==s;double radius=(ev?1.7:1)*(hot?1.7:1)+random_grain(i+8)*.6;disc(l,X+x,Y+y,radius,hot||ev?accent:0xbbc4a8,(int)((ev?.6+z*.4:.42+z*.3)*255));}
        line(l,X+24,Y+149,X+320,Y+149,1,0xd8ddce,255);canvas_text(v,l,X+24,Y+9,220,9,tr(v,"particles"),0x96a185,LEFT);canvas_text(v,l,X+250,Y+9,70,9,tr(v,"air"),0x96a185,RIGHT);
    }
    for(int i=0;i<16;i++){bool ev=at_step(m,i,false)!=NULL;disc(l,X+29+i*19,Y+210,active&&step==i?3.5:ev?2.4:1.5,active&&step==i?accent:ev?0x363b30:0xccd2c1,255);}
}
static void speaker(lv_event_t *e){echoy_view_t *v=lv_event_get_user_data(e);if(v->touch.route!=UI_PERFORM)return;lv_layer_t *l=lv_event_get_layer(e);const double p[][2]={{293,52},{288,56},{284,56},{284,63},{288,63},{293,68},{293,52}};for(int i=1;i<7;i++)line(l,p[i-1][0],p[i-1][1],p[i][0],p[i][1],2,0x536047,255);line(l,299,55,302,59,2,0x536047,255);line(l,302,59,299,64,2,0x536047,255);line(l,303,52,307,59,2,0x536047,255);line(l,307,59,303,67,2,0x536047,255);}
echoy_ui_event_t echoy_view_hit_test(const echoy_view_t *v,int x,int y){echoy_touch_t t=v->touch;t.capture=0;t.pressed=0;echoy_ui_event_t e=echoy_touch(&t,UI_DOWN,x,y);return e.action!=UI_NONE?e:echoy_touch(&t,UI_UP,x,y);}
void echoy_view_pointer(echoy_view_t *v,echoy_pointer_t kind,int x,int y){
    echoy_ui_event_t e=echoy_touch(&v->touch,kind,x,y);
    if(e.action==UI_CHOOSE)v->requested_route=UI_ROLE;
    if(e.action==UI_BACK)v->requested_route=UI_PERFORM;
    if(e.action==UI_VOLUME_OPEN)v->touch.modal=true;
    if(e.action==UI_VOLUME_CLOSE)v->touch.modal=false;
    if(e.action!=UI_NONE&&v->emit)v->emit(v->context,e);
    render(v);
}
static void key_event(lv_event_t *e){
    echoy_view_t *v=lv_event_get_user_data(e);uint32_t key=lv_event_get_key(e);echoy_ui_event_t out={UI_NONE,0};
    if(v->touch.blocked)return;
    if(v->touch.modal){
        if(key==LV_KEY_ESC){out.action=UI_VOLUME_CLOSE;v->touch.modal=false;}
        else if(key==LV_KEY_LEFT||key==LV_KEY_RIGHT||key==LV_KEY_UP||key==LV_KEY_DOWN){out.action=UI_SET_VOLUME;out.value=fmin(1,fmax(0,v->touch.volume+((key==LV_KEY_RIGHT||key==LV_KEY_UP)?.05:-.05)));v->touch.volume=out.value;}
        /* Enter / Tab cannot activate the obscured transport or role page. */
    }else if(key==LV_KEY_ESC&&v->requested_route==UI_ROLE){out.action=UI_BACK;v->requested_route=UI_PERFORM;}
    if(out.action!=UI_NONE&&v->emit)v->emit(v->context,out);
    render(v);
}
static void pointer_event(lv_event_t *e){echoy_view_t *v=lv_event_get_user_data(e);lv_indev_t *i=lv_indev_active();if(!i)return;lv_point_t p;lv_indev_get_point(i,&p);lv_event_code_t code=lv_event_get_code(e);echoy_view_pointer(v,code==LV_EVENT_PRESSED?UI_DOWN:code==LV_EVENT_PRESSING?UI_MOVE:code==LV_EVENT_RELEASED?UI_UP:UI_CANCEL,p.x,p.y);}
static void timer_frame(lv_timer_t *timer){echoy_view_t *v=lv_timer_get_user_data(timer);echoy_view_frame(v,v->snapshot.time_us+(int64_t)(uint32_t)(lv_tick_get()-v->snapshot_tick)*1000);}
echoy_view_t *echoy_view_create(lv_obj_t *screen,echoy_ui_emit_fn emit,void *context){
    echoy_view_t *v=lv_malloc_zeroed(sizeof(*v));if(!v)return NULL;v->screen=screen;v->emit=emit;v->context=context;
    lv_obj_remove_style_all(screen);lv_obj_set_size(screen,480,480);lv_obj_set_style_bg_color(screen,lv_color_hex(PAPER),0);lv_obj_set_style_bg_opa(screen,255,0);lv_obj_set_style_radius(screen,60,0);lv_obj_set_style_clip_corner(screen,true,0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(screen,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);
    v->plot=lv_obj_create(screen);lv_obj_remove_style_all(v->plot);lv_obj_remove_flag(v->plot,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_pos(v->plot,16,154);lv_obj_set_size(v->plot,342,218);lv_obj_add_event_cb(v->plot,draw_plot,LV_EVENT_DRAW_MAIN,v);
    lv_obj_add_event_cb(screen,speaker,LV_EVENT_DRAW_MAIN_END,v);
    /* Pool objects are allocated once; all labels are above rectangles. */
    for(int i=0;i<POOL;i++){v->objects[i]=lv_obj_create(screen);lv_obj_remove_style_all(v->objects[i]);lv_obj_remove_flag(v->objects[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(v->objects[i],LV_OBJ_FLAG_HIDDEN);}
    for(int i=0;i<POOL;i++){v->labels[i]=lv_label_create(screen);lv_obj_remove_style_all(v->labels[i]);lv_obj_remove_flag(v->labels[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_label_set_long_mode(v->labels[i],LV_LABEL_LONG_WRAP);lv_obj_add_flag(v->labels[i],LV_OBJ_FLAG_HIDDEN);}
    v->focus=lv_group_create();lv_group_add_obj(v->focus,screen);lv_group_focus_obj(screen);lv_obj_add_event_cb(screen,key_event,LV_EVENT_KEY,v);
    lv_obj_add_event_cb(screen,pointer_event,LV_EVENT_PRESSED,v);lv_obj_add_event_cb(screen,pointer_event,LV_EVENT_PRESSING,v);lv_obj_add_event_cb(screen,pointer_event,LV_EVENT_RELEASED,v);lv_obj_add_event_cb(screen,pointer_event,LV_EVENT_PRESS_LOST,v);
    v->timer=lv_timer_create(timer_frame,34,v);return v;
}
void echoy_view_set_sink(echoy_view_t *v,echoy_ui_emit_fn emit,void *context){v->emit=emit;v->context=context;}
/* The display observes actual events from its immutable plan, using the same
 * session-to-monotonic mapping as audio. Never synthesize a hit from a motif. */
static void present_hits(echoy_view_t *v){
    const echoy_plan_entry_t *p=plan(v);int64_t now=v->snapshot.time_us;
    const echoy_device_t *d=&v->snapshot.devices[v->device];
    bool changed=!v->presented||!p||v->hit_device!=v->device||v->hit_boot!=p->boot_id||
        v->hit_session!=p->session_id||v->hit_clock!=p->clock_version;
    bool bar_changed=p&&v->presented&&p->bar!=v->hit_bar;
    int64_t since=v->presented?v->presented_us:now-470000;
    if(changed||bar_changed)v->has_hit=false;
    if(changed&&v->presented)since=now-1;
    if(bar_changed&&p)since=(int64_t)ceill(p->epoch_us+(long double)p->bar*ECHOY_BAR_US*1000000/(1000000+p->rate_ppm))-1;
    if(since<now-470000)since=now-470000;
    bool audible=p&&d->lifecycle==ECHOY_READY&&d->transport==ECHOY_PLAYING&&
        d->volume>0&&!d->output_fault&&!d->loading&&!d->data_fault&&p->motif_available;
    if(!audible)v->has_hit=false;
    else for(size_t i=0;i<p->events.count;i++){
        const echoy_event_t *e=&p->events.events[i];
        int64_t at=(int64_t)ceill(p->epoch_us+((long double)p->bar*ECHOY_BAR_US+e->step*156250.0L)*1000000/(1000000+p->rate_ppm));
        if(at>since&&at<=now){echoy_view_hit(v,e,at);since=at;}
    }
    v->presented=true;v->presented_us=now;v->hit_device=v->device;
    if(p){v->hit_boot=p->boot_id;v->hit_session=p->session_id;v->hit_clock=p->clock_version;v->hit_bar=p->bar;}
}
void echoy_view_update(echoy_view_t *v,const echoy_snapshot_t *s,int id){if(!v||!s||id<0||id>=3)return;
    if(v->presented&&id==v->device&&s->devices[id].boot_id==v->snapshot.devices[id].boot_id&&
       (s->time_us<v->snapshot.time_us||s->devices[id].clock_version<v->snapshot.devices[id].clock_version))return;
    v->snapshot=*s;v->snapshot_tick=lv_tick_get();v->device=id;v->now_us=s->time_us;if(!v->touch.capture){v->touch.activity=s->devices[id].activity;v->touch.volume=s->devices[id].volume;}v->touch.modal=s->devices[id].volume_open;present_hits(v);render(v);}
void echoy_view_locale(echoy_view_t *v,bool en){v->english=en;render(v);}
void echoy_view_route(echoy_view_t *v,echoy_route_t route){v->requested_route=route;render(v);}
void echoy_view_motion(echoy_view_t *v,bool reduced){v->reduced=reduced;}
void echoy_view_hit(echoy_view_t *v,const echoy_event_t *event,int64_t at_us){v->hit=*event;v->hit_us=at_us;v->has_hit=true;}
void echoy_view_frame(echoy_view_t *v,int64_t now){if(now-v->last_frame<33333)return;v->now_us=now;v->last_frame=now;lv_obj_invalidate(v->plot);
    if(v->progress&&!v->reduced){
        if(v->progress_kind==1){int width=v->snapshot.devices[v->device].lifecycle==ECHOY_BOOTING?184:400;double amount=.3+.25*(1+sin(now/1000000.*2*PI));lv_obj_set_width(v->progress,(int)(width*amount));}
        else if(v->progress_kind==2){const echoy_device_t *d=&v->snapshot.devices[v->device];double remaining=1;for(int i=0;i<3;i++)if(v->snapshot.sessions[i].id==d->session_id){double total=(double)echoy_phase_steps(&v->snapshot.sessions[i],now),period=d->transport==ECHOY_WAITING?4:16;remaining=1-fmod(fmod(total,period)+period,period)/period;}if(v->snapshot.sync.stage==ECHOY_SYNC_WAITING)remaining=fmin(1,fmax(0,(v->snapshot.sync.apply_at_us-now)/2500000.));lv_obj_set_width(v->progress,(int)(400*remaining));}
    }
}
void echoy_view_destroy(echoy_view_t *v){if(!v)return;lv_timer_delete(v->timer);lv_obj_remove_event_cb_with_user_data(v->screen,pointer_event,v);lv_obj_remove_event_cb_with_user_data(v->screen,key_event,v);lv_group_delete(v->focus);lv_obj_remove_event_cb_with_user_data(v->screen,speaker,v);lv_obj_delete(v->plot);for(int i=0;i<POOL;i++){lv_obj_delete(v->objects[i]);lv_obj_delete(v->labels[i]);}lv_free(v);}
