// SPDX-License-Identifier: Apache-2.0
#include "echoy_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {uint64_t source,target;uint32_t boot;uint8_t data[100];size_t size;int64_t due;} flight_t;
static struct {echoy_protocol_t nodes[3];flight_t flights[512];size_t count;int64_t now;unsigned commits[3];
    unsigned partition;int drop_kind,drop_target;bool duplicate,reorder;} net;
static bool send_packet(void *ctx,uint64_t target,const uint8_t *data,size_t size) {
    int from=(int)(intptr_t)ctx;
    if(net.partition&(1u<<from))return true;
    for(int to=0;to<3;to++)if(to!=from&&(!target||target==net.nodes[to].uid)) {
        if((net.partition&(1u<<to))||(data[3]==net.drop_kind&&to==net.drop_target))continue;
        assert(net.count<512);flight_t *f=&net.flights[net.count++];
        *f=(flight_t){.source=net.nodes[from].uid,.target=net.nodes[to].uid,.boot=net.nodes[from].boot,
            .size=size,.due=net.now+500};memcpy(f->data,data,size);
        if(net.duplicate){assert(net.count<512);net.flights[net.count++]=*f;}
    }return true;
}
static void commit(void *ctx,const echoy_wire_state_t *s,echoy_session_key_t key,int64_t epoch,int ppm,int64_t at) {
    (void)key;(void)epoch;(void)ppm;(void)at;assert(s->revision==2);net.commits[(int)(intptr_t)ctx]++;
}
static void deliver(void) {
    unsigned guard=0;
    while(net.count) {
        assert(++guard<20000);size_t ix=net.reorder?net.count-1:0;
        flight_t f=net.flights[ix];memmove(net.flights+ix,net.flights+ix+1,(--net.count-ix)*sizeof f);
        int to=(int)(f.target/11)-1;if(f.due<net.now)f.due=net.now;net.now=f.due;
        (void)echoy_protocol_receive(&net.nodes[to],f.source,f.boot,f.data,f.size,f.due,f.due);
    }
}
static void advance(int64_t to) {
    int64_t start=net.now;
    for(int64_t t=start+250000;t<=to;t+=250000){net.now=t;for(int i=0;i<3;i++)echoy_protocol_tick(&net.nodes[i],t);deliver();}
    if(net.now<to)net.now=to;
    for(int i=0;i<3;i++)echoy_protocol_tick(&net.nodes[i],net.now);
    deliver();
}
static void setup(bool authorize) {
    memset(&net,0,sizeof net);net.drop_kind=-1;net.drop_target=-1;
    for(int i=0;i<3;i++)echoy_protocol_init(&net.nodes[i],11u*(i+1),i+1,7,send_packet,NULL,commit,(void *)(intptr_t)i);
    if(authorize)for(int i=0;i<3;i++)for(int j=0;j<3;j++)if(i!=j)
        assert(echoy_protocol_authorize(&net.nodes[i],net.nodes[j].uid,net.nodes[j].boot,0));
    advance(22000000);
}
static echoy_wire_state_t group(void) {
    return (echoy_wire_state_t){.revision=2,.reference_uid=11,.epoch_us=0,.member_count=3,.edge_count=2,
        .members={{.uid=11,.boot=1,.phrase=1},{.uid=22,.boot=2,.phrase=1},{.uid=33,.boot=3,.phrase=1}},.edges={{0,1},{1,2}}};
}
int main(void) {
    setup(false);for(int i=0;i<3;i++)assert(net.nodes[i].committed.member_count==1);
    echoy_wire_state_t s=group();assert(!echoy_protocol_begin(&net.nodes[0],&s,net.now));
    setup(true);assert(echoy_protocol_reference(&net.nodes[2])==11);
    assert(echoy_protocol_begin(&net.nodes[0],&s,net.now));deliver();
    int64_t boundary=net.nodes[0].pending.apply_us;assert(boundary>=net.now+4900000);
    advance(boundary+10000);
    for(int i=0;i<3;i++){assert(net.commits[i]==1);assert(net.nodes[i].committed.member_count==3);}
    /* No physical edge APIs are used after authorization: GAME_EVENT sustains the group. */
    advance(boundary+2000000);for(int i=0;i<3;i++)assert(!net.nodes[i].degraded);
    setup(true);net.duplicate=true;assert(echoy_protocol_begin(&net.nodes[0],&s,net.now));deliver();
    boundary=net.nodes[0].pending.apply_us;advance(boundary+10000);for(int i=0;i<3;i++)assert(net.commits[i]==1);
    setup(true);net.drop_kind=ECHOY_COMMIT;net.drop_target=2;
    assert(echoy_protocol_begin(&net.nodes[0],&s,net.now));deliver();boundary=net.nodes[0].pending.apply_us;
    advance(boundary+1000000);assert(net.commits[2]==0);assert(net.nodes[2].committed.member_count==1);
    assert(net.nodes[2].degraded);
    setup(true);assert(echoy_protocol_begin(&net.nodes[0],&s,net.now));deliver();boundary=net.nodes[0].pending.apply_us;
    net.partition=1;advance(boundary+10000);assert(net.commits[1]==0&&net.commits[2]==0);
    setup(true);net.reorder=true;assert(echoy_protocol_begin(&net.nodes[0],&s,net.now));deliver();
    boundary=net.nodes[0].pending.apply_us;advance(boundary+10000);assert(net.commits[1]==0&&net.commits[2]==0);
    setup(true);assert(echoy_protocol_begin(&net.nodes[0],&s,net.now));deliver();
    echoy_protocol_init(&net.nodes[0],11,100,8,send_packet,NULL,commit,NULL);advance(net.now+1000000);
    assert(net.commits[1]==0&&net.commits[2]==0);assert(net.nodes[1].degraded);
    setup(true);net.drop_kind=ECHOY_HEARTBEAT;net.drop_target=1;
    advance(net.now+1000000);assert(net.nodes[1].peers[0].lost);
    puts("three-node delay/loss/duplicate/reorder/partition/reboot checks passed");return 0;
}
