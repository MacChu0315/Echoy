// SPDX-License-Identifier: Apache-2.0
#include "echoy_touch.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static echoy_ui_event_t click(echoy_touch_t *t,int x,int y) {
    echoy_touch(t,UI_DOWN,x,y); return echoy_touch(t,UI_UP,x,y);
}
int main(void) {
    echoy_touch_t t={.activity=.58,.volume=.7};
    assert(click(&t,36,384).action==UI_CHOOSE);
    assert(click(&t,179,467).action==UI_CHOOSE);
    assert(click(&t,180,467).action==UI_NONE);
    assert(click(&t,204,384).action==UI_PLAY);
    assert(click(&t,444,468).action==UI_NONE);
    echoy_touch(&t,UI_DOWN,100,240);
    assert(fabs(echoy_touch(&t,UI_MOVE,100,150).value-1)<1e-9);
    assert(echoy_touch(&t,UI_MOVE,-100,1000).value==0);
    assert(echoy_touch(&t,UI_CANCEL,100,0).action==UI_FINISH_ACTIVITY);
    assert(echoy_touch(&t,UI_MOVE,100,100).action==UI_NONE);
    assert(fabs(echoy_touch(&t,UI_DOWN,385,263).value-.5)<1e-9);
    assert(echoy_touch(&t,UI_UP,385,-999).action==UI_FINISH_ACTIVITY);
    t.modal=true;
    assert(click(&t,100,426).action==UI_NONE);
    assert(click(&t,400,250).action==UI_VOLUME_CLOSE);
    assert(click(&t,100,100).action==UI_VOLUME_CLOSE);
    assert(echoy_touch(&t,UI_DOWN,230,380).action==UI_SET_VOLUME);
    echoy_touch(&t,UI_CANCEL,0,0);
    assert(echoy_touch(&t,UI_MOVE,480,380).action==UI_NONE);
    t.modal=false; t.route=UI_ROLE;
    for(int i=0;i<4;i++){echoy_ui_event_t e=click(&t,36+i*105,119);assert(e.action==UI_ROLE_SELECT&&e.value==i);}
    for(int i=0;i<3;i++){echoy_ui_event_t e=click(&t,36+i*140,276);assert(e.action==UI_PHRASE_SELECT&&e.value==i+1);}
    for(int i=0;i<30;i++)assert(click(&t,20,12).action==UI_BACK);
    t.route=UI_STATUS; t.loading=true;
    assert(click(&t,36,362).action==UI_CANCEL_LOAD);
    t.loading=false;assert(click(&t,443,445).action==UI_RECOVER);
    t.blocked=true;assert(click(&t,36,362).action==UI_NONE);
    puts("UI touch edges, capture/cancel, overshoot, modal isolation, rapid back: PASS");
}
