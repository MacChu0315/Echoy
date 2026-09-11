// SPDX-License-Identifier: Apache-2.0
#include "echoy_protocol.h"
#include <math.h>
#include <string.h>
static echoy_protocol_peer_t *peer(echoy_protocol_t *p,uint64_t uid) {
    for(unsigned i=0;i<2;i++)if(p->peers[i].uid==uid)return &p->peers[i];
    return NULL;
}
static void degrade(echoy_protocol_t *p) {p->degraded=true;p->syncing=true;p->pending.active=false;}
static bool emit(echoy_protocol_t *p,uint64_t target,echoy_packet_t *packet) {
    if(p->sequence==UINT32_MAX){degrade(p);return false;}
    packet->sender_boot=p->boot;packet->sequence=++p->sequence;
    uint8_t bytes[100];size_t size;
    if(!echoy_wire_encode(packet,bytes,&size)||!p->send||!p->send(p->ctx,target,bytes,size)){p->send_failures++;return false;}
    return true;
}
static echoy_packet_t message(const echoy_protocol_t *p,echoy_wire_kind_t kind,uint8_t length) {
    return (echoy_packet_t){.kind=kind,.session=p->session,.fragment_count=1,.body_length=length};
}
static void control(echoy_protocol_t *p,uint64_t target,echoy_wire_kind_t kind) {
    echoy_packet_t m=message(p,kind,8);m.session=p->pending.session;
    m.transaction=p->pending.transaction;m.apply_bar=p->pending.apply_bar;
    echoy_put_le(m.body,p->pending.state.revision,4);echoy_put_le(m.body+4,p->pending.digest,4);(void)emit(p,target,&m);
}
void echoy_protocol_init(echoy_protocol_t *p,uint64_t uid,uint32_t boot,uint32_t nonce,
    echoy_protocol_send_fn send,echoy_protocol_prepare_fn prepare,echoy_protocol_commit_fn commit,void *ctx) {
    if(!p)return;
    memset(p,0,sizeof *p);p->uid=uid;p->boot=boot;p->session=(echoy_session_key_t){uid,boot,nonce};
    p->send=send;p->prepare=prepare;p->commit=commit;p->ctx=ctx;p->last_periodic_us=-ECHOY_HEARTBEAT_US;
    p->committed=(echoy_wire_state_t){.revision=1,.reference_uid=uid,.member_count=1,
        .members={{.uid=uid,.boot=boot,.phrase=1}}};
}
bool echoy_protocol_authorize(echoy_protocol_t *p,uint64_t uid,uint32_t boot,int64_t now) {
    if(!p||!uid||uid==p->uid||!boot||now<0)return false;
    echoy_protocol_peer_t *q=peer(p,uid);
    if(!q)for(unsigned i=0;i<2;i++)if(!p->peers[i].uid||!p->peers[i].authorized){q=&p->peers[i];break;}
    if(!q){p->queue_full++;return false;}
    if(q->authorized&&q->boot==boot&&!q->expired)return true;
    if(q->authorized&&q->boot!=boot)degrade(p);
    memset(q,0,sizeof *q);q->uid=uid;q->boot=boot;q->authorized=true;q->last_rx_us=now;
    q->advertised_members=1;q->advertised_reference=uid;q->assembly.uid=uid;q->assembly.boot=boot;
    return true;
}
uint64_t echoy_protocol_reference(const echoy_protocol_t *p) {
    if(!p)return 0;
    unsigned count=p->committed.member_count;uint64_t uid=p->committed.reference_uid;
    for(unsigned i=0;i<2;i++) {
        const echoy_protocol_peer_t *q=&p->peers[i];if(!q->authorized||q->lost)continue;
        if(q->advertised_members>count||(q->advertised_members==count&&q->advertised_reference<uid)) {
            count=q->advertised_members;uid=q->advertised_reference;
        }
    }return uid;
}
static bool state_allowed(echoy_protocol_t *p,const echoy_wire_state_t *s) {
    bool self=false;
    for(unsigned i=0;i<s->member_count;i++) {
        if(s->members[i].uid==p->uid){if(s->members[i].boot!=p->boot)return false;self=true;continue;}
        echoy_protocol_peer_t *q=peer(p,s->members[i].uid);
        if(!q||!q->authorized||q->boot!=s->members[i].boot||q->lost)return false;
    }
    /* No proposal may silently remove an existing member. Loss/hold is separate. */
    for(unsigned i=0;i<p->committed.member_count;i++) {
        bool found=false;for(unsigned j=0;j<s->member_count;j++)found|=p->committed.members[i].uid==s->members[j].uid;
        if(!found)return false;
    }
    return self&&s->revision>p->committed.revision&&(!p->prepare||p->prepare(p->ctx,s));
}
static bool map_state(echoy_protocol_t *p,uint64_t source,const echoy_wire_state_t *s,uint32_t bar,
    int64_t *epoch,int64_t *apply,int *rate) {
    long double remote=s->epoch_us+(long double)bar*ECHOY_BAR_US*1000000/(1000000+s->rate_ppm);
    if(remote<0||remote>INT64_MAX)return false;
    *rate=s->rate_ppm;
    if(source==p->uid){*epoch=s->epoch_us;*apply=(int64_t)ceill(remote);return true;}
    echoy_protocol_peer_t *q=peer(p,source);
    if(!q||p->now_us-q->clock.last_t4>750000||!echoy_clock_remote_to_local(&q->clock,s->epoch_us,epoch)||
       !echoy_clock_remote_to_local(&q->clock,(int64_t)ceill(remote),apply))return false;
    *rate=(int)llround((1+q->clock.rate)*(1000000+s->rate_ppm)-1000000);return *rate>=-1000&&*rate<=1000;
}
static unsigned member_mask(const echoy_protocol_t *p) {
    unsigned mask=0;for(unsigned i=0;i<2;i++)for(unsigned j=0;j<p->pending.state.member_count;j++)
        if(p->peers[i].uid==p->pending.state.members[j].uid)mask|=1u<<i;
    return mask;
}
bool echoy_protocol_begin(echoy_protocol_t *p,const echoy_wire_state_t *s,int64_t now) {
    uint8_t data[960];size_t n;
    if(!p||!s||now<p->now_us||p->pending.active||!echoy_wire_state_encode(s,data,&n)||
       s->reference_uid!=p->uid||echoy_protocol_reference(p)!=p->uid||!state_allowed(p,s))return false;
    for(unsigned i=0;i<2;i++)if(p->peers[i].authorized&&!p->peers[i].clock.stable){p->syncing=true;return false;}
    if(p->next_transaction==UINT32_MAX)return false;
    long double bar=ceill(((long double)now+2*ECHOY_BAR_US+40000-s->epoch_us)*(1000000+s->rate_ppm)/
        ((long double)ECHOY_BAR_US*1000000));
    if(bar<1||bar>UINT32_MAX)return false;
    echoy_protocol_pending_t pending={.active=true,.coordinator=true,.state=*s,.session=p->session,
        .transaction=++p->next_transaction,.apply_bar=(uint32_t)bar,.digest=(uint32_t)echoy_get_le(data+n-4,4),.last_send_us=now};
    if(!map_state(p,p->uid,s,pending.apply_bar,&pending.epoch_us,&pending.apply_us,&pending.rate_ppm))return false;
    p->pending=pending;p->syncing=true;p->now_us=now;
    for(unsigned i=0;i<2;i++)if(member_mask(p)&(1u<<i)) {
        for(size_t off=0;off<n;off+=60) {
            echoy_packet_t m=message(p,ECHOY_PREPARE,(uint8_t)(n-off>60?60:n-off));
            m.transaction=pending.transaction;m.apply_bar=pending.apply_bar;
            m.fragment_index=(uint8_t)(off/60);m.fragment_count=(uint8_t)((n+59)/60);memcpy(m.body,data+off,m.body_length);
            (void)emit(p,p->peers[i].uid,&m);
        }
    }
    return true;
}
static bool matches(const echoy_protocol_t *p,const echoy_packet_t *m) {
    return p->pending.transaction==m->transaction&&p->pending.apply_bar==m->apply_bar&&
        echoy_session_equal(p->pending.session,m->session)&&
        p->pending.state.revision==echoy_get_le(m->body,4)&&p->pending.digest==echoy_get_le(m->body+4,4);
}
bool echoy_protocol_receive(echoy_protocol_t *p,uint64_t source,uint32_t boot,const uint8_t *data,
    size_t size,int64_t received,int64_t now) {
    echoy_packet_t m;
    if(!p||source==p->uid||!source||received<0||now<received||!echoy_wire_decode(data,size,&m)||m.sender_boot!=boot)return false;
    echoy_protocol_peer_t *q=peer(p,source);
    if(m.kind>=ECHOY_READY_MSG&&q&&!(member_mask(p)&(1u<<(unsigned)(q-p->peers))))return false;
    if(m.kind==ECHOY_HELLO) {
        if(m.body[0]<1||m.body[0]>3||m.body[1]||echoy_get_le(m.body+14,2)!=ECHOY_ALGORITHM_VERSION||
           !echoy_get_le(m.body+2,8)||!echoy_get_le(m.body+10,4))return false;
        p->discovered++;
        if(!q||!q->authorized)return true;
    }
    if(!q||!q->authorized){p->rejected++;return false;}
    if(q->boot!=boot){p->rejected++;degrade(p);return false;}
    if(m.sequence<=q->sequence){p->rejected++;return false;}
    q->sequence=m.sequence;
    /* An expired member needs fresh physical authorization; radio reappearance
     * cannot resurrect a group after the application hold window. */
    if(q->expired)return false;
    p->now_us=now>p->now_us?now:p->now_us;
    switch(m.kind) {
    case ECHOY_HELLO:
        q->advertised_members=m.body[0];q->advertised_reference=echoy_get_le(m.body+2,8);q->session=m.session;return true;
    case ECHOY_TIME_REQ: {
        uint64_t t1=echoy_get_le(m.body,8);if(t1>INT64_MAX)return false;
        echoy_packet_t response=message(p,ECHOY_TIME_RESP,24);
        echoy_put_le(response.body,t1,8);echoy_put_le(response.body+8,(uint64_t)received,8);
        echoy_put_le(response.body+16,(uint64_t)now,8);return emit(p,source,&response);
    }
    case ECHOY_TIME_RESP: {
        uint64_t t1=echoy_get_le(m.body,8),t2=echoy_get_le(m.body+8,8),t3=echoy_get_le(m.body+16,8);
        if(t1!=(uint64_t)q->time_request_us||t1>INT64_MAX||t2>INT64_MAX||t3>INT64_MAX)return false;
        return echoy_clock_observe(&q->clock,(int64_t)t1,(int64_t)t2,(int64_t)t3,received);
    }
    case ECHOY_HEARTBEAT: {
        if(echoy_get_le(m.body+4,4)>1||!echoy_get_le(m.body,4))return false;
        q->last_rx_us=received;q->lost=false;
        if(q->member&&!echoy_session_equal(m.session,p->session)&&!p->pending.active)degrade(p);
        uint32_t revision=(uint32_t)echoy_get_le(m.body,4);
        if(echoy_session_equal(m.session,p->session)&&q->member&&revision!=p->committed.revision) {
            if(!(p->pending.active&&now<p->pending.apply_us&&revision==p->pending.state.revision))degrade(p);
        } else if(p->pending.active&&echoy_session_equal(m.session,p->pending.session)&&now>=p->pending.apply_us&&
                  revision==p->pending.state.revision&&revision!=p->committed.revision)degrade(p);
        return true;
    }
    case ECHOY_PREPARE: {
        if(source!=echoy_protocol_reference(p)||p->pending.active)return false;
        echoy_wire_state_t s;echoy_wire_result_t result=echoy_wire_assemble(&q->assembly,source,boot,&m,now,&s);
        if(result==ECHOY_WIRE_FULL)p->queue_full++;
        if(result==ECHOY_WIRE_MORE)return true;
        if(result!=ECHOY_WIRE_OK||s.reference_uid!=source||!echoy_session_equal(m.session,q->session)||!state_allowed(p,&s))return false;
        echoy_protocol_pending_t pending={.active=true,.state=s,.session=m.session,.transaction=m.transaction,.apply_bar=m.apply_bar,.last_send_us=now};
        uint8_t bytes[960];size_t n;if(!echoy_wire_state_encode(&s,bytes,&n))return false;
        pending.digest=(uint32_t)echoy_get_le(bytes+n-4,4);
        if(!map_state(p,source,&s,m.apply_bar,&pending.epoch_us,&pending.apply_us,&pending.rate_ppm)||
           pending.apply_us-now<2*ECHOY_BAR_US){p->syncing=true;return false;}
        p->pending=pending;p->syncing=true;control(p,source,ECHOY_READY_MSG);return true;
    }
    case ECHOY_READY_MSG:
        if(!p->pending.active||!p->pending.coordinator||now>=p->pending.apply_us||!matches(p,&m))return false;
        p->pending.ready_mask|=1u<<(unsigned)(q-p->peers);
        if(p->pending.ready_mask==member_mask(p)) {
            p->pending.commit_seen=true;
            for(unsigned i=0;i<2;i++)if(member_mask(p)&(1u<<i))control(p,p->peers[i].uid,ECHOY_COMMIT);
        }return true;
    case ECHOY_COMMIT:
        if(p->pending.coordinator||source!=p->pending.state.reference_uid||!matches(p,&m))return false;
        if(p->pending.active&&now<p->pending.apply_us)p->pending.commit_seen=true;
        else if(p->committed.revision!=p->pending.state.revision)return false;
        control(p,source,ECHOY_ACK);return true;
    case ECHOY_ACK:
        if(!p->pending.coordinator||!matches(p,&m))return false;
        p->pending.ack_mask|=1u<<(unsigned)(q-p->peers);return true;
    case ECHOY_ABORT:
        if(!p->pending.active||!matches(p,&m))return false;
        degrade(p);return true;
    default:return false;
    }
}
void echoy_protocol_tick(echoy_protocol_t *p,int64_t now) {
    if(!p||now<p->now_us)return;
    p->now_us=now;
    for(unsigned i=0;i<2;i++) {
        echoy_protocol_peer_t *q=&p->peers[i];if(!q->authorized)continue;
        if(!q->lost&&now-q->last_rx_us>=ECHOY_HEARTBEAT_US*ECHOY_HEARTBEAT_MISSES) {
            q->lost=true;q->lost_us=q->last_rx_us+ECHOY_HEARTBEAT_US*ECHOY_HEARTBEAT_MISSES;
            if(q->member||p->pending.active)degrade(p);
        }
        if(q->lost&&now-q->lost_us>=ECHOY_PROTOCOL_HOLD_US)q->expired=true;
    }
    if(p->pending.active&&now>=p->pending.apply_us) {
        if(!p->pending.commit_seen)degrade(p);
        else {
            p->session=p->pending.session;p->committed=p->pending.state;
            for(unsigned i=0;i<2;i++)p->peers[i].member=(member_mask(p)&(1u<<i))!=0;
            p->pending.active=false;p->syncing=false;p->degraded=false;
            if(p->commit)p->commit(p->ctx,&p->committed,p->session,p->pending.epoch_us,p->pending.rate_ppm,p->pending.apply_us);
        }
    }
    if(now-p->last_periodic_us<ECHOY_HEARTBEAT_US)return;
    p->last_periodic_us=now;
    echoy_packet_t hello=message(p,ECHOY_HELLO,16);hello.body[0]=p->committed.member_count;
    echoy_put_le(hello.body+2,p->committed.reference_uid,8);echoy_put_le(hello.body+10,p->committed.revision,4);
    echoy_put_le(hello.body+14,ECHOY_ALGORITHM_VERSION,2);(void)emit(p,0,&hello);
    for(unsigned i=0;i<2;i++) {
        echoy_protocol_peer_t *q=&p->peers[i];if(!q->authorized||q->expired)continue;
        echoy_packet_t heart=message(p,ECHOY_HEARTBEAT,8);echoy_put_le(heart.body,p->committed.revision,4);
        echoy_put_le(heart.body+4,p->degraded?1:0,4);(void)emit(p,q->uid,&heart);
        echoy_packet_t request=message(p,ECHOY_TIME_REQ,8);q->time_request_us=now;
        echoy_put_le(request.body,(uint64_t)now,8);(void)emit(p,q->uid,&request);
        if(p->pending.coordinator&&p->pending.commit_seen&&now<=p->pending.apply_us+750000&&
           !(p->pending.ack_mask&(1u<<i))&&(member_mask(p)&(1u<<i)))control(p,q->uid,ECHOY_COMMIT);
        else if(p->pending.active&&!p->pending.coordinator&&!p->pending.commit_seen&&
                q->uid==p->pending.state.reference_uid)control(p,q->uid,ECHOY_READY_MSG);
    }
}
