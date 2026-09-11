// SPDX-License-Identifier: Apache-2.0
#include "state_internal.h"
#include <math.h>
#include <string.h>
static const double initial_activity[]={.58,.56,.48,.50};
static bool valid_device(int d) {return d>=0&&d<ECHOY_MAX_DEVICES;}
static void cancel_role(echoy_device_t *d) {
    d->role_generation++;d->pending_role=-1;d->preparing_role=-1;d->pending_phrase=0;
}
static void pause_device(echoy_device_t *d) {
    d->transport=ECHOY_PAUSED;d->play_preparing=false;d->play_generation++;d->play_at_us=0;d->play_session=0;
}
void echoy_core_init(echoy_core_t *c,const uint64_t uids[3]) {
    if(!c)return;
    memset(c,0,sizeof *c);
    for(int i=0;i<3;i++) {
        echoy_device_t *d=&c->devices[i];d->uid=uids?uids[i]:(uint64_t)i+1;
        d->role=(echoy_role_t)i;d->activity=d->committed_activity=initial_activity[i];
        memcpy(d->memories,initial_activity,sizeof initial_activity);
        d->phrase=1;for(int j=0;j<4;j++)d->phrase_memories[j]=j==3?2:1;
        d->volume=.7;d->battery=100;d->pending_role=d->preparing_role=-1;d->link_online=true;
    }
}
void echoy_commit_members(echoy_core_t *c,unsigned members) {
    for(int i=0;i<3;i++)if(members&MEMBER(i)) {
        echoy_device_t *d=&c->devices[i];
        if(d->pending_role>=0) {
            d->memories[d->role]=d->activity;d->phrase_memories[d->role]=d->phrase;
            d->role=(echoy_role_t)d->pending_role;d->activity=d->memories[d->role];
            d->phrase=d->phrase_memories[d->role];d->pending_role=d->preparing_role=-1;
        }
        /* A phrase chosen during preparation belongs to that transaction. */
        if(d->pending_phrase&&d->preparing_role<0) {
            if(d->phrase_role==(int)d->role)d->phrase=d->phrase_memories[d->role]=d->pending_phrase;
            d->pending_phrase=0;
        }
        d->committed_activity=d->activity;
    }
}
void echoy_commit_idle(echoy_core_t *c,int id) {
    echoy_session_t *s=echoy_session_for(c,id);if(!s||s->wire_managed)return;
    for(int i=0;i<3;i++)if((s->members&MEMBER(i))&&c->devices[i].transport!=ECHOY_PAUSED)return;
    echoy_commit_members(c,s->members);
}
static echoy_result_t request_play(echoy_core_t *c,int id) {
    echoy_device_t *d=&c->devices[id];echoy_session_t *s=echoy_session_for(c,id);
    if(!s||d->lifecycle!=ECHOY_READY||d->output_fault||d->data_fault||d->loading||!d->battery)return ECHOY_NOT_READY;
    d->transport=ECHOY_WAITING;d->play_preparing=false;
    d->play_at_us=echoy_next_boundary(s,c->time_us+25000,false);d->play_session=s->id;return ECHOY_OK;
}
static bool callback(echoy_action_t t) {
    return t==ECHOY_BOOT_READY||t==ECHOY_ROLE_READY||t==ECHOY_ROLE_FAILED||t==ECHOY_PLAY_READY||t==ECHOY_PLAY_FAILED||t==ECHOY_SYNC_READY||t==ECHOY_SYNC_FAIL;
}
static bool callback_current(const echoy_core_t *c,const echoy_input_t *e) {
    const echoy_device_t *d=&c->devices[e->device];
    if(e->type!=ECHOY_BOOT&&e->boot_id&&e->boot_id!=d->boot_id)return false;
    if(callback(e->type)&&(!e->boot_id||e->boot_id!=d->boot_id||d->lifecycle==ECHOY_OFF))return false;
    if((e->type==ECHOY_ROLE_READY||e->type==ECHOY_ROLE_FAILED)&&
       (e->generation!=d->role_generation||d->preparing_role<0))return false;
    if((e->type==ECHOY_PLAY_READY||e->type==ECHOY_PLAY_FAILED)&&
       (e->generation!=d->play_generation||!d->play_preparing))return false;
    if((e->type==ECHOY_SYNC_READY||e->type==ECHOY_SYNC_FAIL)&&
       (e->generation!=c->sync.generation||c->sync.stage!=ECHOY_SYNC_PREPARING||!(c->sync.members&MEMBER(e->device))))return false;
    if(e->type==ECHOY_BOOT_READY&&d->lifecycle!=ECHOY_BOOTING)return false;
    return true;
}
echoy_result_t echoy_dispatch(echoy_core_t *c,const echoy_input_t *e) {
    if(!c||!e||e->at_us<0||e->at_us>INT64_MAX-ECHOY_BAR_US||e->type<ECHOY_BOOT||e->type>ECHOY_BATTERY)return ECHOY_INVALID;
    if(e->type==ECHOY_CONTACTS) {
        /* Validate the complete report before changing time or topology. */
        echoy_result_t r=echoy_receive_contacts(c,e);if(r!=ECHOY_OK)return r;
        echoy_tick(c,e->at_us);return ECHOY_OK;
    }
    if(!valid_device(e->device)||!isfinite(e->value))return ECHOY_INVALID;
    echoy_device_t *d=&c->devices[e->device];
    if(!callback_current(c,e))return ECHOY_STALE;
    if((e->type==ECHOY_ROLE_REQUEST&&(e->value<0||e->value>3||floor(e->value)!=e->value))||
       (e->type==ECHOY_PHRASE&&(e->value<1||e->value>3||floor(e->value)!=e->value)))return ECHOY_INVALID;
    if(d->lifecycle==ECHOY_OFF&&e->type!=ECHOY_BOOT&&e->type!=ECHOY_BATTERY)return ECHOY_NOT_READY;
    echoy_tick(c,e->at_us);
    /* Time advancement can replace a sync transaction or power a member off. */
    if(!callback_current(c,e))return ECHOY_STALE;
    echoy_result_t result=ECHOY_OK;
    switch(e->type) {
    case ECHOY_BOOT:
        if(d->lifecycle!=ECHOY_OFF||!d->battery)return ECHOY_NOT_READY;
        /* Hardware boot IDs are injected; automatic IDs are host/local fallback. */
        if(e->boot_id&&e->boot_id!=d->boot_id)d->boot_id=e->boot_id;else d->boot_id=++c->generation;
        d->lifecycle=ECHOY_BOOTING;pause_device(d);cancel_role(d);d->motif_available=false;
        d->output_fault=d->data_fault=d->loading=d->role_error=false;d->link_online=true;
        d->lost_split=d->moving=d->volume_open=false;d->notice_until_us=0;break;
    case ECHOY_BOOT_READY:
        echoy_fresh_session(c,e->device,c->time_us,c->time_us+75000);d->lifecycle=ECHOY_READY;break;
    case ECHOY_POWER_OFF:
        echoy_leave(c,e->device);d->lifecycle=ECHOY_OFF;pause_device(d);cancel_role(d);
        d->motif_available=false;d->committed_activity=d->activity;d->volume_open=false;d->discovered=0;
        d->clock_version++;
        for(int j=0;j<3;j++){c->contacts[e->device][j].present=false;c->contacts[j][e->device].present=false;}
        if(c->sync.members&MEMBER(e->device))c->sync.stage=ECHOY_SYNC_NONE;
        break;
    case ECHOY_PLAY_TOGGLE:
        if(d->transport!=ECHOY_PAUSED||d->play_preparing){pause_device(d);echoy_commit_idle(c,e->device);}
        else result=request_play(c,e->device);
        break;
    case ECHOY_PAUSE: pause_device(d);echoy_commit_idle(c,e->device);break;
    case ECHOY_PLAY_PREPARE:
        if(d->lifecycle!=ECHOY_READY)return ECHOY_NOT_READY;
        d->play_preparing=true;d->play_generation++;break;
    case ECHOY_PLAY_READY: result=request_play(c,e->device);break;
    case ECHOY_PLAY_FAILED: d->play_preparing=false;d->output_fault=true;break;
    case ECHOY_ROLE_REQUEST:
        cancel_role(d);d->role_error=false;
        if((int)e->value!=(int)d->role)d->preparing_role=(int)e->value;
        break;
    case ECHOY_ROLE_READY: d->pending_role=d->preparing_role;d->preparing_role=-1;echoy_commit_idle(c,e->device);break;
    case ECHOY_ROLE_FAILED: cancel_role(d);d->role_error=true;break;
    case ECHOY_ROLE_CANCEL: cancel_role(d);break;
    case ECHOY_PHRASE: {
        int role=d->preparing_role>=0?d->preparing_role:d->pending_role>=0?d->pending_role:(int)d->role;
        d->phrase_role=role;d->pending_phrase=(int)e->value;
        if(d->pending_phrase==(role==(int)d->role?d->phrase:d->phrase_memories[role]))d->pending_phrase=0;
        echoy_commit_idle(c,e->device);break;
    }
    case ECHOY_ACTIVITY: d->activity=fmax(0,fmin(1,e->value));d->memories[d->role]=d->activity;break;
    case ECHOY_FINISH_ACTIVITY: echoy_commit_idle(c,e->device);break;
    case ECHOY_VOLUME: d->volume=fmax(0,fmin(1,e->value));break;
    case ECHOY_VOLUME_OPEN: d->volume_open=e->value!=0;break;
    case ECHOY_OUTPUT_FAULT: d->output_fault=e->value!=0;if(d->output_fault)d->volume_open=false;break;
    case ECHOY_DATA_FAULT: d->data_fault=e->value!=0;if(d->data_fault)d->volume_open=false;break;
    case ECHOY_DATA_LOADING: d->loading=e->value!=0;break;
    case ECHOY_LINK:
        if(d->link_online!=(e->value!=0)){d->link_online=e->value!=0;d->link_lost_us=c->time_us;}
        if(d->link_online)d->lost_split=false;
        break;
    case ECHOY_MOVE:
        if(!d->moving&&e->value!=0)d->moving_since_us=c->time_us;
        d->moving=e->value!=0;break;
    case ECHOY_SYNC_READY: {
        echoy_session_t *ref=NULL;for(int j=0;j<3;j++)if(c->sessions[j].id==c->sync.reference)ref=&c->sessions[j];
        if(!ref)return ECHOY_STALE;
        c->sync.stage=ECHOY_SYNC_WAITING;c->sync.apply_at_us=echoy_next_boundary(ref,c->time_us+140000,true);break;
    }
    case ECHOY_SYNC_FAIL: c->sync.stage=ECHOY_SYNC_FAILED;break;
    case ECHOY_NOTICE: {
        int64_t duration=(int64_t)fmax(0,fmin(e->value,3600000000.0));
        d->notice_until_us=duration>INT64_MAX-c->time_us?INT64_MAX:c->time_us+duration;break;
    }
    case ECHOY_BATTERY: d->battery=(int)fmax(0,fmin(100,e->value));break;
    default:return ECHOY_INVALID;
    }
    c->revision++;echoy_tick(c,c->time_us);return result;
}
void echoy_tick(echoy_core_t *c,int64_t at) {
    if(!c||at<0||at>INT64_MAX-ECHOY_BAR_US)return;
    if(at>c->time_us)c->time_us=at;
    for(int i=0;i<3;i++)if(c->devices[i].lifecycle!=ECHOY_OFF&&!c->devices[i].battery) {
        echoy_input_t off={.device=i,.type=ECHOY_POWER_OFF,.at_us=c->time_us};
        /* Avoid recursive tick while processing automatic shutdown. */
        c->devices[i].battery=1;(void)echoy_dispatch(c,&off);c->devices[i].battery=0;
    }
    echoy_session_tick(c);
    for(int j=0;j<3;j++) {
        echoy_session_t *s=&c->sessions[j];if(!s->id)continue;
        int64_t bar=(int64_t)(echoy_phase_steps(s,c->time_us)/16);
        if(bar!=s->last_bar) {
            s->last_bar=bar;
            for(int i=0;i<3;i++)if(!s->wire_managed&&(s->members&MEMBER(i))) {
                echoy_device_t *d=&c->devices[i];d->motif_available=!d->data_fault&&!d->loading&&d->battery>0;
            }
            if(!s->wire_managed)echoy_commit_members(c,s->members);
            c->revision++;
        }
        for(int i=0;i<3;i++)if(s->members&MEMBER(i)) {
            echoy_device_t *d=&c->devices[i];if(d->transport!=ECHOY_WAITING)continue;
            if(d->play_session!=s->id){d->play_session=s->id;d->play_at_us=echoy_next_boundary(s,c->time_us+25000,false);}
            if(c->time_us>=d->play_at_us) {
                if(!d->output_fault&&!d->data_fault&&!d->loading&&d->battery>0){
                    d->transport=ECHOY_PLAYING;
                    /* A sparse owner tick still starts on the requested beat. */
                    d->audible=d->volume>0&&d->motif_available;
                    if(d->audible)d->audible_since_us=d->play_at_us;
                }
                else d->play_at_us=echoy_next_boundary(s,c->time_us+25000,false);
            }
        }
    }
    echoy_publish(c);
}
void echoy_publish(echoy_core_t *c) {
    echoy_bar_plan_t *p=&c->plan;memset(p,0,sizeof *p);p->revision=c->revision;p->at_us=c->time_us;
    /* Retain the latest eligibility transition even if UI snapshots coalesce
     * an entire fault/mute window. Publishing never changes transport intent. */
    for(int i=0;i<3;i++){
        echoy_device_t *d=&c->devices[i];
        bool audible=d->lifecycle==ECHOY_READY&&d->transport==ECHOY_PLAYING&&d->volume>0&&
            !d->output_fault&&!d->data_fault&&!d->loading&&d->motif_available;
        if(audible&&!d->audible)d->audible_since_us=c->time_us;
        d->audible=audible;
    }
    for(int sidx=0;sidx<3;sidx++) {
        echoy_session_t *s=&c->sessions[sidx];if(!s->id)continue;
        unsigned visited=0;
        for(int head=0;head<3;head++) {
            if(!(s->members&MEMBER(head)))continue;
            bool incoming=false;for(int j=0;j<3;j++)if(c->committed_edges&EDGE(j,head))incoming=true;
            if(incoming)continue;
            int id=head,source=-1;int counts[4]={0};echoy_motif_t *left=NULL;
            while(id>=0&&!(visited&MEMBER(id))) {
                visited|=MEMBER(id);echoy_device_t *d=&c->devices[id];
                echoy_plan_entry_t *q=&p->devices[p->count++];q->device=id;q->source=source;
                q->boot_id=d->boot_id;q->session_id=s->id;q->clock_version=d->clock_version;
                q->bar=s->last_bar;q->epoch_us=s->epoch_us;q->rate_ppm=s->rate_ppm;
                q->role=d->role;q->phrase=d->phrase;q->activity=d->activity;q->committed_activity=d->committed_activity;
                q->volume=d->volume;q->transport=d->transport;q->motif_available=d->motif_available;q->audible_since_us=d->audible_since_us;
                if(source<0)memset(counts,0,sizeof counts);
                echoy_music_input_t in={.role=d->role,.phrase=d->phrase,.bar=(int)(q->bar%4),.duplicate=counts[d->role]++,
                    .activity=d->committed_activity,.bpm=96,.left=left};
                if(d->motif_available)(void)echoy_music_compose(&in,&q->motif);
                if(d->transport==ECHOY_PLAYING&&!d->output_fault&&!d->data_fault&&!d->loading)q->events=q->motif;
                source=q->motif.count?id:-1;left=q->motif.count?&q->motif:NULL;
                int next=-1;for(int j=0;j<3;j++)if(c->committed_edges&EDGE(id,j))next=j;
                id=next;
            }
        }
    }
}
void echoy_snapshot(const echoy_core_t *c,echoy_snapshot_t *out) {if(c&&out)*out=*c;}
void echoy_bar_plan(const echoy_core_t *c,echoy_bar_plan_t *out) {if(c&&out)*out=c->plan;}
const char *echoy_feedback_key(const echoy_core_t *c,int id) {
    if(!c||!valid_device(id))return "off";
    const echoy_device_t *d=&c->devices[id];
    if(d->lifecycle==ECHOY_OFF)return "off";
    if(d->lifecycle==ECHOY_BOOTING)return "boot";
    if(d->preparing_role>=0)return "loading-role";
    if(d->loading)return "loading-data";
    if(d->role_error)return "error-role";
    if(d->data_fault)return "error-data";
    if(d->output_fault)return "error-audio";
    if(!d->motif_available)return "motif-recovery";
    if(d->play_preparing)return "play-preparing";
    if(c->sync.stage!=ECHOY_SYNC_NONE&&(c->sync.members&MEMBER(id))) {
        if(c->sync.stage==ECHOY_SYNC_FAILED)return "sync-failed";
        return c->sync.stage==ECHOY_SYNC_WAITING?"sync-waiting":"syncing";
    }
    if(d->transport==ECHOY_WAITING)return "waiting";
    if(d->pending_role>=0)return "pending-role";
    if(d->pending_phrase)return "pending-phrase";
    if(fabs(d->activity-d->committed_activity)>.00001)return "pending-activity";
    if(d->moving)return "moving";
    for(int a=0;a<3;a++)for(int b=0;b<3;b++)if((a==id||b==id)&&c->contacts[a][b].present) {
        if(c->devices[a].moving||c->devices[b].moving)return "moving";
        if(c->contacts[a][b].uncertain||c->time_us-c->contacts[a][b].since_us<ECHOY_CONTACT_US)return "contact-confirming";
    }
    const echoy_session_t *s=NULL;for(int j=0;j<3;j++)if(c->sessions[j].id==d->session_id)s=&c->sessions[j];
    if(s&&s->arrangement_pending)return "arrangement-pending";
    if(d->notice_until_us>c->time_us)return "notice";
    if(!d->link_online)return d->lost_split?"link-split":"link-hold";
    bool source=false,follower=false;for(size_t j=0;j<c->plan.count;j++) {
        if(c->plan.devices[j].device==id)source=c->plan.devices[j].source>=0;
        if(c->plan.devices[j].source==id)follower=true;
    }
    bool shared=s&&(s->members&(s->members-1));
    if(d->transport==ECHOY_PAUSED)return source?"paused-source":shared?"paused-shared":"paused-solo";
    return source?"normal-source":follower?"normal-leader":shared?"normal-shared-solo":"normal-solo";
}
