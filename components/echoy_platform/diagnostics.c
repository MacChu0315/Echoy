// SPDX-License-Identifier: Apache-2.0
#include "echoy_diagnostics.h"
#include <inttypes.h>
#include <stdio.h>
#include <math.h>
bool echoy_diagnostics_format(const echoy_control_t *c,const echoy_diagnostics_t *d,char *b,size_t cap,size_t *n){
    if(n)*n=0;
    if(!c||!d||!b||!cap||!n)return false;
    const echoy_protocol_t *p=&c->protocol;const echoy_device_t *local=&c->core.devices[0];
    int length=snprintf(b,cap,"{\"schema\":1,\"wire_uid\":\"%016" PRIx64 "\",\"wire_boot\":%" PRIu32 ",\"iris_id\":\"%.32s\",\"iris_boot\":\"%016" PRIx64 "\","
        "\"session\":{\"creator\":\"%016" PRIx64 "\",\"boot\":%" PRIu32 ",\"nonce\":%" PRIu32 ",\"members\":%u},\"state_revision\":%" PRIu64 ",\"wire_revision\":%" PRIu32 ",\"active_edges\":%u,\"pending_edges\":%u,"
        "\"sync\":{\"stage\":%d,\"pending\":%s,\"degraded\":%s},\"clock\":[{\"uid\":\"%016" PRIx64 "\",\"boot\":%" PRIu32 ",\"offset_us\":%.3f,\"rate_ppm\":%.3f,\"uncertainty_us\":%.3f,\"lost\":%s},{\"uid\":\"%016" PRIx64 "\",\"boot\":%" PRIu32 ",\"offset_us\":%.3f,\"rate_ppm\":%.3f,\"uncertainty_us\":%.3f,\"lost\":%s}],"
        "\"audio\":{\"output_fault\":%s,\"error\":%d,\"queue_failures\":%u,\"gaps\":%" PRIu64 ",\"started\":%" PRIu64 ",\"active\":%u,\"voice_peak\":%u,\"pending\":%u,\"missed\":%" PRIu64 ",\"capacity_errors\":%" PRIu64 ",\"clipped_samples\":%" PRIu64 "},\"battery\":{\"percent\":%d,\"error\":%d,\"failures\":%u},\"settings\":{\"error\":%d,\"failures\":%u},"
        "\"psram_free\":%u,\"ui_updates\":%u,\"touch_processed\":%u,\"touch_dropped\":%u,\"touch_cleanups\":%u,\"touch_cancel_pending\":%s,\"plan_rejections\":%" PRIu64 ",\"max_render_write_us\":%" PRId64 ",\"heap_free\":%u,\"owner_stack_free\":%u,\"ui_error\":%d,\"imu_error\":%d,\"modules\":[{\"error\":%d,\"type\":%d,\"state\":%d},{\"error\":%d,\"type\":%d,\"state\":%d}],\"drops\":{\"radio\":%u,\"input\":%u,\"publish\":%u},\"rejects\":{\"owner\":%u,\"protocol\":%u,\"capacity\":%u,\"send\":%u,\"contact\":%u},\"device\":{\"role\":%d,\"phrase\":%d,\"pending_role\":%d,\"pending_phrase\":%d,\"activity\":%.6f,\"committed_activity\":%.6f,\"volume\":%.6f,\"transport\":%d,\"lifecycle\":%d,\"locale\":%d},\"injection_enabled\":%s}",
        p->uid,p->boot,d->iris_id,d->iris_boot,p->session.creator_uid,p->session.creator_boot,p->session.nonce,p->committed.member_count,
        c->core.revision,p->committed.revision,c->core.committed_edges,p->pending.active?p->pending.state.edge_count:0,c->core.sync.stage,p->pending.active?"true":"false",p->degraded?"true":"false",
        p->peers[0].uid,p->peers[0].boot,p->peers[0].clock.offset_us,p->peers[0].clock.rate*1e6,p->peers[0].clock.uncertainty_us,p->peers[0].lost?"true":"false",
        p->peers[1].uid,p->peers[1].boot,p->peers[1].clock.offset_us,p->peers[1].clock.rate*1e6,p->peers[1].clock.uncertainty_us,p->peers[1].lost?"true":"false",
        local->output_fault?"true":"false",d->audio_error,d->audio_queue_failures,d->output_gaps,d->audio.started,d->audio.active,d->audio.peak_voices,d->audio.pending,d->audio.missed,d->audio.capacity_errors,d->audio.clipped_samples,local->battery,d->battery_error,d->battery_failures,d->settings_error,d->settings_failures,
        d->psram_free,d->ui_updates,d->touch_processed,d->touch_dropped,d->touch_cleanups,d->touch_cancel_pending?"true":"false",d->plan_rejections,d->max_render_write_us,d->heap_free,d->stack_free,d->ui_error,d->imu_error,d->module_error[0],d->module_type[0],d->module_state[0],d->module_error[1],d->module_type[1],d->module_state[1],d->radio_dropped,c->dropped,c->publication_failures,c->rejected,p->rejected,p->queue_full,p->send_failures,d->contact_errors,local->role,local->phrase,local->pending_role,local->pending_phrase,local->activity,local->committed_activity,local->volume,local->transport,local->lifecycle,c->english,d->injection_enabled?"true":"false");
    if(length<0||(size_t)length>=cap){b[0]=0;return false;}*n=(size_t)length;return true;
}
bool echoy_diagnostics_injection(const uint8_t *p,size_t n,bool enabled,echoy_ui_event_t *out){
    if(!enabled||!p||!out||n!=3)return false;
    unsigned value=(unsigned)p[1]|((unsigned)p[2]<<8);double v=value;
    switch(p[0]){
    case UI_PLAY:case UI_FINISH_ACTIVITY:case UI_RECOVER:case UI_CANCEL_LOAD:if(value)return false;break;
    case UI_ROLE_SELECT:if(value>3)return false;break;
    case UI_PHRASE_SELECT:if(value<1||value>3)return false;break;
    case UI_SET_ACTIVITY:case UI_SET_VOLUME:if(value>1000)return false;v=value/1000.0;break;
    default:return false;
    }
    *out=(echoy_ui_event_t){(echoy_ui_action_t)p[0],v};return true;
}
bool echoy_diagnostics_touch(const uint8_t *p,size_t n,bool enabled,echoy_pointer_t *phase,int *x,int *y){
    if(!enabled||!p||n!=5||!phase||!x||!y||p[0]>UI_CANCEL)return false;
    unsigned px=p[1]|((unsigned)p[2]<<8),py=p[3]|((unsigned)p[4]<<8);
    if(px>=480||py>=480)return false;
    *phase=(echoy_pointer_t)p[0];*x=(int)px;*y=(int)py;return true;
}
