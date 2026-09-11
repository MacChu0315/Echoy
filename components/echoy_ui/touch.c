// SPDX-License-Identifier: Apache-2.0
#include "echoy_touch.h"
static double clamp(double v){return v<0?0:v>1?1:v;}
static bool in(int x,int y,int l,int t,int w,int h){return x>=l&&x<l+w&&y>=t&&y<t+h;}
static int hit(echoy_touch_t *t,int x,int y){
    if(t->blocked)return UI_NONE;
    if(t->modal){
        if(in(x,y,359,210,84,84))return UI_VOLUME_CLOSE;
        if(in(x,y,57,340,366,84))return UI_SET_VOLUME;
        return in(x,y,28,204,424,252)?UI_NONE:UI_VOLUME_CLOSE;
    }
    if(t->route==UI_STATUS)return in(x,y,36,362,408,84)?(t->loading?UI_CANCEL_LOAD:UI_RECOVER):UI_NONE;
    if(t->route==UI_ROLE){
        if(in(x,y,20,12,84,84))return UI_BACK;
        for(int i=0;i<4;i++)if(in(x,y,36+i*105,119,93,101))return 100+i;
        for(int i=0;i<3;i++)if(in(x,y,36+i*140,276,128,98))return 110+i;
        return UI_NONE;
    }
    if(in(x,y,253,18,84,84))return UI_VOLUME_OPEN;
    if(in(x,y,36,384,144,84))return UI_CHOOSE;
    if(in(x,y,204,384,240,84))return UI_PLAY;
    if(in(x,y,26,154,322,218))return UI_SET_ACTIVITY;
    if(in(x,y,360,154,96,218))return 120;
    return UI_NONE;
}
echoy_ui_event_t echoy_touch(echoy_touch_t *t,echoy_pointer_t p,int x,int y){
    echoy_ui_event_t e={UI_NONE,0};
    if(p==UI_DOWN){
        if(t->capture)return e;
        t->pressed=hit(t,x,y); t->capture=t->pressed; t->start_y=y;t->start_value=t->activity;
        if(t->capture!=120&&t->capture!=UI_SET_VOLUME)return e;
    }
    if(p==UI_DOWN||p==UI_MOVE){
        if(t->capture==UI_SET_ACTIVITY||t->capture==120){e.action=UI_SET_ACTIVITY;e.value=clamp(t->capture==120?(338.-y)/150:t->start_value+(t->start_y-y)/180.);t->activity=e.value;}
        if(t->capture==UI_SET_VOLUME){e.action=UI_SET_VOLUME;e.value=clamp((x-70.)/340);t->volume=e.value;}
        return e;
    }
    int capture=t->capture;t->capture=0;t->pressed=0;
    if(capture==UI_SET_ACTIVITY||capture==120){e.action=UI_FINISH_ACTIVITY;return e;}
    if(p==UI_CANCEL||!capture||capture==UI_SET_VOLUME||hit(t,x,y)!=capture)return e;
    if(capture>=100&&capture<104){e.action=UI_ROLE_SELECT;e.value=capture-100;}
    else if(capture>=110&&capture<113){e.action=UI_PHRASE_SELECT;e.value=capture-109;}
    else e.action=(echoy_ui_action_t)capture;
    return e;
}
