// SPDX-License-Identifier: Apache-2.0
#include "echoy_wire.h"
#include <assert.h>
#include <string.h>
static echoy_wire_state_t state(void) {
    return (echoy_wire_state_t){.revision=7,.epoch_us=100000,.reference_uid=11,.member_count=2,.edge_count=1,
        .members={{.uid=11,.boot=1,.phrase=1,.activity=500,.motif_available=true},
                  {.uid=22,.boot=2,.phrase=4,.role=3,.activity=1000,.transport=2}},.edges={{0,1}}};
}
int main(void) {
    uint8_t data[960], packet[100]; size_t n,len; echoy_packet_t decoded;
    echoy_packet_t p={.kind=ECHOY_TIME_REQ,.sender_boot=0x12345678,.sequence=1,
        .session={11,1,9},.fragment_count=1,.body_length=8};
    assert(echoy_wire_encode(&p,packet,&len)); assert(len==48);
    assert(packet[0]==0x43 && packet[1]==0x45 && packet[4]==0x78 && packet[7]==0x12);
    assert(echoy_wire_decode(packet,len,&decoded));
    for(size_t i=0;i<len;i++)assert(!echoy_wire_decode(packet,i,&decoded));
    packet[2]=2;assert(!echoy_wire_decode(packet,len,&decoded));packet[2]=1;
    packet[39]=1;assert(!echoy_wire_decode(packet,len,&decoded));packet[39]=0;
    for(int kind=1;kind<=9;kind++) { const uint8_t lengths[]={0,16,8,24,8,1,8,8,8,8};
        p.kind=(echoy_wire_kind_t)kind;p.body_length=lengths[kind];p.transaction=kind>=5?1:0;
        p.apply_bar=kind>=5?5:0;assert(echoy_wire_encode(&p,packet,&len));
        assert(echoy_wire_decode(packet,len,&decoded));p.body_length=0;assert(!echoy_wire_encode(&p,packet,&len)); }
    echoy_wire_state_t s=state(), out;assert(echoy_wire_state_encode(&s,data,&n));
    assert(echoy_wire_state_decode(data,n,&out));assert(out.members[1].uid==22 && out.edges[0][1]==1);
    data[55]^=1;assert(!echoy_wire_state_decode(data,n,&out));data[55]^=1;
    s.edges[1][0]=1;s.edges[1][1]=0;s.edge_count=2;assert(!echoy_wire_state_encode(&s,data,&n));s=state();
    assert(echoy_wire_state_encode(&s,data,&n));
    echoy_reassembler_t r={.uid=11,.boot=1};
    p=(echoy_packet_t){.kind=ECHOY_PREPARE,.sender_boot=1,.session={11,1,9},.transaction=1,
        .sequence=1,.apply_bar=5,.fragment_count=(uint8_t)((n+59)/60),.body_length=60};
    memcpy(p.body,data,60); assert(echoy_wire_assemble(&r,22,1,&p,0,&out)==ECHOY_WIRE_BAD);
    assert(echoy_wire_assemble(&r,11,2,&p,0,&out)==ECHOY_WIRE_BAD);
    assert(echoy_wire_assemble(&r,11,1,&p,0,&out)==ECHOY_WIRE_MORE);
    assert(echoy_wire_assemble(&r,11,1,&p,1,&out)==ECHOY_WIRE_REPLAY);
    p.sequence++;p.fragment_index=1;p.body_length=(uint8_t)(n-60);memcpy(p.body,data+60,n-60);
    assert(echoy_wire_assemble(&r,11,1,&p,2,&out)==ECHOY_WIRE_OK);
    p.sequence++;assert(echoy_wire_assemble(&r,11,1,&p,3,&out)==ECHOY_WIRE_REPLAY);
    p.transaction=2;assert(echoy_wire_assemble(&r,11,1,&p,4,&out)==ECHOY_WIRE_BAD);
    /* Two incomplete transactions per source; a third cannot evict them. */
    memset(&r,0,sizeof r);r.uid=11;r.boot=1;
    p.fragment_index=0;p.body_length=60;memcpy(p.body,data,60);
    p.transaction=10;p.sequence=10;assert(echoy_wire_assemble(&r,11,1,&p,10,&out)==ECHOY_WIRE_MORE);
    p.transaction=11;p.sequence=11;assert(echoy_wire_assemble(&r,11,1,&p,11,&out)==ECHOY_WIRE_MORE);
    p.transaction=12;p.sequence=12;assert(echoy_wire_assemble(&r,11,1,&p,12,&out)==ECHOY_WIRE_FULL);
    assert(echoy_wire_assemble(&r,11,1,&p,2000012,&out)==ECHOY_WIRE_MORE);
    p.fragment_count=17;assert(echoy_wire_assemble(&r,11,1,&p,2000013,&out)==ECHOY_WIRE_BAD);
    echoy_core_t core;uint64_t uids[3]={11,22,33};echoy_core_init(&core,uids);
    for(int i=0;i<2;i++) {
        echoy_input_t e={.type=ECHOY_BOOT,.device=i,.boot_id=(uint64_t)i+1};assert(echoy_dispatch(&core,&e)==ECHOY_OK);
        e.type=ECHOY_BOOT_READY;assert(echoy_dispatch(&core,&e)==ECHOY_OK);
    }
    core.devices[0].volume=.3;core.devices[0].activity=.7;s=state();
    assert(echoy_core_install(&core,0,&s,99,100000,0,5100000)==ECHOY_OK);
    assert(core.devices[0].transport==ECHOY_PAUSED && core.devices[0].volume==.3);
    assert(core.devices[0].committed_activity==.5 && core.devices[0].activity==.7);
    assert(core.committed_edges==(1u<<1));assert(!core.devices[1].motif_available);
    echoy_tick(&core,10100000);assert(core.committed_edges==(1u<<1));assert(!core.devices[1].motif_available);
    echoy_input_t input={.type=ECHOY_PAUSE,.device=1,.at_us=10100000};
    assert(echoy_dispatch(&core,&input)==ECHOY_OK);
    input.type=ECHOY_FINISH_ACTIVITY;input.device=0;assert(echoy_dispatch(&core,&input)==ECHOY_OK);
    assert(core.devices[0].committed_activity==.5);
    s.members[1].boot=50;assert(echoy_core_install(&core,0,&s,99,100000,0,10100000)==ECHOY_STALE);
    return 0;
}
