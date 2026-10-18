/* Real admission, routed transport, group owners and PCM. Only MAC latency and
 * immutable worker completion time are simulated. Seed6768/roles match the
 * captured failure; 20ms driver completion is a stress profile, not measured RF. */
#include "echoy_radio.h"
#include "echoy_group.h"
#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    echoy_radio_t *radio;echoy_group_t *group;echoy_band_t *band;
    echoy_band_job_t *job;echoy_link_update_t update;
    uint64_t id;uint32_t boot;int64_t offset,busy_until,job_due;
    bool queued;uint32_t failed_identity;
} node_t;
static node_t nodes[3];static int64_t now;
static struct { bool used;unsigned from;uint64_t to;int64_t at;size_t size;uint8_t data[100]; } packets[128];
static bool read_owner(void *ctx,echoy_link_view_t *v){return echoy_band_link_read(((node_t*)ctx)->band,v);}
static bool receive_owner(void *ctx,const echoy_link_update_t *u){node_t *n=ctx;n->update=*u;n->queued=true;if(u->failed)n->failed_identity=u->session;return true;}
static bool send_group(void *ctx,uint64_t to,uint16_t service,const uint8_t *data,size_t size){node_t *n=ctx;return echoy_radio_send(n->radio,to,service,data,size,now+n->offset);}
static bool available(void *ctx){return now>=((node_t*)ctx)->busy_until;}
static bool send_raw(void *ctx,uint64_t to,const uint8_t *data,size_t size){
    node_t *n=ctx;assert(available(n)&&size<=100);
    for(unsigned i=0;i<128;i++)if(!packets[i].used){
        packets[i].used=true;packets[i].from=(unsigned)(n-nodes);packets[i].to=to;
        packets[i].at=now+10000;packets[i].size=size;memcpy(packets[i].data,data,size);
        n->busy_until=now+20000;return true;
    }return false;
}
static void edge(void *ctx,bool left,uint64_t peer,uint32_t session,int64_t t){echoy_group_edge(((node_t*)ctx)->group,left,peer,session,t);}
static void lost(void *ctx,bool left,int64_t t){echoy_group_radio_lost(((node_t*)ctx)->group,left,t);}
static void receive(void *ctx,uint64_t from,const uint8_t *data,size_t size,int64_t t){echoy_group_receive(((node_t*)ctx)->group,from,data,size,t);}
static void delivery(void *ctx,uint64_t to,uint16_t service,bool ok,int64_t t){echoy_group_delivery(((node_t*)ctx)->group,to,service,ok,t);}
static void stamp(void *ctx,uint8_t *data,size_t size,int64_t t){echoy_group_stamp_send(((node_t*)ctx)->group,data,size,t);}
static void advance(int64_t until){
    for(;now<until;now+=10000){
        for(unsigned i=0;i<128;i++)if(packets[i].used && now>=packets[i].at){
            unsigned from=packets[i].from;
            for(unsigned to=0;to<3;to++)if(to!=from && (!packets[i].to||packets[i].to==nodes[to].id))
                echoy_radio_receive(nodes[to].radio,nodes[from].id,nodes[from].boot,packets[i].data,packets[i].size,now+nodes[to].offset);
            packets[i].used=false;
        }
        for(unsigned i=0;i<3;i++){
            node_t *n=&nodes[i];int64_t local=now+n->offset;
            if(n->queued){n->queued=false;(void)echoy_band_link_update(n->band,&n->update,local);}
            echoy_band_tick(n->band,local);echoy_group_tick(n->group,local);echoy_radio_tick(n->radio,local);
            if(n->job && now>=n->job_due){(void)echoy_band_finish_job(n->band,n->job,local);n->job=NULL;}
            if(!n->job){n->job=echoy_band_take_job(n->band);if(n->job){assert(echoy_band_prepare_job(n->job));n->job_due=now+1800000;}}
            float pcm[240];echoy_band_render(n->band,local,pcm,240);
            for(unsigned k=0;k<240;k++)assert(isfinite(pcm[k]));
        }
    }
}
static void roles(void){assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_HARMONY,now+nodes[1].offset));assert(echoy_band_action(nodes[2].band,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_VOICE,now+nodes[2].offset));}
static void run(bool later,unsigned record_delay){
    memset(nodes,0,sizeof(nodes));memset(packets,0,sizeof(packets));now=1000000;
    const uint64_t ids[]={0x30eda0f4459aULL,0x30eda0f40c6cULL,0x30eda0f452a0ULL};
    const int64_t offsets[]={0,90000000,390000000};
    const uint8_t bca[]={1,2,3},abc[]={2,3,1};
    for(unsigned i=0;i<3;i++){
        node_t *n=&nodes[i];n->id=ids[i];n->boot=100+i;n->offset=offsets[i];n->band=echoy_band_create();n->group=calloc(1,echoy_group_size());n->radio=calloc(1,echoy_radio_size());assert(n->band&&n->group&&n->radio);
        echoy_group_callbacks_t gc={read_owner,receive_owner,send_group,n};echoy_radio_callbacks_t rc={send_raw,edge,lost,receive,delivery,n,stamp,available};
        assert(echoy_group_init(n->group,echoy_group_size(),n->id,n->boot,&gc));assert(echoy_radio_init(n->radio,echoy_radio_size(),n->id,n->boot,&rc));echoy_radio_contacts(n->radio,true,bca[i],now+n->offset);
    }
    advance(now+60000000);
    if(!later){roles();advance(now+15000000);}
    for(unsigned i=0;i<3;i++)echoy_radio_contacts(nodes[i].radio,true,abc[i],now+nodes[i].offset);
    advance(now+15000000+record_delay);assert(echoy_band_action(nodes[0].band,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,now));
    echoy_control_view_t v;assert(echoy_band_read(nodes[0].band,ECHOY_TILE_A,&v));int64_t capture=v.state.capture_at_us;
    for(unsigned bit=0;bit<16;bit++)if(6768u&(1u<<bit)){advance(capture+bit*156250+10000);assert(echoy_band_action(nodes[0].band,ECHOY_TILE_A,ECHOY_ACT_TAP,0,now));}
    advance(capture+22000000);if(later)roles();advance(now+40000000);
    for(unsigned i=0;i<3;i++){
        node_t *n=&nodes[i];echoy_group_info_t g;assert(echoy_group_info(n->group,&g));assert(echoy_band_read(n->band,ECHOY_TILE_A,&v));
        if(n->failed_identity==g.identity)fprintf(stderr,"same graph failure node=%u role_order=%u phase=%u\n",i,later,g.phase);
        assert(g.phase==5 && g.count==3 && g.rank==i);
        const echoy_group_timing_t *t=&g.timing;
        assert(t->membership_us>=n->offset && t->started_us>=t->membership_us);
        assert(t->offered_us>=t->started_us && t->output_ready_us>=t->offered_us);
        assert(t->encoded_us>=t->offered_us && t->encoded_us<=t->output_ready_us);
        assert(t->armed_us>=t->output_ready_us && t->boundary_us>=t->armed_us);
        assert(t->committed_us>=t->boundary_us && t->committed_us<=now+n->offset);
        if(i) assert(t->input_first_us>=t->started_us && t->input_ready_us>=t->input_first_us && t->offered_us>=t->input_ready_us);
        else {assert(t->input_first_us==-1 && t->input_ready_us==-1);assert(t->boundary_us-t->armed_us>=1000000 && t->boundary_us-t->armed_us<3500000);}
        printf("TIMING node=%u start=%lld input=%lld..%lld offer=%lld ready=%lld arm=%lld boundary=%lld committed=%lld\n",i,
            (long long)(t->started_us-n->offset),(long long)(t->input_first_us-n->offset),
            (long long)(t->input_ready_us-n->offset),(long long)(t->offered_us-n->offset),
            (long long)(t->output_ready_us-n->offset),(long long)(t->armed_us-n->offset),
            (long long)(t->boundary_us-n->offset),(long long)(t->committed_us-n->offset));
        assert(n->failed_identity!=g.identity);
        assert(!v.preparation_failed && !v.state.continuous && v.state.phase==ECHOY_IDLE && v.plan.source_count==6);
        assert(v.cooperation==(i?ECHOY_COOP_FOLLOWING:ECHOY_COOP_CONNECTED));
        if(!i)assert(v.state.seed==6768 && v.plan.count==13);
        if(n->job)echoy_band_discard_job(n->job);
        echoy_band_delete(n->band);free(n->group);free(n->radio);
    }
    printf("PASS captured six-note source, %s role changes, record phase%u, 20ms driver/1.8s worker\n",later?"later":"earlier",record_delay);
}
int main(void){for(unsigned delay=0;delay<2500000;delay+=500000){run(false,delay);run(true,delay);}}
