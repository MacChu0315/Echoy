// SPDX-License-Identifier: Apache-2.0
#include "echoy_control.h"
#include <math.h>
#include <string.h>
static int index_of(const echoy_wire_state_t *s,uint64_t uid){for(unsigned i=0;i<s->member_count;i++)if(s->members[i].uid==uid)return (int)i;return -1;}
static int slot(echoy_control_t *c,uint64_t uid){for(int i=0;i<3;i++)if(c->core.devices[i].uid==uid)return i;return -1;}
static echoy_protocol_peer_t *peer(echoy_control_t *c,uint64_t uid){for(int i=0;i<2;i++)if(c->protocol.peers[i].uid==uid)return &c->protocol.peers[i];return NULL;}
static echoy_session_t *local_session(echoy_control_t *c){for(int i=0;i<3;i++)if(c->core.sessions[i].id==c->core.devices[0].session_id)return &c->core.sessions[i];return NULL;}
static void input(echoy_control_t *c,int d,echoy_action_t t,double v,int64_t now){echoy_input_t e={.device=d,.type=t,.value=v,.at_us=now,.boot_id=c->core.devices[d].boot_id,.generation=c->core.devices[d].role_generation};if(echoy_dispatch(&c->core,&e)!=ECHOY_OK)c->rejected++;}
static bool transmit(void *ctx,uint64_t uid,const uint8_t *p,size_t n){echoy_control_t *c=ctx;return c->send&&c->send(c->send_ctx,uid,p,n);}
static bool prepare(void *ctx,const echoy_wire_state_t *s){
    echoy_control_t *c=ctx;int local=index_of(s,c->protocol.uid);if(local<0)return false;
    const echoy_device_t *d=&c->core.devices[0];
    /* Output failure suppresses sound, never the generated motif. */
    return d->lifecycle==ECHOY_READY&&!d->loading&&!d->data_fault&&s->members[local].phrase<=3;
}
static void installed(void *ctx,const echoy_wire_state_t *s,echoy_session_key_t key,int64_t epoch,int rate,int64_t now){
    echoy_control_t *c=ctx;
    if(!echoy_session_equal(key,c->installed_key)){c->installed_key=key;c->session_serial=++c->core.serial;}
    echoy_device_t *d=&c->core.devices[0];int local=index_of(s,d->uid);echoy_role_t previous=d->role;double activity=d->activity;
    if(echoy_core_install(&c->core,0,s,c->session_serial,epoch,rate,now<c->core.time_us?c->core.time_us:now)!=ECHOY_OK){c->rejected++;c->protocol.degraded=true;return;}
    if(local>=0){
        if(previous!=d->role)d->activity=d->committed_activity;
        else d->activity=activity;
        d->memories[d->role]=d->activity;d->phrase_memories[d->role]=d->phrase;
        if(d->pending_role==(int)d->role)d->pending_role=-1;
        if(d->pending_phrase==d->phrase&&d->phrase_role==(int)d->role)d->pending_phrase=0;
    }
}
static echoy_wire_member_t device_member(const echoy_device_t *d,bool pending){
    int role=pending&&d->pending_role>=0?d->pending_role:(int)d->role;
    int phrase=role==(int)d->role?d->phrase:d->phrase_memories[role];
    if(pending&&d->pending_phrase&&d->phrase_role==role)phrase=d->pending_phrase;
    return (echoy_wire_member_t){.uid=d->uid,.boot=(uint32_t)d->boot_id,.role=role,.phrase=phrase,
        .activity=(uint16_t)lround((role==(int)d->role?d->activity:d->memories[role])*1000),
        .motif_available=!d->loading&&!d->data_fault,.transport=d->transport};
}
static bool authorize(echoy_control_t *c,uint64_t uid,uint32_t boot,int64_t now){
    int ix=slot(c,uid);if(ix<0)for(int i=1;i<3;i++)if(!c->core.devices[i].uid||c->core.devices[i].lifecycle==ECHOY_OFF){ix=i;break;}
    if(ix<0||!echoy_protocol_authorize(&c->protocol,uid,boot,now))return false;
    echoy_device_t *d=&c->core.devices[ix];
    if(d->uid!=uid||d->boot_id!=boot||d->lifecycle!=ECHOY_READY){
        if(d->lifecycle!=ECHOY_OFF)input(c,ix,ECHOY_POWER_OFF,0,now);
        d->uid=uid;d->battery=100;echoy_input_t e={.type=ECHOY_BOOT,.device=ix,.boot_id=boot,.at_us=now};
        if(echoy_dispatch(&c->core,&e)!=ECHOY_OK)return false;
        input(c,ix,ECHOY_BOOT_READY,0,now);
    }
    echoy_protocol_peer_t *q=peer(c,uid);unsigned p=(unsigned)(q-c->protocol.peers);
    if(c->requests[p].uid!=uid||c->requests[p].boot!=boot){memset(&c->requests[p],0,sizeof c->requests[p]);c->requests[p].uid=uid;c->requests[p].boot=boot;memset(&c->remote[p],0,sizeof c->remote[p]);}
    return true;
}
void echoy_control_init(echoy_control_t *c,uint64_t uid,uint32_t boot,uint32_t nonce,const echoy_settings_t *s,echoy_protocol_send_fn send,void *ctx){
    memset(c,0,sizeof *c);c->send=send;c->send_ctx=ctx;uint64_t ids[]={uid,0,0};echoy_core_init(&c->core,ids);
    if(s){(void)echoy_settings_restore(s,&c->core.devices[0]);c->english=s->locale;}
    echoy_input_t e={.type=ECHOY_BOOT,.device=0,.boot_id=boot};(void)echoy_dispatch(&c->core,&e);input(c,0,ECHOY_BOOT_READY,0,0);
    echoy_protocol_init(&c->protocol,uid,boot,nonce,transmit,prepare,installed,c);
    c->protocol.committed.members[0]=device_member(&c->core.devices[0],false);c->protocol.committed.epoch_us=75000;
    c->installed_key=c->protocol.session;c->session_serial=c->core.devices[0].session_id;c->request_at_us=-ECHOY_HEARTBEAT_US;
}
bool echoy_control_enqueue(echoy_control_t *c,echoy_ui_event_t e){
    if(!c||e.action<UI_NONE||e.action>UI_CANCEL_LOAD||!isfinite(e.value))return false;
    if(c->count==ECHOY_CONTROL_DEPTH){c->dropped++;return false;}c->queue[(c->head+c->count)%ECHOY_CONTROL_DEPTH]=e;c->count++;return true;
}
void echoy_control_physical(echoy_control_t *c,uint64_t left,uint64_t right,bool uncertain,int64_t now){
    if(!c||left==c->protocol.uid||right==c->protocol.uid||(left&&left==right))return;
    if(c->physical[0]!=left||c->physical[1]!=right||c->uncertain!=uncertain)c->physical_since_us=now;
    c->physical[0]=left;c->physical[1]=right;c->uncertain=uncertain;
}
static bool physical(const echoy_control_t *c,uint64_t uid){return !c->uncertain&&uid&&(uid==c->physical[0]||uid==c->physical[1]);}
bool echoy_control_receive(echoy_control_t *c,uint64_t uid,uint32_t boot,const uint8_t *bytes,size_t size,int64_t received,int64_t now){
    echoy_packet_t p;if(!c||received<0||now<received||!echoy_wire_decode(bytes,size,&p)||p.sender_boot!=boot||uid==c->protocol.uid)return false;
    echoy_protocol_peer_t *q=peer(c,uid);
    if(p.kind==ECHOY_HELLO&&physical(c,uid)&&p.body[0]>=1&&p.body[0]<=3&&!p.body[1]&&echoy_get_le(p.body+2,8)&&echoy_get_le(p.body+10,4)&&echoy_get_le(p.body+14,2)==1){
        if(!q||q->boot!=boot||!q->authorized||q->expired){if(!authorize(c,uid,boot,now))return false;q=peer(c,uid);}
    }
    if(!p.flags)return echoy_protocol_receive(&c->protocol,uid,boot,bytes,size,received,now);
    if(!q||!q->authorized||q->lost||q->expired||q->boot!=boot||p.sequence<=q->sequence||!echoy_session_equal(p.session,q->session)){c->rejected++;return false;}
    unsigned ix=(unsigned)(q-c->protocol.peers);echoy_wire_state_t state;
    echoy_wire_result_t result=echoy_wire_assemble(&c->requests[ix],uid,boot,&p,now,&state);
    if(result==ECHOY_WIRE_MORE){q->sequence=p.sequence;return true;}
    if(result!=ECHOY_WIRE_OK||state.transport_request_uid!=uid||state.reference_uid!=uid||p.apply_bar!=1){c->rejected++;return false;}
    int sender=index_of(&state,uid);if(sender<0||state.members[sender].boot!=boot){c->rejected++;return false;}
    /* Every edge is local to this physical witness. No remote member can invent
     * an edge between two other devices. Existing group members can relay UID/
     * boot bindings; discovery-only senders cannot reach this point. */
    for(unsigned i=0;i<state.edge_count;i++)if(state.members[state.edges[i][0]].uid!=uid&&state.members[state.edges[i][1]].uid!=uid){c->rejected++;return false;}
    for(unsigned i=0;i<state.member_count;i++){
        const echoy_wire_member_t *m=&state.members[i];echoy_protocol_peer_t *known=peer(c,m->uid);
        if(m->phrase>3||(m->uid==c->protocol.uid&&m->boot!=c->protocol.boot)||
            (known&&known->authorized&&known->boot!=m->boot)){c->rejected++;return false;}
    }
    for(unsigned i=0;i<state.member_count;i++){
        echoy_wire_member_t *m=&state.members[i];if(m->phrase>3){c->rejected++;return false;}
        if(m->uid==c->protocol.uid){if(m->boot!=c->protocol.boot)return false;continue;}
        echoy_protocol_peer_t *known=peer(c,m->uid);
        if(m->uid!=uid&&(!known||!known->authorized||known->boot!=m->boot)){
            if(!physical(c,uid)&&!q->member){c->rejected++;return false;}
            if(!authorize(c,m->uid,m->boot,now)){c->rejected++;return false;}
        }
    }
    q->sequence=p.sequence;c->remote[ix]=state;return true;
}
static void add_edge(echoy_wire_state_t *s,uint64_t a,uint64_t b){
    int left=index_of(s,a),right=index_of(s,b);if(left<0||right<0||left==right)return;
    for(unsigned i=0;i<s->edge_count;i++)if(s->edges[i][0]==left&&s->edges[i][1]==right)return;
    if(s->edge_count<2){s->edges[s->edge_count][0]=(uint8_t)left;s->edges[s->edge_count++][1]=(uint8_t)right;}
}
static echoy_wire_state_t request_state(echoy_control_t *c,int64_t now){
    echoy_wire_state_t s={.revision=c->protocol.committed.revision,.reference_uid=c->protocol.uid,.transport_request_uid=c->protocol.uid,.member_count=1};
    s.members[0]=device_member(&c->core.devices[0],true);
    /* Handoff lists only established members or currently verified physical
     * neighbors. Transitively learned uncommitted peers are not re-vouched. */
    for(int i=0;i<2;i++){echoy_protocol_peer_t *q=&c->protocol.peers[i];int ix=slot(c,q->uid);
        if(q->authorized&&!q->expired&&ix>=0&&(q->member||physical(c,q->uid)))s.members[s.member_count++]=device_member(&c->core.devices[ix],false);}
    if(!c->uncertain&&now-c->physical_since_us>=ECHOY_CONTACT_US){add_edge(&s,c->physical[0],c->protocol.uid);add_edge(&s,c->protocol.uid,c->physical[1]);}
    return s;
}
static void send_request(echoy_control_t *c,int64_t now){
    if(now-c->request_at_us<ECHOY_HEARTBEAT_US||c->request_transaction==UINT32_MAX)return;
    c->request_at_us=now;
    echoy_wire_state_t s=request_state(c,now);uint8_t data[960];size_t n;if(!echoy_wire_state_encode(&s,data,&n))return;
    uint32_t transaction=++c->request_transaction;
    for(int i=0;i<2;i++){echoy_protocol_peer_t *q=&c->protocol.peers[i];if(!q->authorized||q->expired)continue;
        for(size_t off=0;off<n;off+=60){if(c->protocol.sequence==UINT32_MAX)return;
            echoy_packet_t p={.kind=ECHOY_PREPARE,.flags=1,.sender_boot=c->protocol.boot,.sequence=++c->protocol.sequence,.session=c->protocol.session,
                .transaction=transaction,.apply_bar=1,.fragment_index=off/60,.fragment_count=(n+59)/60,.body_length=n-off>60?60:n-off};
            memcpy(p.body,data+off,p.body_length);uint8_t bytes[100];size_t size;
            if(!echoy_wire_encode(&p,bytes,&size)||!transmit(c,q->uid,bytes,size))c->protocol.send_failures++;
        }
    }
}
static void contacts(echoy_control_t *c,int64_t now){
    echoy_wire_state_t graph={.member_count=3};
    for(int i=0;i<3;i++)graph.members[i].uid=c->core.devices[i].uid;
    if(!c->uncertain||now-c->physical_since_us<ECHOY_HOLD_US){
        add_edge(&graph,c->physical[0],c->protocol.uid);add_edge(&graph,c->protocol.uid,c->physical[1]);
    }
    for(int j=0;j<2;j++){
        echoy_protocol_peer_t *q=&c->protocol.peers[j];if(!q->authorized||q->expired)continue;
        for(unsigned i=0;i<c->remote[j].edge_count;i++)add_edge(&graph,c->remote[j].members[c->remote[j].edges[i][0]].uid,c->remote[j].members[c->remote[j].edges[i][1]].uid);
    }
    echoy_input_t e={.type=ECHOY_CONTACTS,.device=0,.at_us=now,.edge_count=graph.edge_count};
    for(unsigned i=0;i<graph.edge_count;i++)e.edges[i]=(echoy_edge_t){graph.edges[i][0],graph.edges[i][1],c->uncertain&&(graph.edges[i][0]==0||graph.edges[i][1]==0)};
    if(echoy_dispatch(&c->core,&e)!=ECHOY_OK)c->rejected++;
}
static bool musical_equal(const echoy_wire_state_t *a,const echoy_wire_state_t *b){
    if(a->member_count!=b->member_count||a->edge_count!=b->edge_count||a->reference_uid!=b->reference_uid)return false;
    for(unsigned i=0;i<a->member_count;i++){int j=index_of(b,a->members[i].uid);if(j<0)return false;
        const echoy_wire_member_t *x=&a->members[i],*y=&b->members[j];if(x->boot!=y->boot||x->role!=y->role||x->phrase!=y->phrase||x->activity!=y->activity||x->motif_available!=y->motif_available)return false;}
    for(unsigned i=0;i<a->edge_count;i++){bool found=false;for(unsigned j=0;j<b->edge_count;j++)found|=a->members[a->edges[i][0]].uid==b->members[b->edges[j][0]].uid&&a->members[a->edges[i][1]].uid==b->members[b->edges[j][1]].uid;if(!found)return false;}
    return true;
}
static void propose(echoy_control_t *c,int64_t now){
    if(c->protocol.pending.active||echoy_protocol_reference(&c->protocol)!=c->protocol.uid||now-c->last_proposal_us<1000000)return;
    echoy_session_t *local=local_session(c);if(!local)return;
    echoy_wire_state_t s={.revision=c->protocol.committed.revision+1,.reference_uid=c->protocol.uid,.epoch_us=local->epoch_us,.rate_ppm=local->rate_ppm,.member_count=1};
    s.members[0]=device_member(&c->core.devices[0],true);
    for(int i=0;i<2;i++){
        echoy_protocol_peer_t *q=&c->protocol.peers[i];int j=index_of(&c->remote[i],q->uid);
        if(q->authorized&&!q->lost&&!q->expired&&j>=0)s.members[s.member_count++]=c->remote[i].members[j];
    }
    if(s.member_count==1)return;
    echoy_wire_state_t own=request_state(c,now);
    for(unsigned i=0;i<own.edge_count;i++)add_edge(&s,own.members[own.edges[i][0]].uid,own.members[own.edges[i][1]].uid);
    for(int j=0;j<2;j++)for(unsigned i=0;i<c->remote[j].edge_count;i++)add_edge(&s,c->remote[j].members[c->remote[j].edges[i][0]].uid,c->remote[j].members[c->remote[j].edges[i][1]].uid);
    /* Joining needs a complete chain; removal of edges in an established group
     * is also a negotiated arrangement and never erases membership. */
    if(s.member_count>c->protocol.committed.member_count&&s.edge_count+1!=s.member_count)return;
    if(!c->protocol.degraded&&musical_equal(&s,&c->protocol.committed))return;
    c->last_proposal_us=now;(void)echoy_protocol_begin(&c->protocol,&s,now);
}
void echoy_control_tick(echoy_control_t *c,int64_t now,bool fault){
    if(!c||now<c->core.time_us)return;
    if(c->core.devices[0].output_fault!=fault)input(c,0,ECHOY_OUTPUT_FAULT,fault,now);
    while(c->count){echoy_ui_event_t e=c->queue[c->head];c->head=(c->head+1)%ECHOY_CONTROL_DEPTH;c->count--;
        switch(e.action){
        case UI_PLAY:input(c,0,ECHOY_PLAY_TOGGLE,0,now);break;
        case UI_ROLE_SELECT:input(c,0,ECHOY_ROLE_REQUEST,e.value,now);if(c->core.devices[0].preparing_role>=0)input(c,0,ECHOY_ROLE_READY,0,now);break;
        case UI_PHRASE_SELECT:input(c,0,ECHOY_PHRASE,e.value,now);break;
        case UI_SET_ACTIVITY:input(c,0,ECHOY_ACTIVITY,e.value,now);break;
        case UI_FINISH_ACTIVITY:input(c,0,ECHOY_FINISH_ACTIVITY,0,now);break;
        case UI_SET_VOLUME:input(c,0,ECHOY_VOLUME,e.value,now);break;
        case UI_VOLUME_OPEN:case UI_VOLUME_CLOSE:input(c,0,ECHOY_VOLUME_OPEN,e.action==UI_VOLUME_OPEN,now);break;
        case UI_CANCEL_LOAD:input(c,0,ECHOY_ROLE_CANCEL,0,now);break;
        default:break;
        }
    }
    echoy_protocol_tick(&c->protocol,now);
    bool expired=false;for(int i=0;i<2;i++){echoy_protocol_peer_t *q=&c->protocol.peers[i];int ix=slot(c,q->uid);if(!q->authorized||ix<0)continue;
        if(c->core.devices[ix].link_online==q->lost)input(c,ix,ECHOY_LINK,!q->lost,q->lost?q->lost_us:now);
        if(q->expired&&q->member)expired=true;
    }
    contacts(c,now);
    echoy_tick(&c->core,now);
    if(expired){
        /* Core's hold split preserves phase. Rebase surviving membership only
         * after that split; stale protocol membership must not block recovery. */
        echoy_session_t *s=local_session(c);if(s){echoy_wire_state_t kept={.revision=1,.reference_uid=c->core.devices[s->leader].uid,.epoch_us=s->epoch_us,.rate_ppm=s->rate_ppm};
            for(int i=0;i<3;i++)if(s->members&(1u<<i))kept.members[kept.member_count++]=device_member(&c->core.devices[i],false);
            c->protocol.pending.active=false;c->protocol.committed=kept;c->protocol.session=(echoy_session_key_t){kept.reference_uid,(uint32_t)c->core.devices[s->leader].boot_id,(uint32_t)++c->core.serial};
            c->protocol.degraded=true;c->protocol.syncing=true;
            for(int i=0;i<2;i++)c->protocol.peers[i].member=index_of(&kept,c->protocol.peers[i].uid)>=0;
        }
    }
    send_request(c,now);propose(c,now);
    if(c->publish&&!c->publish(c->publish_ctx,&c->core,&c->core.plan))c->publication_failures++;
}
bool echoy_control_forecast(const echoy_control_t *c,int64_t target,echoy_core_t *scratch){
    if(!c||!scratch||scratch==&c->core||target<c->core.time_us||target-c->core.time_us>40000)return false;
    *scratch=c->core;const echoy_protocol_pending_t *p=&c->protocol.pending;
    if(p->active&&p->commit_seen&&p->apply_us<=target){
        uint64_t id=echoy_session_equal(p->session,c->installed_key)?c->session_serial:++scratch->serial;
        int64_t at=p->apply_us<scratch->time_us?scratch->time_us:p->apply_us;
        if(echoy_core_install(scratch,0,&p->state,id,p->epoch_us,p->rate_ppm,at)!=ECHOY_OK)return false;
    }
    echoy_tick(scratch,target);return true;
}
