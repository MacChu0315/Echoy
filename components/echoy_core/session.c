// SPDX-License-Identifier: Apache-2.0
#include "state_internal.h"
#include <math.h>
#include <string.h>
long double echoy_phase_steps(const echoy_session_t *s,int64_t at) {
    if(!s||!s->id||at<=s->epoch_us)return 0;
    return ((long double)at-s->epoch_us)*(1000000+s->rate_ppm)/156250000000.0L;
}
int64_t echoy_next_boundary(const echoy_session_t *s,int64_t at,bool bar) {
    if(!s||!s->id)return 0;
    long double period=(bar?ECHOY_BAR_US:ECHOY_BEAT_US)*1000000.0L/(1000000+s->rate_ppm);
    long double n=ceill(((long double)at-s->epoch_us)/period);if(n<0)n=0;
    long double result=ceill(s->epoch_us+n*period);
    return result>=INT64_MAX?INT64_MAX:(int64_t)result;
}
echoy_session_t *echoy_session_for(echoy_core_t *c,int d) {
    if(!c->devices[d].session_id)return NULL;
    for(int j=0;j<3;j++)if(c->sessions[j].id==c->devices[d].session_id)return &c->sessions[j];
    return NULL;
}
void echoy_leave(echoy_core_t *c,int id) {
    echoy_session_t *s=echoy_session_for(c,id);
    if(s) {
        s->members&=~MEMBER(id);s->arrangement_pending=false;
        if(!s->members)memset(s,0,sizeof *s);
        else if(s->leader==id) {
            int leader=-1;for(int j=0;j<3;j++)if((s->members&MEMBER(j))&&(leader<0||c->devices[j].uid<c->devices[leader].uid))leader=j;
            s->leader=leader; /* Keep epoch and rate: no phase step on election. */
        }
    }
    c->devices[id].session_id=0;
    for(int j=0;j<3;j++)c->committed_edges&=~(EDGE(id,j)|EDGE(j,id));
}
void echoy_fresh_session(echoy_core_t *c,int id,int64_t at,int64_t epoch) {
    for(int j=0;j<3;j++)if(!c->sessions[j].id) {
        echoy_session_t *s=&c->sessions[j];memset(s,0,sizeof *s);
        s->id=++c->serial;s->members=MEMBER(id);s->leader=id;s->epoch_us=epoch;s->created_us=at;
        s->rate_ppm=c->devices[id].clock_ppm;s->last_bar=-1;
        c->devices[id].session_id=s->id;c->devices[id].clock_version++;return;
    }
}
echoy_result_t echoy_receive_contacts(echoy_core_t *c,const echoy_input_t *e) {
    if(e->edge_count>2)return ECHOY_TOPOLOGY_REJECTED;
    unsigned edges=0,in=0,out=0;
    for(size_t i=0;i<e->edge_count;i++) {
        int a=e->edges[i].left,b=e->edges[i].right;
        if(a<0||b<0||a>=3||b>=3||a==b||(out&MEMBER(a))||(in&MEMBER(b)))return ECHOY_TOPOLOGY_REJECTED;
        edges|=EDGE(a,b);out|=MEMBER(a);in|=MEMBER(b);
    }
    for(int start=0;start<3;start++) {
        int at=start;unsigned seen=0;
        while(at>=0) {
            if(seen&MEMBER(at))return ECHOY_TOPOLOGY_REJECTED;
            seen|=MEMBER(at);int next=-1;for(int j=0;j<3;j++)if(edges&EDGE(at,j))next=j;at=next;
        }
    }
    int64_t at=e->at_us>c->time_us?e->at_us:c->time_us;
    for(int a=0;a<3;a++)for(int b=0;b<3;b++) {
        echoy_contact_t *old=&c->contacts[a][b];bool found=false,uncertain=false;
        for(size_t j=0;j<e->edge_count;j++)if(e->edges[j].left==a&&e->edges[j].right==b){found=true;uncertain=e->edges[j].uncertain;}
        if(found&&(!old->present||old->uncertain!=uncertain))old->since_us=at;
        old->present=found;old->uncertain=uncertain;
    }
    /* update_arrangements compares effective targets; repeated evidence must
     * preserve an already agreed boundary instead of restarting its deadline. */
    c->revision++;return ECHOY_OK;
}
static bool available(const echoy_core_t *c,int a,int b) {
    const echoy_device_t *x=&c->devices[a],*y=&c->devices[b];
    return x->lifecycle==ECHOY_READY&&y->lifecycle==ECHOY_READY&&x->session_id&&y->session_id&&x->link_online&&y->link_online;
}
static bool confirmed(const echoy_core_t *c,int a,int b) {
    const echoy_contact_t *k=&c->contacts[a][b];
    return k->present&&!k->uncertain&&c->time_us-k->since_us>=ECHOY_CONTACT_US&&available(c,a,b)&&!c->devices[a].moving&&!c->devices[b].moving;
}
static int count(unsigned mask) {int n=0;for(;mask;mask>>=1)n+=(int)(mask&1);return n;}
static void update_merge(echoy_core_t *c) {
    unsigned involved=0;
    for(int a=0;a<3;a++)for(int b=0;b<3;b++)if(confirmed(c,a,b)&&c->devices[a].session_id!=c->devices[b].session_id) {
        involved|=echoy_session_for(c,a)->members|echoy_session_for(c,b)->members;
    }
    if(!involved){c->sync.stage=ECHOY_SYNC_NONE;return;}
    bool changed=c->sync.members!=involved;
    for(int j=0;j<3;j++)if((involved&MEMBER(j))&&c->sync.boots[j]!=c->devices[j].boot_id)changed=true;
    if(c->sync.stage==ECHOY_SYNC_NONE||changed) {
        echoy_session_t *ref=NULL;
        for(int j=0;j<3;j++) {
            echoy_session_t *s=&c->sessions[j];if(!s->id||!(s->members&involved))continue;
            if(!ref||count(s->members)>count(ref->members)||
                (count(s->members)==count(ref->members)&&c->devices[s->leader].uid<c->devices[ref->leader].uid))ref=s;
        }
        memset(&c->sync,0,sizeof c->sync);c->sync.stage=ECHOY_SYNC_PREPARING;
        c->sync.members=involved;c->sync.reference=ref->id;c->sync.generation=++c->generation;c->sync.started_us=c->time_us;
        for(int j=0;j<3;j++)c->sync.boots[j]=c->devices[j].boot_id;
    }
    if(c->sync.stage!=ECHOY_SYNC_WAITING||c->time_us<c->sync.apply_at_us)return;
    echoy_session_t *ref=NULL;for(int j=0;j<3;j++)if(c->sessions[j].id==c->sync.reference)ref=&c->sessions[j];
    if(!ref){c->sync.stage=ECHOY_SYNC_NONE;return;}
    for(int j=0;j<3;j++) {
        echoy_session_t *s=&c->sessions[j];if(!s->id||s==ref||!(s->members&involved))continue;
        for(int k=0;k<3;k++)if(s->members&MEMBER(k)){c->devices[k].session_id=ref->id;c->devices[k].clock_version++;}
        ref->members|=s->members;memset(s,0,sizeof *s);
    }
    /* A merge and a direction swap may share a boundary. Replace the whole
     * validated chain atomically; unioning old/new edges could create a cycle. */
    for(int a=0;a<3;a++)for(int b=0;b<3;b++)
        if((ref->members&MEMBER(a))&&(ref->members&MEMBER(b)))c->committed_edges&=~EDGE(a,b);
    ref->arrangement_pending=false;
    for(int a=0;a<3;a++)for(int b=0;b<3;b++)if(confirmed(c,a,b)&&c->devices[a].session_id==ref->id&&c->devices[b].session_id==ref->id)c->committed_edges|=EDGE(a,b);
    c->sync.stage=ECHOY_SYNC_NONE;c->revision++;
}
static void update_arrangements(echoy_core_t *c) {
    for(int j=0;j<3;j++) {
        echoy_session_t *s=&c->sessions[j];if(!s->id)continue;
        unsigned target=0,current=0;
        for(int a=0;a<3;a++)for(int b=0;b<3;b++) {
            if(!(s->members&MEMBER(a))||!(s->members&MEMBER(b)))continue;
            unsigned edge=EDGE(a,b);if(c->committed_edges&edge)current|=edge;
            const echoy_contact_t *k=&c->contacts[a][b];
            if(!k->present||!available(c,a,b))continue;
            if(confirmed(c,a,b))target|=edge;
            else if(current&edge) {
                int64_t since=k->since_us;
                if(c->devices[a].moving)since=c->devices[a].moving_since_us;
                else if(c->devices[b].moving)since=c->devices[b].moving_since_us;
                if(c->time_us-since<ECHOY_HOLD_US)target|=edge;
            }
        }
        if(current==target){s->arrangement_pending=false;continue;}
        if(!s->arrangement_pending||s->target_edges!=target) {
            s->arrangement_pending=true;s->target_edges=target;
            s->arrangement_at_us=echoy_next_boundary(s,c->time_us+40000,true);
        }
        if(c->time_us>=s->arrangement_at_us) {
            c->committed_edges=(c->committed_edges&~current)|target;
            s->arrangement_pending=false;c->revision++;
        }
    }
}
void echoy_session_tick(echoy_core_t *c) {
    for(int i=0;i<3;i++) {
        echoy_device_t *d=&c->devices[i];d->discovered=0;
        if(d->lifecycle!=ECHOY_READY)continue;
        if(!d->link_online&&!d->lost_split&&c->time_us-d->link_lost_us>=ECHOY_HOLD_US) {
            echoy_session_t *old=echoy_session_for(c,i);
            int64_t committed_bar=old->last_bar;
            long double phase=echoy_phase_steps(old,c->time_us);
            /* Round epoch down: sub-microsecond quantization cannot move time backwards. */
            long double duration=phase*156250000000.0L/(1000000+d->clock_ppm);
            int64_t epoch=(int64_t)floorl(c->time_us-duration);
            echoy_leave(c,i);echoy_fresh_session(c,i,c->time_us,epoch);
            /* A continuous clock handover is not a musical boundary. Retain
             * the last committed bar; core tick still commits if a real bar
             * boundary was crossed since that commit (including sparse ticks). */
            echoy_session_for(c,i)->last_bar=committed_bar;d->lost_split=true;
        }
        if(d->link_online)for(int j=0;j<3;j++)if(j!=i&&c->devices[j].lifecycle==ECHOY_READY&&c->devices[j].link_online)d->discovered|=MEMBER(j);
    }
    update_merge(c);update_arrangements(c);
}
