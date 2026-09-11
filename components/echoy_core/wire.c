// SPDX-License-Identifier: Apache-2.0
#include "echoy_wire.h"
#include "state_internal.h"
#include <string.h>
uint64_t echoy_get_le(const uint8_t *p,unsigned n) {
    uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;
}
void echoy_put_le(uint8_t *p,uint64_t v,unsigned n) {for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
bool echoy_session_equal(echoy_session_key_t a,echoy_session_key_t b) {
    return a.creator_uid==b.creator_uid&&a.creator_boot==b.creator_boot&&a.nonce==b.nonce;
}
uint32_t echoy_wire_digest(const uint8_t *p,size_t n) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<n;i++){crc^=p[i];for(int bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    return ~crc;
}
static bool packet_valid(const echoy_packet_t *p) {
    static const uint8_t sizes[]={0,16,8,24,8,0,8,8,8,8};
    if(!p||p->kind<1||p->kind>9||!p->sender_boot||!p->sequence||(p->flags && (p->kind!=ECHOY_PREPARE||p->flags!=1))||
       !p->session.creator_uid||!p->session.creator_boot||!p->session.nonce||
       !p->fragment_count||p->fragment_count>16||p->fragment_index>=p->fragment_count||
       !p->body_length||p->body_length>60)return false;
    if(p->kind>=ECHOY_PREPARE) {if(!p->transaction||!p->apply_bar)return false;}
    else if(p->transaction||p->apply_bar)return false;
    if(p->kind==ECHOY_PREPARE)return p->fragment_index+1==p->fragment_count||p->body_length==60;
    return p->fragment_count==1&&!p->fragment_index&&p->body_length==sizes[p->kind];
}
bool echoy_wire_encode(const echoy_packet_t *p,uint8_t out[100],size_t *n) {
    if(!out||!n||!packet_valid(p))return false;
    memset(out,0,40);echoy_put_le(out,0x4543,2);out[2]=1;out[3]=(uint8_t)p->kind;
    echoy_put_le(out+4,p->sender_boot,4);echoy_put_le(out+8,p->sequence,4);
    echoy_put_le(out+12,p->session.creator_uid,8);echoy_put_le(out+20,p->session.creator_boot,4);
    echoy_put_le(out+24,p->session.nonce,4);echoy_put_le(out+28,p->transaction,4);
    echoy_put_le(out+32,p->apply_bar,4);out[36]=p->fragment_index;out[37]=p->fragment_count;
    out[38]=p->body_length;out[39]=p->flags;memcpy(out+40,p->body,p->body_length);*n=40+p->body_length;return true;
}
bool echoy_wire_decode(const uint8_t *data,size_t n,echoy_packet_t *out) {
    if(!data||!out||n<40||n>100||echoy_get_le(data,2)!=0x4543||data[2]!=1||n!=(size_t)40+data[38])return false;
    echoy_packet_t p={.kind=(echoy_wire_kind_t)data[3],.sender_boot=(uint32_t)echoy_get_le(data+4,4),
        .sequence=(uint32_t)echoy_get_le(data+8,4),.session={echoy_get_le(data+12,8),
        (uint32_t)echoy_get_le(data+20,4),(uint32_t)echoy_get_le(data+24,4)},
        .transaction=(uint32_t)echoy_get_le(data+28,4),.apply_bar=(uint32_t)echoy_get_le(data+32,4),
        .fragment_index=data[36],.fragment_count=data[37],.body_length=data[38],.flags=data[39]};
    if(!packet_valid(&p))return false;
    memcpy(p.body,data+40,p.body_length);*out=p;return true;
}
static bool state_valid(const echoy_wire_state_t *s) {
    if(!s||!s->revision||!s->member_count||s->member_count>3||s->edge_count>2||
       s->epoch_us<0||s->epoch_us>INT64_MAX-3*ECHOY_BAR_US||s->rate_ppm < -500||s->rate_ppm>500)return false;
    bool reference=false,request=!s->transport_request_uid;
    for(unsigned i=0;i<s->member_count;i++) {
        const echoy_wire_member_t *m=&s->members[i];
        if(!m->uid||!m->boot||m->role>3||m->phrase<1||m->phrase>4||m->activity>1000||m->transport>2)return false;
        for(unsigned j=0;j<i;j++)if(s->members[j].uid==m->uid)return false;
        reference|=m->uid==s->reference_uid;request|=m->uid==s->transport_request_uid;
    }
    unsigned in=0,out=0;
    for(unsigned i=0;i<s->edge_count;i++) {
        unsigned a=s->edges[i][0],b=s->edges[i][1];
        if(a>=s->member_count||b>=s->member_count||a==b||(out&(1u<<a))||(in&(1u<<b)))return false;
        out|=1u<<a;in|=1u<<b;
        for(unsigned j=0;j<i;j++)if(s->edges[j][0]==b&&s->edges[j][1]==a)return false;
    }
    return reference&&request;
}
bool echoy_wire_state_encode(const echoy_wire_state_t *s,uint8_t out[960],size_t *n) {
    if(!out||!n||!state_valid(s))return false;
    *n=44+24*s->member_count+2*s->edge_count;memset(out,0,*n);
    echoy_put_le(out,ECHOY_ALGORITHM_VERSION,2);out[2]=s->member_count;out[3]=s->edge_count;
    echoy_put_le(out+4,s->revision,4);echoy_put_le(out+8,(uint64_t)s->epoch_us,8);
    echoy_put_le(out+16,(uint32_t)s->rate_ppm,4);echoy_put_le(out+20,s->reference_uid,8);
    echoy_put_le(out+28,s->transport_request_uid,8);
    for(unsigned i=0;i<s->member_count;i++) {
        uint8_t *p=out+40+24*i;const echoy_wire_member_t *m=&s->members[i];
        echoy_put_le(p,m->uid,8);echoy_put_le(p+8,m->boot,4);p[12]=m->role;p[13]=m->phrase;
        echoy_put_le(p+14,m->activity,2);p[16]=m->motif_available;p[17]=m->transport;
    }
    memcpy(out+40+24*s->member_count,s->edges,2*s->edge_count);
    echoy_put_le(out+*n-4,echoy_wire_digest(out,*n-4),4);return true;
}
bool echoy_wire_state_decode(const uint8_t *p,size_t n,echoy_wire_state_t *out) {
    if(!p||!out||n<68||n>960||echoy_get_le(p,2)!=ECHOY_ALGORITHM_VERSION||
       p[2]<1||p[2]>3||p[3]>2||n!=(size_t)44+24*p[2]+2*p[3]||echoy_get_le(p+36,4)||
       echoy_get_le(p+n-4,4)!=echoy_wire_digest(p,n-4)||echoy_get_le(p+8,8)>INT64_MAX)return false;
    uint32_t rate=(uint32_t)echoy_get_le(p+16,4);
    int64_t signed_rate=rate<=INT32_MAX?(int64_t)rate:(int64_t)rate-INT64_C(4294967296);
    echoy_wire_state_t s={.revision=(uint32_t)echoy_get_le(p+4,4),.epoch_us=(int64_t)echoy_get_le(p+8,8),
        .rate_ppm=(int32_t)signed_rate,.reference_uid=echoy_get_le(p+20,8),.transport_request_uid=echoy_get_le(p+28,8),
        .member_count=p[2],.edge_count=p[3]};
    for(unsigned i=0;i<s.member_count;i++) {
        const uint8_t *q=p+40+24*i;
        if(q[16]>1||echoy_get_le(q+18,6))return false;
        s.members[i]=(echoy_wire_member_t){.uid=echoy_get_le(q,8),.boot=(uint32_t)echoy_get_le(q+8,4),
            .role=q[12],.phrase=q[13],.activity=(uint16_t)echoy_get_le(q+14,2),.motif_available=q[16]!=0,.transport=q[17]};
    }
    memcpy(s.edges,p+40+24*s.member_count,2*s.edge_count);
    if(!state_valid(&s))return false;
    *out=s;return true;
}
echoy_wire_result_t echoy_wire_assemble(echoy_reassembler_t *r,uint64_t source,uint32_t boot,
    const echoy_packet_t *p,int64_t now,echoy_wire_state_t *out) {
    if(!r||!p||!out||now<0||source!=r->uid||boot!=r->boot||p->sender_boot!=boot||
       p->kind!=ECHOY_PREPARE||!packet_valid(p))return ECHOY_WIRE_BAD;
    if(p->sequence<=r->sequence||p->transaction<=r->completed_transaction)return ECHOY_WIRE_REPLAY;
    echoy_assembly_t *a=NULL,*free_slot=NULL;
    for(unsigned i=0;i<2;i++) {
        echoy_assembly_t *slot=&r->transactions[i];
        if(slot->used&&now-slot->started_us>2000000)slot->used=false;
        if(slot->used&&slot->first.transaction==p->transaction)a=slot;
        if(!slot->used)free_slot=slot;
    }
    if(!a) {
        if(p->fragment_index)return ECHOY_WIRE_BAD;
        if(!free_slot)return ECHOY_WIRE_FULL;
        a=free_slot;memset(a,0,sizeof *a);a->used=true;a->first=*p;a->started_us=now;
    }
    if(p->fragment_index!=a->next||p->fragment_count!=a->first.fragment_count||
       p->apply_bar!=a->first.apply_bar||p->flags!=a->first.flags||!echoy_session_equal(p->session,a->first.session)||
       a->size+p->body_length>960)return ECHOY_WIRE_BAD;
    r->sequence=p->sequence;memcpy(a->data+a->size,p->body,p->body_length);a->size+=p->body_length;a->next++;
    if(a->next<p->fragment_count)return ECHOY_WIRE_MORE;
    a->used=false;r->completed_transaction=p->transaction;
    return echoy_wire_state_decode(a->data,a->size,out)?ECHOY_WIRE_OK:ECHOY_WIRE_BAD;
}

echoy_result_t echoy_core_install(echoy_core_t *c,int local,const echoy_wire_state_t *s,
    uint64_t session_id,int64_t epoch,int rate,int64_t now) {
    if(!c||local<0||local>=3||!state_valid(s)||!session_id||rate < -1000||rate>1000||
       now<c->time_us||now>INT64_MAX-ECHOY_BAR_US||epoch>now||epoch<now-INT64_C(31536000000000))return ECHOY_INVALID;
    int slots[3]={-1,-1,-1};unsigned members=0;int leader=-1;
    for(unsigned i=0;i<s->member_count;i++) {
        for(int j=0;j<3;j++)if(c->devices[j].uid==s->members[i].uid)slots[i]=j;
        int j=slots[i];if(j<0||c->devices[j].boot_id!=s->members[i].boot||c->devices[j].lifecycle!=ECHOY_READY||!c->devices[j].battery)return ECHOY_STALE;
        members|=1u<<j;if(s->members[i].uid==s->reference_uid)leader=j;
    }
    if(!(members&(1u<<local))||leader<0)return ECHOY_INVALID;
    /* Validate first, then replace the complete chain in one owner operation. */
    for(int i=0;i<3;i++)if((c->sessions[i].members&members)||c->sessions[i].id==session_id) {
        if(c->sessions[i].members&~members)return ECHOY_INVALID;
    }
    echoy_tick(c,now);
    for(int i=0;i<3;i++)if(c->sessions[i].members&members)memset(&c->sessions[i],0,sizeof c->sessions[i]);
    int ix=0;while(ix<3&&c->sessions[ix].id)ix++;
    if(ix==3)return ECHOY_INVALID;
    c->sessions[ix]=(echoy_session_t){.id=session_id,.members=members,.leader=leader,.epoch_us=epoch,
        .created_us=now,.last_bar=-1,.rate_ppm=rate,.wire_managed=true};
    unsigned edges=0;
    for(unsigned i=0;i<s->edge_count;i++)edges|=1u<<(slots[s->edges[i][0]]*3+slots[s->edges[i][1]]);
    for(int a=0;a<3;a++)for(int b=0;b<3;b++)if((members&(1u<<a))&&(members&(1u<<b)))c->committed_edges&=~(1u<<(a*3+b));
    c->committed_edges|=edges;
    for(unsigned i=0;i<s->member_count;i++) {
        echoy_device_t *d=&c->devices[slots[i]];const echoy_wire_member_t *m=&s->members[i];
        d->session_id=session_id;d->clock_version++;d->role=(echoy_role_t)m->role;d->phrase=m->phrase;
        d->committed_activity=m->activity/1000.0;d->motif_available=m->motif_available;
        if(slots[i]!=local){d->transport=(echoy_transport_t)m->transport;d->activity=d->committed_activity;}
    }
    c->sessions[ix].last_bar=(int64_t)(echoy_phase_steps(&c->sessions[ix],now)/16);
    c->sync.stage=ECHOY_SYNC_NONE;c->revision++;echoy_publish(c);return ECHOY_OK;
}
