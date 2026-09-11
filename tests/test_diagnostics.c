#include "echoy_diagnostics.h"
#include <assert.h>
#include <string.h>
int main(void){static echoy_control_t c;echoy_control_init(&c,123,7,9,NULL,NULL,NULL);echoy_diagnostics_t d={.iris_id="01234567890123456789012345678901"};char b[4096];size_t n;
 assert(echoy_diagnostics_format(&c,&d,b,sizeof b,&n));assert(n==strlen(b));assert(strstr(b,"wire_uid")&&strstr(b,"clock")&&strstr(b,"audio")&&!strstr(b,"token"));assert(!echoy_diagnostics_format(&c,&d,b,8,&n));
 echoy_ui_event_t e;uint8_t p[]={UI_PLAY,0,0};assert(!echoy_diagnostics_injection(p,sizeof p,false,&e));assert(echoy_diagnostics_injection(p,sizeof p,true,&e)&&e.action==UI_PLAY);p[0]=255;assert(!echoy_diagnostics_injection(p,sizeof p,true,&e));
 uint8_t touch[]={UI_DOWN,0xdf,1,0xdf,1};echoy_pointer_t phase;int x,y;
 assert(!echoy_diagnostics_touch(touch,sizeof touch,false,&phase,&x,&y));
 assert(echoy_diagnostics_touch(touch,sizeof touch,true,&phase,&x,&y)&&phase==UI_DOWN&&x==479&&y==479);
 touch[1]=0xe0;assert(!echoy_diagnostics_touch(touch,sizeof touch,true,&phase,&x,&y));
 touch[1]=0;touch[2]=0;touch[0]=4;assert(!echoy_diagnostics_touch(touch,sizeof touch,true,&phase,&x,&y));
 touch[0]=UI_CANCEL;assert(echoy_diagnostics_touch(touch,sizeof touch,true,&phase,&x,&y));
 assert(!echoy_diagnostics_touch(touch,4,true,&phase,&x,&y));
 return 0;}
