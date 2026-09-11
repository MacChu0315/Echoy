// SPDX-License-Identifier: Apache-2.0
#include "echoy_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
typedef struct {uint64_t source,target;uint32_t boot;uint8_t data[100];size_t size;int64_t due;} flight_t;
static struct {echoy_protocol_t nodes[3];flight_t flights[512];size_t count;int64_t now;unsigned commits[3];
    unsigned partition;int drop_kind,drop_target;bool duplicate,reorder,unstable;
    long double offsets[3],rates[3];int64_t committed_at[3],committed_epoch[3];int committed_ppm[3];
    long double phase_error_us[3],mapped_error_us[3];} net;
static int64_t local_time(int node,int64_t real) {
    return (int64_t)llroundl(net.offsets[node]+real*(1+net.rates[node]/1000000));
}
static int64_t real_time(int node,int64_t local) {
    return (int64_t)llroundl((local-net.offsets[node])/(1+net.rates[node]/1000000));
}
static int64_t boundary_time(void) {return real_time(0,net.nodes[0].pending.apply_us);}
static bool begin_group(const echoy_wire_state_t *state) {
    return echoy_protocol_begin(&net.nodes[0],state,local_time(0,net.now));
}
static bool send_packet(void *ctx,uint64_t target,const uint8_t *data,size_t size) {
    int from=(int)(intptr_t)ctx;
    if(net.partition&(1u<<from))return true;
    for(int to=0;to<3;to++)if(to!=from&&(!target||target==net.nodes[to].uid)) {
        if((net.partition&(1u<<to))||(data[3]==net.drop_kind&&(to==net.drop_target||net.drop_target<0)))continue;
        assert(net.count<512);flight_t *f=&net.flights[net.count++];
        *f=(flight_t){.source=net.nodes[from].uid,.target=net.nodes[to].uid,.boot=net.nodes[from].boot,
            .size=size,.due=net.now+500};memcpy(f->data,data,size);
        if(net.unstable&&to==1&&data[3]==ECHOY_TIME_RESP) {
            /* Distort only remote clock observations, preserving honest heartbeat delivery. */
            uint64_t shift=((net.now/250000)&1)?12000:0;
            echoy_put_le(f->data+48,echoy_get_le(f->data+48,8)+shift,8);
            echoy_put_le(f->data+56,echoy_get_le(f->data+56,8)+shift,8);
        }
        if(net.duplicate){assert(net.count<512);net.flights[net.count++]=*f;}
    }return true;
}
static void commit(void *ctx,const echoy_wire_state_t *s,echoy_session_key_t key,int64_t epoch,int ppm,int64_t at) {
    int node=(int)(intptr_t)ctx;assert(s->revision==2);assert(key.creator_uid==11&&key.creator_boot==1&&key.nonce==7);
    uint32_t bar=net.nodes[node].pending.apply_bar;
    long double reference=s->epoch_us+(long double)bar*2500000*1000000/(1000000+s->rate_ppm);
    long double expected_real=(reference-net.offsets[0])/(1+net.rates[0]/1000000);
    long double expected_local=net.offsets[node]+expected_real*(1+net.rates[node]/1000000);
    net.mapped_error_us[node]=fabsl(at-expected_local);
    assert(net.mapped_error_us[node]<2000); /* Independently derived from each physical clock. */
    long double stored_boundary=epoch+(long double)bar*2500000*1000000/(1000000+ppm);
    net.phase_error_us[node]=fabsl(stored_boundary-at);
    assert(at>net.committed_at[node]);
    net.committed_at[node]=at;net.committed_epoch[node]=epoch;net.committed_ppm[node]=ppm;net.commits[node]++;
}
static void deliver(void) {
    unsigned guard=0;
    while(net.count) {
        assert(++guard<20000);size_t ix=net.reorder?net.count-1:0;
        flight_t f=net.flights[ix];memmove(net.flights+ix,net.flights+ix+1,(--net.count-ix)*sizeof f);
        int to=(int)(f.target/11)-1;if(f.due<net.now)f.due=net.now;net.now=f.due;
        (void)echoy_protocol_receive(&net.nodes[to],f.source,f.boot,f.data,f.size,local_time(to,f.due),local_time(to,f.due));
    }
}
static void advance(int64_t to) {
    int64_t start=net.now;
    for(int64_t t=start+250000;t<=to;t+=250000){net.now=t;for(int i=0;i<3;i++)echoy_protocol_tick(&net.nodes[i],local_time(i,t));deliver();}
    if(net.now<to)net.now=to;
    for(int i=0;i<3;i++)echoy_protocol_tick(&net.nodes[i],local_time(i,net.now));
    deliver();
}
static void setup(bool authorize) {
    memset(&net,0,sizeof net);net.drop_kind=-1;net.drop_target=-1;
    net.offsets[0]=1000000;net.offsets[1]=2500000;net.offsets[2]=250000;
    net.rates[0]=0;net.rates[1]=100.4L;net.rates[2]=-37.6L;
    for(int i=0;i<3;i++)echoy_protocol_init(&net.nodes[i],11u*(i+1),i+1,7,send_packet,NULL,commit,(void *)(intptr_t)i);
    if(authorize)for(int i=0;i<3;i++)for(int j=0;j<3;j++)if(i!=j)
        assert(echoy_protocol_authorize(&net.nodes[i],net.nodes[j].uid,net.nodes[j].boot,local_time(i,0)));
    advance(22000000);
}
static echoy_wire_state_t group(void) {
    return (echoy_wire_state_t){.revision=2,.reference_uid=11,.epoch_us=0,.member_count=3,.edge_count=2,
        .members={{.uid=11,.boot=1,.phrase=1},{.uid=22,.boot=2,.phrase=1},{.uid=33,.boot=3,.phrase=1}},.edges={{0,1},{1,2}}};
}
static void clock_failure(bool unstable) {
    setup(true);echoy_wire_state_t s=group();assert(begin_group(&s));deliver();
    int64_t boundary=boundary_time();
    for(int i=0;i<3;i++)assert(net.nodes[i].pending.active&&net.nodes[i].pending.commit_seen);
    if(unstable)net.unstable=true;else {net.drop_kind=ECHOY_TIME_RESP;net.drop_target=1;}
    advance(net.now+1000000);
    for(int i=0;i<3;i++) {
        assert(!net.nodes[i].peers[0].lost); /* Heartbeats continue; loss cannot mask this test. */
        assert(net.nodes[i].degraded&&!net.nodes[i].pending.active);
    }
    advance(boundary+10000);
    for(int i=0;i<3;i++)assert(net.commits[i]==0&&net.nodes[i].committed.revision==1&&net.nodes[i].degraded);
    net.unstable=false;net.drop_kind=-1;advance(net.now+22000000);
    for(int i=0;i<3;i++)assert(net.nodes[i].degraded); /* Good clock alone cannot install. */
    assert(begin_group(&s));deliver();advance(boundary_time()+10000);
    for(int i=0;i<3;i++)assert(net.commits[i]==1&&!net.nodes[i].degraded);
}
static void long_clocks(void) {
    setup(true);advance(INT64_C(43200000000)); /* 12 h of independent clock exchanges. */
    echoy_wire_state_t s=group();assert(begin_group(&s));deliver();advance(boundary_time()+10000);
    for(int i=0;i<3;i++) {
        assert(net.commits[i]==1);fprintf(stderr,"node %d stored boundary error %.3Lf us, physical clock error %.3Lf us\n",i,net.phase_error_us[i],net.mapped_error_us[i]);
        assert(net.phase_error_us[i]<=1.1L);
        assert(net.committed_ppm[i]==(i==0?0:i==1?-100:38));
        echoy_core_t core;const uint64_t uids[]={11,22,33};echoy_core_init(&core,uids);
        for(int j=0;j<3;j++) {
            echoy_input_t e={.type=ECHOY_BOOT,.device=j,.boot_id=(uint64_t)j+1};
            assert(echoy_dispatch(&core,&e)==ECHOY_OK);e.type=ECHOY_BOOT_READY;
            assert(echoy_dispatch(&core,&e)==ECHOY_OK);
        }
        assert(echoy_core_install(&core,i,&net.nodes[i].committed,99,net.committed_epoch[i],
            net.committed_ppm[i],net.committed_at[i])==ECHOY_OK);
        echoy_session_t *installed=NULL;for(int j=0;j<3;j++)if(core.sessions[j].id==99)installed=&core.sessions[j];
        assert(installed);
        long double bar_at_commit=echoy_phase_steps(installed,net.committed_at[i])/16;
        assert(fabsl(bar_at_commit-net.nodes[i].pending.apply_bar)*2500000<=1.1L);
        assert(echoy_phase_steps(installed,net.committed_at[i]+1)>=bar_at_commit*16);
        long double phase=(local_time(i,net.now)-net.committed_epoch[i])*(1000000.L+net.committed_ppm[i])/2500000000000.L;
        long double future=(local_time(i,net.now+1000000)-net.committed_epoch[i])*(1000000.L+net.committed_ppm[i])/2500000000000.L;
        assert(future>phase);
    }
}
int main(int argc,char **argv) {
    if(argc==2) {
        if(!strcmp(argv[1],"freshness"))clock_failure(false);
        else if(!strcmp(argv[1],"unstable"))clock_failure(true);
        else if(!strcmp(argv[1],"long"))long_clocks();
        else assert(false);
        return 0;
    }
    setup(false);for(int i=0;i<3;i++)assert(net.nodes[i].committed.member_count==1);
    echoy_wire_state_t s=group();assert(!begin_group(&s));
    setup(true);assert(echoy_protocol_reference(&net.nodes[2])==11);
    assert(begin_group(&s));deliver();
    int64_t boundary=boundary_time();assert(boundary>=net.now+4900000);
    advance(boundary+10000);
    for(int i=0;i<3;i++){assert(net.commits[i]==1);assert(net.nodes[i].committed.member_count==3);}
    /* No physical edge APIs are used after authorization: GAME_EVENT sustains the group. */
    advance(boundary+2000000);for(int i=0;i<3;i++)assert(!net.nodes[i].degraded);
    setup(true);net.duplicate=true;assert(begin_group(&s));deliver();
    boundary=boundary_time();advance(boundary+10000);for(int i=0;i<3;i++)assert(net.commits[i]==1);
    setup(true);net.drop_kind=ECHOY_COMMIT;net.drop_target=2;
    assert(begin_group(&s));deliver();boundary=boundary_time();
    advance(boundary+1000000);assert(net.commits[2]==0);assert(net.nodes[2].committed.member_count==1);
    assert(net.nodes[2].degraded);
    setup(true);assert(begin_group(&s));deliver();boundary=boundary_time();
    net.partition=1;advance(boundary+10000);assert(net.commits[1]==0&&net.commits[2]==0);
    setup(true);net.reorder=true;assert(begin_group(&s));deliver();
    boundary=boundary_time();advance(boundary+10000);assert(net.commits[1]==0&&net.commits[2]==0);
    setup(true);assert(begin_group(&s));deliver();
    echoy_protocol_init(&net.nodes[0],11,100,8,send_packet,NULL,commit,NULL);advance(net.now+1000000);
    assert(net.commits[1]==0&&net.commits[2]==0);assert(net.nodes[1].degraded);
    setup(true);net.drop_kind=ECHOY_HEARTBEAT;net.drop_target=1;
    advance(net.now+1000000);assert(net.nodes[1].peers[0].lost);
    puts("three-node delay/loss/duplicate/reorder/partition/reboot checks passed");return 0;
}
