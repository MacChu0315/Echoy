// SPDX-License-Identifier: Apache-2.0
#include "echoy_services.h"
#include "echoy_wire.h"
#include <math.h>
#include <string.h>
void echoy_settings_defaults(echoy_settings_t *s){
    *s=(echoy_settings_t){.phrase=1,.volume=.7,.activity={.58,.56,.48,.5},.phrases={1,1,1,2}};
}
bool echoy_settings_valid(const echoy_settings_t *s){
    if(!s||s->role>3||s->phrase<1||s->phrase>3||s->locale>1||!isfinite(s->volume)||s->volume<0||s->volume>1)return false;
    for(int i=0;i<4;i++)if(!isfinite(s->activity[i])||s->activity[i]<0||s->activity[i]>1||s->phrases[i]<1||s->phrases[i]>3)return false;
    return s->phrases[s->role]==s->phrase;
}
void echoy_settings_capture(echoy_settings_t *s,const echoy_device_t *d,bool en){
    memset(s,0,sizeof *s);s->role=d->role;s->phrase=d->phrase;s->volume=d->volume;s->locale=en;
    for(int i=0;i<4;i++){s->activity[i]=d->memories[i];s->phrases[i]=d->phrase_memories[i];}
    s->activity[d->role]=d->committed_activity;s->phrases[d->role]=d->phrase;
}
bool echoy_settings_restore(const echoy_settings_t *s,echoy_device_t *d){
    if(!d||!echoy_settings_valid(s))return false;
    d->role=s->role;d->phrase=s->phrase;d->volume=s->volume;
    for(int i=0;i<4;i++){d->memories[i]=s->activity[i];d->phrase_memories[i]=s->phrases[i];}
    d->activity=d->committed_activity=s->activity[s->role];d->transport=ECHOY_PAUSED;
    d->session_id=0;d->pending_role=d->preparing_role=-1;d->pending_phrase=0;d->play_preparing=false;return true;
}
/* Fixed little-endian schema, quantized to millionths; no ABI padding or runtime state. */
static void encode(const echoy_settings_t *s,uint8_t b[64]){
    memset(b,0,64);b[0]=1;b[1]=s->role;b[2]=s->phrase;b[3]=s->locale;
    echoy_put_le(b+4,(uint32_t)llround(s->volume*1000000),4);
    for(int i=0;i<4;i++){echoy_put_le(b+8+4*i,(uint32_t)llround(s->activity[i]*1000000),4);b[24+i]=s->phrases[i];}
    echoy_put_le(b+60,echoy_wire_digest(b,60),4);
}
static bool equal(const echoy_settings_t *a,const echoy_settings_t *b){uint8_t x[64],y[64];encode(a,x);encode(b,y);return !memcmp(x,y,64);}
bool echoy_storage_load(echoy_storage_t *s){
    if(!s)return false;
    echoy_settings_defaults(&s->saved);s->dirty=false;uint8_t b[64];size_t n=sizeof b;
    int err=s->read?s->read(s->ctx,b,&n):-1;
    if(!err&&(n!=64||b[0]!=1||echoy_get_le(b+60,4)!=echoy_wire_digest(b,60)))err=-2;
    echoy_settings_t v={0};
    if(!err){v.role=b[1];v.phrase=b[2];v.locale=b[3];v.volume=echoy_get_le(b+4,4)/1000000.0;
        for(int i=0;i<4;i++){v.activity[i]=echoy_get_le(b+8+4*i,4)/1000000.0;v.phrases[i]=b[24+i];}
        for(int i=28;i<60;i++)if(b[i])err=-2;
        if(!echoy_settings_valid(&v))err=-2;}
    s->last_error=err;if(err){s->failures++;return false;}s->saved=v;return true;
}
bool echoy_storage_stage(echoy_storage_t *s,const echoy_settings_t *v,int64_t now){
    if(!s||now<0||now>INT64_MAX-ECHOY_SETTINGS_DEBOUNCE_US||!echoy_settings_valid(v))return false;
    if(equal(v,&s->saved)){s->dirty=false;return true;}
    if(!s->dirty||!equal(v,&s->pending)){s->pending=*v;s->due_us=now+ECHOY_SETTINGS_DEBOUNCE_US;s->dirty=true;}return true;
}
bool echoy_storage_flush(echoy_storage_t *s,int64_t now){
    if(!s)return false;
    if(!s->dirty||now<s->due_us)return true;
    uint8_t b[64];encode(&s->pending,b);int err=s->write?s->write(s->ctx,b,sizeof b):-1;s->last_error=err;
    if(err){s->failures++;s->due_us=now+ECHOY_SETTINGS_DEBOUNCE_US;return false;}
    s->saved=s->pending;s->dirty=false;return true;
}
