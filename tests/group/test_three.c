/* Independent real band/arrangement/preparation/PCM owners. Only physical
 * contacts, network delivery and monotonic clock adapters are simulated. */
#include "echoy_group.h"
#include "echoy_link_codec.h"
#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    echoy_group_t *group;
    echoy_band_t *band;
    echoy_link_update_t update;
    echoy_control_view_t state;
    echoy_link_view_t view,frozen_view;
    bool freeze_view;
    echoy_audio_loops_t *expected_pcm;
    uint32_t expected_crc;
    bool expected_original;
    uint64_t checked_samples;
    int64_t offset,silent_after;
    uint64_t id;
    bool queued, prepare,fail_next;
    unsigned rejects;
    double energy;
} node_t;
static node_t nodes[3];
static int64_t physical;
static uint32_t drop_kinds;
static uint64_t isolated;
static bool start_empty;
static bool wide_boot_offsets;
static struct {
    bool used; uint64_t from,to; uint16_t service; size_t size;
    uint8_t bytes[64]; int64_t at;
} packets[64];
static bool read_owner(void *ctx,echoy_link_view_t *view)
{
    node_t *n=ctx;
    if (n->freeze_view) { *view=n->frozen_view; return true; }
    return echoy_band_link_read(n->band,view);
}
static bool receive_owner(void *ctx,const echoy_link_update_t *u)
{ node_t *n=ctx; n->update=*u; n->queued=true; return true; }
static bool send_packet(void *ctx,uint64_t to,uint16_t service,const uint8_t *data,size_t size)
{
    node_t *n=ctx; assert(size<=64);
    for (unsigned i=0;i<64;++i) if (!packets[i].used) {
        packets[i].used=true; packets[i].from=n->id; packets[i].to=to;
        packets[i].service=service; packets[i].size=size;
        memcpy(packets[i].bytes,data,size); packets[i].at=physical+1000; return true;
    }
    return false;
}
static void owner(node_t *n)
{
    int64_t now=physical+n->offset;
    if (n->queued) {
        n->queued=false;
        if (!echoy_band_link_update(n->band,&n->update,now)) ++n->rejects;
    }
    echoy_band_tick(n->band,now);
    if (n->prepare) {
        echoy_band_job_t *j=echoy_band_take_job(n->band);
        if (j) {
            if (n->fail_next) { n->fail_next=false; assert(!echoy_band_finish_job(n->band,j,now)); }
            else { assert(echoy_band_prepare_job(j)); assert(echoy_band_finish_job(n->band,j,now)); }
        }
    }
    assert(echoy_band_read(n->band,ECHOY_TILE_A,&n->state));
    assert(echoy_band_link_read(n->band,&n->view));
}
static void advance(int64_t until)
{
    for (;physical<until;physical+=10000) {
        for (unsigned i=0;i<64;++i) if (packets[i].used && packets[i].at<=physical) {
            unsigned a=0,b=0;
            while (a<3 && nodes[a].id!=packets[i].from) ++a;
            while (b<3 && nodes[b].id!=packets[i].to) ++b;
            assert(a<3 && b<3);
            if (!(drop_kinds & (1u<<packets[i].bytes[1])) &&
                (!isolated || (nodes[a].id!=isolated && nodes[b].id!=isolated)))
                echoy_group_receive(nodes[b].group,nodes[a].id,packets[i].bytes,packets[i].size,
                                    physical+nodes[b].offset);
            echoy_group_delivery(nodes[a].group,nodes[b].id,packets[i].service,true,
                                 physical+nodes[a].offset);
            packets[i].used=false;
        }
        for (unsigned i=0;i<3;++i) {
            node_t *n=&nodes[i]; owner(n);
            echoy_group_tick(n->group,physical+n->offset);
            bool playing=n->state.state.phase==ECHOY_PLAYING && n->state.state.continuous;
            if (playing) {
                uint8_t bytes[8192]; size_t size=echoy_link_plan_encode(&n->state.plan,bytes,sizeof(bytes));
                assert(size); uint32_t crc=echoy_link_crc32(bytes,size);
                const echoy_arrange_tile_t *tile=echoy_arrange_get_tile(echoy_band_arrangement(n->band),ECHOY_TILE_A);
                bool original=tile->current.source==ECHOY_TILE_NONE && !tile->current.detached &&
                    n->state.plan.role==ECHOY_PULSE && n->state.plan.phrase==1;
                if (!n->expected_pcm || crc!=n->expected_crc || original!=n->expected_original) {
                    echoy_audio_free_loops(n->expected_pcm); n->expected_pcm=NULL;
                    assert(echoy_audio_prepare(&n->state.plan,original,&n->expected_pcm)==ECHOY_AUDIO_OK);
                    n->expected_crc=crc; n->expected_original=original;
                }
            }
            int64_t local=physical+n->offset;
            bool changing=n->view.armed_at_us && local+10000>=n->view.armed_at_us &&
                local<n->view.armed_at_us+8000;
            float pcm[240]; echoy_band_render(n->band,local,pcm,240);
            for (unsigned k=0;k<240;++k) {
                assert(isfinite(pcm[k])); n->energy+=pcm[k]*pcm[k];
                if (playing && !changing) {
                    int64_t at=local+(int64_t)k*1000000/ECHOY_AUDIO_SAMPLE_RATE;
                    double want=0;
                    if (at>=n->state.state.start_us) {
                        int64_t phase=(at-n->view.clock_origin_us)%2500000;
                        if (phase<0) phase+=2500000;
                        uint64_t pos=((uint64_t)phase*ECHOY_AUDIO_SAMPLE_RATE+500000)/1000000;
                        want=n->expected_pcm->samples[ECHOY_AUDIO_FULL][pos%n->expected_pcm->frames]*.65*.45;
                    }
                    if (fabs(pcm[k]-want)>=1e-6) {
                        fprintf(stderr,"PCM node=%u at=%lld got=%.9f want=%.9f clock=%lld start=%lld\n",i,(long long)at,pcm[k],want,(long long)n->view.clock_origin_us,(long long)n->state.state.start_us);
                        assert(0);
                    }
                    ++n->checked_samples;
                }
                if (!n->state.state.continuous && n->state.state.phase==ECHOY_IDLE &&
                    physical+n->offset+(int64_t)k*1000000/ECHOY_AUDIO_SAMPLE_RATE>=n->silent_after)
                    assert(pcm[k]==0);
            }
        }
    }
}
static void join(unsigned a,unsigned b,uint32_t session)
{
    echoy_group_edge(nodes[a].group,false,nodes[b].id,session,physical+nodes[a].offset);
    echoy_group_edge(nodes[b].group,true,nodes[a].id,session,physical+nodes[b].offset);
}
static void initialize(unsigned order)
{
    static const unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    memset(nodes,0,sizeof(nodes)); memset(packets,0,sizeof(packets));
    physical=1000000; drop_kinds=0; isolated=0;
    for (unsigned i=0;i<3;++i) {
        node_t *n=&nodes[i]; n->id=11+orders[order][i]*11;
        n->offset=wide_boot_offsets ? (int64_t)orders[order][i]*70000000 : (int64_t)i*200000;
        n->prepare=true; n->band=echoy_band_create(); assert(n->band);
        if (!start_empty) assert(echoy_band_action(n->band,ECHOY_TILE_A,ECHOY_ACT_EXAMPLE,0,physical+n->offset));
        if (n->id!=11) assert(echoy_band_action(n->band,ECHOY_TILE_A,ECHOY_ACT_ROLE,n->id==22 ? ECHOY_VOICE : ECHOY_HARMONY,physical+n->offset));
        n->group=calloc(1,echoy_group_size()); assert(n->group);
        echoy_group_callbacks_t cb={read_owner,receive_owner,send_packet,n};
        assert(echoy_group_init(n->group,echoy_group_size(),n->id,100+i,&cb));
    }
    advance(5000000);
    for (unsigned i=0;i<3;++i) {
        assert(nodes[i].state.state.phase==ECHOY_IDLE);
    }
}

static void destroy_nodes(void)
{
    for (unsigned i=0;i<3;++i) {
        echoy_audio_free_loops(nodes[i].expected_pcm); echoy_band_delete(nodes[i].band); free(nodes[i].group);
    }
    memset(nodes,0,sizeof(nodes));
}
static void equal_plan(const echoy_plan_t *a,const echoy_plan_t *b)
{
    uint8_t x[8192],y[8192];
    size_t n=echoy_link_plan_encode(a,x,sizeof(x));
    assert(n && n==echoy_link_plan_encode(b,y,sizeof(y)) && !memcmp(x,y,n));
}
static void check_chain(void)
{
    echoy_plan_t expected,local;
    for (unsigned i=1;i<3;++i) {
        assert(echoy_compose(nodes[i-1].state.plan.source,nodes[i-1].state.plan.source_count,
            nodes[i].state.plan.role,nodes[i].state.plan.phrase,nodes[i].state.plan.density,&local));
        assert(echoy_arrange_linked(&local,&nodes[i-1].state.plan,&expected));
        equal_plan(&expected,&nodes[i].state.plan);
    }
}
static void pause_all(void)
{
    for (unsigned i=0;i<3;++i) {
        assert(echoy_band_action(nodes[i].band,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,physical+nodes[i].offset));
        nodes[i].silent_after=physical+nodes[i].offset+8000;
    }
    advance(physical+500000);
}
static void first_start(unsigned first,bool simultaneous)
{
    pause_all();
    int64_t pressed=physical;
    assert(echoy_band_action(nodes[first].band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,physical+nodes[first].offset));
    owner(&nodes[first]);
    if (nodes[first].state.state.start_us!=pressed+nodes[first].offset+50000) fprintf(stderr,"PROMPT first=%u pressed=%lld start=%lld origin=%lld offset=%lld\n",first,(long long)pressed,(long long)nodes[first].state.state.start_us,(long long)nodes[first].view.clock_origin_us,(long long)nodes[first].offset);
    assert(nodes[first].state.state.start_us==pressed+nodes[first].offset+50000);
    if (simultaneous) for (unsigned i=0;i<3;++i) if (i!=first)
        assert(echoy_band_action(nodes[i].band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,physical+nodes[i].offset));
    advance(physical+500000);
    if (!simultaneous) for (unsigned i=0;i<3;++i) if (i!=first) {
        assert(!nodes[i].state.state.continuous);
        assert(echoy_band_action(nodes[i].band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,physical+nodes[i].offset));
        advance(physical+500000);
    }
    advance(physical+3000000);
    for (unsigned i=0;i<3;++i) {
        assert(nodes[i].state.state.continuous && nodes[i].energy>1e-6);
        if (llabs(nodes[i].view.clock_origin_us-nodes[i].offset-pressed-50000)>=20000)
            fprintf(stderr,"START first=%u simultaneous=%d rank=%u pressed=%lld origin=%lld\n",first,simultaneous,i,(long long)pressed,(long long)(nodes[i].view.clock_origin_us-nodes[i].offset));
        assert(llabs(nodes[i].view.clock_origin_us-nodes[i].offset-pressed-50000)<20000);
    }
}
static void fixed_overlap(void)
{
    pause_all();
    uint32_t old=nodes[0].view.armed_transaction;
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,2,physical+nodes[1].offset));
    int64_t limit=physical+5000000;
    while (physical<limit && (nodes[2].view.armed_transaction==old || !nodes[2].view.armed_transaction))
        advance(physical+10000);
    int64_t at=nodes[0].view.armed_at_us;
    assert(at>physical+100000);
    assert(echoy_band_action(nodes[2].band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,physical+nodes[2].offset));
    owner(&nodes[2]);
    assert(nodes[2].state.state.start_us==physical+nodes[2].offset+50000);
    advance(at+1000000);
    for (unsigned i=0;i<3;++i) {
        assert(llabs(nodes[i].view.clock_origin_us-nodes[i].offset-at)<20000);
        assert(nodes[i].view.committed_transaction==nodes[0].view.committed_transaction);
        assert(nodes[i].state.state.continuous==(i==2));
    }
    check_chain();
}
static void middle_readiness_edit(void)
{
    pause_all(); nodes[2].prepare=false;
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,physical+nodes[1].offset));
    advance(physical+1000000);
    assert(nodes[1].view.offer_ready);
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,1,physical+nodes[1].offset));
    nodes[2].prepare=true;
    advance(physical+8000000);
    assert(nodes[1].state.plan.phrase==1); check_chain();
}

static void loss_and_recovery(void)
{
    uint32_t old=nodes[0].view.committed_transaction;
    drop_kinds=1u<<6; /* DATA missing while its radio delivery is reported. */
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,physical+nodes[1].offset));
    advance(physical+7000000);
    assert(nodes[0].view.committed_transaction==old);
    assert(nodes[1].state.plan.phrase==2);
    drop_kinds=0;
    advance(physical+8000000);
    assert(nodes[1].state.plan.phrase==3); check_chain();
    /* Complete radio isolation must preserve actual contact and old music. */
    isolated=nodes[2].id; old=nodes[0].view.committed_transaction;
    echoy_plan_t retained=nodes[2].state.plan;
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,1,physical+nodes[1].offset));
    advance(physical+35000000);
    assert(nodes[0].view.committed_transaction==old);
    assert(nodes[2].state.state.following);
    equal_plan(&retained,&nodes[2].state.plan);
    isolated=0; advance(physical+10000000);
    assert(nodes[1].state.plan.phrase==1); check_chain();
}

static void renewed_sessions_keep_membership(void)
{
    pause_all();
    join(0,1,601); join(1,2,602);
    advance(physical+20000);
    assert(nodes[1].state.state.following && nodes[2].state.state.following);
    int64_t pressed=physical;
    assert(echoy_band_action(nodes[2].band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,physical+nodes[2].offset));
    owner(&nodes[2]);
    assert(nodes[2].state.state.start_us==pressed+nodes[2].offset+50000);
    advance(physical+10000000);
    assert(nodes[2].state.state.continuous && !nodes[0].state.state.continuous && !nodes[1].state.state.continuous);
    check_chain();
}

static void unmapped_simultaneous_start(void)
{
    pause_all(); drop_kinds=1u<<3; /* PONG withheld on both physical seams. */
    join(0,1,701); join(1,2,702); advance(physical+100000);
    int64_t pressed=physical;
    for (unsigned i=1;i<3;++i) {
        assert(echoy_band_action(nodes[i].band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,physical+nodes[i].offset));
        owner(&nodes[i]); assert(nodes[i].state.state.start_us==pressed+nodes[i].offset+50000);
    }
    advance(physical+300000); drop_kinds=0; advance(physical+800000);
    assert(llabs(nodes[0].view.clock_origin_us-pressed-50000)<20000);
    advance(physical+8000000);
    assert(!nodes[0].state.state.continuous && nodes[1].state.state.continuous && nodes[2].state.state.continuous);
    check_chain();
}

static void steady_radio_loss(void)
{
    first_start(1,false);
    echoy_plan_t retained[3];
    for (unsigned i=0;i<3;++i) retained[i]=nodes[i].state.plan;
    isolated=nodes[2].id; advance(physical+35000000);
    for (unsigned i=0;i<3;++i) {
        assert(nodes[i].state.cooperation==ECHOY_COOP_RECOVERING);
        assert(nodes[i].state.state.continuous && nodes[i].state.state.following==(i!=0));
        equal_plan(&retained[i],&nodes[i].state.plan);
    }
    isolated=0; advance(physical+12000000);
    for (unsigned i=0;i<3;++i) {
        assert(nodes[i].state.cooperation!=ECHOY_COOP_RECOVERING);
        assert(nodes[i].state.state.continuous); equal_plan(&retained[i],&nodes[i].state.plan);
    }
}

static void radio_event_retains_group(void)
{
    echoy_group_radio_lost(nodes[1].group,true,physical+nodes[1].offset);
    echoy_group_info_t info;
    assert(echoy_group_info(nodes[1].group,&info) && info.phase==6 && info.count==3 && info.rank==1);
    echoy_group_tick(nodes[1].group,physical+nodes[1].offset); owner(&nodes[1]);
    assert(nodes[1].state.cooperation==ECHOY_COOP_RECOVERING && nodes[1].state.state.following);
    advance(physical+10000000);
    assert(nodes[1].state.cooperation!=ECHOY_COOP_RECOVERING);
    check_chain();
}

static void failed_prepare_and_new_edit(void)
{
    first_start(1,false);
    echoy_plan_t retained[3]; for (unsigned i=0;i<3;++i) retained[i]=nodes[i].state.plan;
    nodes[2].fail_next=true;
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,physical+nodes[1].offset));
    advance(physical+8000000);
    echoy_group_info_t info; assert(echoy_group_info(nodes[0].group,&info) && info.phase==6);
    uint32_t failed_attempt=info.attempt;
    for (unsigned i=0;i<3;++i) equal_plan(&retained[i],&nodes[i].state.plan);
    advance(physical+4000000);
    assert(echoy_group_info(nodes[0].group,&info) && info.attempt==failed_attempt);
    int64_t origin=nodes[1].view.clock_origin_us-nodes[1].offset;
    int64_t bar=origin+((physical-origin+2500000-1)/2500000)*2500000;
    if (bar-150000<physical) bar+=2500000;
    advance(bar-150000); drop_kinds=1u<<7; /* Delay the new STATE past this bar. */
    unsigned desired=retained[1].phrase==1 ? 2u : 1u;
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,desired,physical+nodes[1].offset));
    advance(physical+250000);
    for (unsigned i=0;i<3;++i) equal_plan(&retained[i],&nodes[i].state.plan);
    drop_kinds=0; advance(physical+8000000);
    assert(nodes[1].state.plan.phrase==desired); check_chain();
}

static void upstream_supersedes_prepared_middle(void)
{
    nodes[2].prepare=false;
    assert(echoy_band_action(nodes[0].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,2,physical));
    advance(physical+1000000);
    assert(nodes[1].view.offer_ready);
    assert(echoy_band_action(nodes[0].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,physical));
    advance(physical+1000000); nodes[2].prepare=true;
    advance(physical+10000000);
    assert(nodes[0].state.plan.phrase==3); check_chain();
}
static void run(unsigned order,bool reverse_join)
{
    printf("RUN order=%u reverse=%u\n",order,reverse_join); fflush(stdout);
    initialize(order);
    if (reverse_join) { join(1,2,502); advance(physical+1000000); join(0,1,501); }
    else { join(0,1,501); advance(physical+1000000); join(1,2,502); }
    advance(physical+10000000);
    assert(nodes[1].state.state.following && nodes[2].state.state.following);
    for (unsigned i=0;i<3;++i) {
        assert(nodes[i].view.committed_transaction);
        assert(!nodes[i].state.state.continuous);
    }
    check_chain();
    for (unsigned first=0;first<3;++first) first_start(first,false);
    first_start(0,true);
    middle_readiness_edit(); fixed_overlap();
    if (order==0 && !reverse_join) { loss_and_recovery(); renewed_sessions_keep_membership(); unmapped_simultaneous_start(); steady_radio_loss(); radio_event_retains_group(); failed_prepare_and_new_edit(); upstream_supersedes_prepared_middle(); }
    first_start(1,false);
    echoy_plan_t retained[3];
    for (unsigned i=0;i<3;++i) retained[i]=nodes[i].state.plan;
    echoy_group_edge(nodes[1].group,true,0,0,physical+nodes[1].offset);
    echoy_group_edge(nodes[0].group,false,0,0,physical);
    advance(physical+20000);
    assert(!nodes[1].state.state.following);
    assert(nodes[1].state.state.continuous && nodes[2].state.state.continuous);
    equal_plan(&retained[1],&nodes[1].state.plan);
    echoy_group_edge(nodes[1].group,false,0,0,physical+nodes[1].offset);
    echoy_group_edge(nodes[2].group,true,0,0,physical+nodes[2].offset);
    advance(physical+20000);
    uint64_t checked=0;
    for (unsigned i=0;i<3;++i) checked+=nodes[i].checked_samples;
    for (unsigned i=0;i<3;++i) {
        assert(!nodes[i].state.state.following && nodes[i].state.state.continuous);
        equal_plan(&retained[i],&nodes[i].state.plan);
        echoy_audio_free_loops(nodes[i].expected_pcm); echoy_band_delete(nodes[i].band); free(nodes[i].group);
    }
    printf("PASS order=%u join=%s; A/B/C-first, simultaneous, readiness edit, fixed overlap, release; real PCM samples=%llu\n",order,reverse_join ? "BC-first" : "AB-first",(unsigned long long)checked);
}

static void deferred_upstream(void)
{
    initialize(0); join(1,2,502); advance(physical+10000000);
    assert(nodes[2].state.state.following && !nodes[1].state.state.following);
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,physical+nodes[1].offset));
    advance(physical+20000); join(0,1,501); advance(physical+500000);
    assert(nodes[1].state.state.phase==ECHOY_COUNT_IN && !nodes[1].state.state.following);
    assert(nodes[2].state.state.following);
    /* The waiting A seam cannot block an already admitted B—C edit. */
    assert(echoy_band_action(nodes[2].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,physical+nodes[2].offset));
    advance(physical+4000000);
    assert(nodes[1].state.state.phase==ECHOY_RECORDING && !nodes[1].state.state.following);
    assert(nodes[2].state.plan.phrase==3);
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_CANCEL_RECORD,0,physical+nodes[1].offset));
    advance(physical+8000000);
    assert(nodes[1].state.state.following && nodes[2].state.state.following);
    check_chain();
    for (unsigned i=0;i<3;++i) { echoy_audio_free_loops(nodes[i].expected_pcm); echoy_band_delete(nodes[i].band); free(nodes[i].group); }
    puts("PASS deferred upstream while B records: B—C edit proceeds, then three-owner admission");
}

static void capture_result_after_arm(void)
{
    initialize(0); join(1,2,502); advance(physical+10000000);
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,physical+nodes[1].offset));
    owner(&nodes[1]);
    int64_t capture=nodes[1].state.state.capture_at_us-nodes[1].offset;
    advance(physical+20000); join(0,1,501);
    advance(capture+300000);
    assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_TAP,0,physical+nodes[1].offset));
    advance(capture+1300000);
    uint32_t old=nodes[1].view.armed_transaction;
    assert(echoy_band_action(nodes[2].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,physical+nodes[2].offset));
    int64_t limit=capture+2450000;
    while (physical<limit && (!nodes[1].view.armed_transaction || nodes[1].view.armed_transaction==old)) advance(physical+10000);
    if (!nodes[1].view.armed_transaction || nodes[1].view.armed_transaction==old) {
        echoy_group_info_t info; echoy_group_info(nodes[1].group,&info);
        fprintf(stderr,"CAPTURE phys=%lld capture=%lld group_phase=%u count=%u tx=%u owner_offer=%u ready=%d busy=%d deferred=%d requested=%llu proposal=%llu cmd=%d\n",
            (long long)physical,(long long)capture,info.phase,info.count,info.attempt,nodes[1].view.offered_transaction,nodes[1].view.offer_ready,nodes[1].view.busy,nodes[1].view.deferred,
            (unsigned long long)nodes[1].view.requested_revision,(unsigned long long)nodes[1].view.proposal_revision,nodes[1].update.command);
    }
    assert(nodes[1].view.armed_transaction && nodes[1].view.armed_transaction!=old);
    uint64_t frozen=nodes[1].view.requested_revision;
    assert(nodes[1].view.armed_at_us-nodes[1].offset>capture+2500000);
    advance(capture+2600000);
    assert(nodes[1].view.requested_revision==frozen); /* Capture result queues behind ARM. */
    advance(physical+20000000);
    assert(nodes[1].state.state.seed==4 && nodes[1].state.state.phase==ECHOY_IDLE);
    assert(nodes[1].state.state.following && nodes[2].state.state.following);
    assert(!nodes[1].state.state.continuous && nodes[2].state.plan.phrase==3);
    check_chain();
    for (unsigned i=0;i<3;++i) {
        echoy_audio_free_loops(nodes[i].expected_pcm); echoy_band_delete(nodes[i].band); free(nodes[i].group);
    }
    puts("PASS recording result after ARM keeps frozen revision; retained seed/reply completes before upstream admission");
}

static void incompatible_peer_stays_local(void)
{
    initialize(0); drop_kinds=1u<<1; join(0,1,501);
    const uint8_t old_frame[36]={2,10};
    echoy_group_receive(nodes[0].group,nodes[1].id,old_frame,sizeof(old_frame),physical);
    advance(physical+4000000);
    for (unsigned i=0;i<2;++i) {
        assert(!nodes[i].state.state.following && !nodes[i].state.state.continuous);
        assert(nodes[i].state.cooperation==ECHOY_COOP_RECOVERING);
    }
    drop_kinds=0; advance(physical+8000000);
    assert(nodes[1].state.state.following && !nodes[1].state.state.continuous);
    for (unsigned i=0;i<3;++i) {
        echoy_audio_free_loops(nodes[i].expected_pcm); echoy_band_delete(nodes[i].band); free(nodes[i].group);
    }
    puts("PASS incompatible/missing app HELLO cannot establish following; existing recovery feedback and fresh handshake work");
}

static void empty_chain_gets_first_recording(void)
{
    start_empty=true; initialize(0); start_empty=false;
    join(0,1,501); join(1,2,502); advance(physical+8000000);
    for (unsigned i=0;i<3;++i) {
        assert(!nodes[i].state.plan.count && !nodes[i].state.state.continuous);
        assert(nodes[i].state.state.following==(i!=0));
    }
    assert(echoy_band_action(nodes[0].band,ECHOY_TILE_A,ECHOY_ACT_RECORD,0,physical));
    owner(&nodes[0]); int64_t capture=nodes[0].state.state.capture_at_us;
    advance(capture+310000);
    assert(echoy_band_action(nodes[0].band,ECHOY_TILE_A,ECHOY_ACT_TAP,0,physical));
    advance(capture+15000000);
    assert(nodes[0].state.state.seed==4);
    for (unsigned i=0;i<3;++i) {
        assert(nodes[i].state.plan.count && !nodes[i].state.state.continuous && nodes[i].state.state.phase==ECHOY_IDLE);
        assert(nodes[i].state.state.following==(i!=0));
    }
    check_chain();
    for (unsigned i=0;i<3;++i) {
        echoy_audio_free_loops(nodes[i].expected_pcm); echoy_band_delete(nodes[i].band); free(nodes[i].group);
    }
    puts("PASS initially empty three-node chain receives first recorded seed and full linked banks; one reply and no autoplay");
}

static void owner_availability_recovery(void)
{
    for (unsigned target=0;target<2;++target) {
        initialize(0); join(0,1,501); join(1,2,502); advance(15000000); first_start(0,false);
        assert(echoy_band_action(nodes[target].band,ECHOY_TILE_A,ECHOY_ACT_CONDITION,ECHOY_LOADING,physical+nodes[target].offset));
        nodes[target].silent_after=physical+nodes[target].offset+8000;
        unsigned edited=target==0 ? 1 : 0;
        assert(echoy_band_action(nodes[edited].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,physical+nodes[edited].offset));
        advance(physical+35000000);
        echoy_group_info_t info; assert(echoy_group_info(nodes[0].group,&info) && info.phase==6);
        assert(echoy_band_action(nodes[target].band,ECHOY_TILE_A,ECHOY_ACT_CONDITION,ECHOY_NORMAL,physical+nodes[target].offset));
        advance(physical+12000000);
        assert(nodes[edited].state.plan.phrase==3); check_chain();
        for (unsigned i=0;i<3;++i) {
            assert(nodes[i].state.state.continuous==(i!=target));
            assert(nodes[i].state.cooperation!=ECHOY_COOP_RECOVERING);
            echoy_audio_free_loops(nodes[i].expected_pcm); echoy_band_delete(nodes[i].band); free(nodes[i].group);
        }
    }
    puts("PASS root/middle owner availability recovery after failed transaction without replug or implicit resume");
}

static void cached_owner_across_root_replacement(void)
{
    initialize(0); join(1,2,502); advance(physical+5000000);
    join(0,1,501); advance(physical+10000000); first_start(1,false);
    echoy_plan_t retained=nodes[1].state.plan;
    nodes[1].frozen_view=nodes[1].view; nodes[1].freeze_view=true;
    echoy_group_edge(nodes[1].group,true,0,0,physical+nodes[1].offset);
    echoy_group_edge(nodes[0].group,false,0,0,physical);
    advance(physical+500000); nodes[1].freeze_view=false;
    advance(physical+10000000);
    echoy_group_info_t info;
    assert(echoy_group_info(nodes[1].group,&info) && info.count==2 && info.rank==0 && info.phase==5);
    assert(echoy_group_info(nodes[2].group,&info) && info.count==2 && info.rank==1 && info.phase==5);
    equal_plan(&retained,&nodes[1].state.plan);
    assert(!nodes[1].state.state.following && nodes[2].state.state.following);
    for (unsigned i=0;i<3;++i) {
        assert(nodes[i].state.state.continuous);
        echoy_audio_free_loops(nodes[i].expected_pcm); echoy_band_delete(nodes[i].band); free(nodes[i].group);
    }
    puts("PASS cached owner publication across root replacement cannot bind old group readiness/revision");
}
static void boot_clock_offsets(void)
{
    wide_boot_offsets=true;
    for (unsigned order=0;order<6;++order) {
        initialize(order); join(0,1,501); advance(physical+5000000);
        for (unsigned i=0;i<2;++i) {
            echoy_group_info_t info; assert(echoy_group_info(nodes[i].group,&info));
            assert(info.count==2 && info.phase==5 && info.clock_mapped);
            assert(!nodes[i].state.state.continuous);
        }
        advance(physical+35000000);
        for (unsigned i=0;i<2;++i) {
            echoy_group_info_t info; assert(echoy_group_info(nodes[i].group,&info) && info.phase==5);
        }
        int64_t pressed=physical;
        assert(echoy_band_action(nodes[1].band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,physical+nodes[1].offset));
        advance(physical+4000000);
        assert(nodes[1].state.state.continuous && !nodes[0].state.state.continuous);
        assert(nodes[1].state.state.start_us==pressed+nodes[1].offset+50000);
        assert(echoy_band_action(nodes[0].band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,physical+nodes[0].offset));
        advance(physical+4000000);
        assert(nodes[0].state.state.continuous && nodes[1].state.state.continuous);
        assert(llabs((nodes[0].view.clock_origin_us-nodes[0].offset)-
                     (nodes[1].view.clock_origin_us-nodes[1].offset))<20000);
        destroy_nodes();
    }
    for (unsigned order=0;order<6;++order) for (unsigned reverse=0;reverse<2;++reverse) {
        initialize(order);
        if (reverse) { join(1,2,502); advance(physical+1000000); join(0,1,501); }
        else { join(0,1,501); advance(physical+1000000); join(1,2,502); }
        advance(physical+10000000);
        for (unsigned i=0;i<3;++i) {
            echoy_group_info_t info; assert(echoy_group_info(nodes[i].group,&info));
            assert(info.count==3 && info.phase==5 && info.clock_mapped);
            assert(nodes[i].view.committed_transaction && !nodes[i].state.state.continuous);
            assert(nodes[i].state.cooperation!=ECHOY_COOP_RECOVERING);
        }
        check_chain();
        for (unsigned first=0;first<3;++first) first_start(first,false);
        destroy_nodes();
    }
    /* If mapping never completes and even ABORT is lost, the local monotonic
     * budget must still expire, for either sign of the unknown clock offset. */
    for (unsigned order=0;order<6;order+=5) {
        initialize(order); drop_kinds=(1u<<2)|(1u<<3)|(1u<<10); join(0,1,501);
        advance(physical+4000000);
        echoy_group_info_t info; assert(echoy_group_info(nodes[1].group,&info));
        assert(!info.clock_mapped && info.phase==1);
        for (unsigned edit=0;edit<3;++edit) {
            assert(echoy_band_action(nodes[0].band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,2+(edit%2),physical+nodes[0].offset));
            advance(physical+10000000);
        }
        assert(echoy_group_info(nodes[1].group,&info) && !info.clock_mapped && info.phase==6);
        assert(nodes[1].state.cooperation==ECHOY_COOP_RECOVERING && !nodes[1].state.state.continuous);
        destroy_nodes();
    }
    wide_boot_offsets=false;
    puts("PASS 70/140-second boot clock differences: six steady pairs, all six orders/both triple joins, first starts and bounded unmapped timeout despite edits/lost ABORT");
}
int main(int argc,char **argv)
{
    if (argc>1 && !strcmp(argv[1],"boot-offsets")) { boot_clock_offsets(); return 0; }
    if (argc>1 && !strcmp(argv[1],"cached")) { cached_owner_across_root_replacement(); return 0; }
    if (argc>1 && !strcmp(argv[1],"available")) { owner_availability_recovery(); return 0; }
    if (argc>1 && !strcmp(argv[1],"superseded")) { initialize(0); join(0,1,501); join(1,2,502); advance(15000000); upstream_supersedes_prepared_middle(); destroy_nodes(); return 0; }
    if (argc>1 && !strcmp(argv[1],"prepare-fail")) { initialize(0); join(0,1,501); join(1,2,502); advance(15000000); failed_prepare_and_new_edit(); destroy_nodes(); return 0; }
    if (argc>1 && !strcmp(argv[1],"steady")) { initialize(0); join(0,1,501); join(1,2,502); advance(15000000); steady_radio_loss(); destroy_nodes(); return 0; }
    if (argc>1 && !strcmp(argv[1],"unmapped")) { initialize(0); join(0,1,501); join(1,2,502); advance(15000000); unmapped_simultaneous_start(); destroy_nodes(); return 0; }
    if (argc>1 && !strcmp(argv[1],"capture")) { capture_result_after_arm(); return 0; }
    if (argc>1 && !strcmp(argv[1],"incompatible")) { incompatible_peer_stays_local(); return 0; }
    if (argc>1 && !strcmp(argv[1],"empty")) { empty_chain_gets_first_recording(); return 0; }
    boot_clock_offsets(); cached_owner_across_root_replacement(); owner_availability_recovery(); empty_chain_gets_first_recording(); incompatible_peer_stays_local(); deferred_upstream(); capture_result_after_arm();
    for (unsigned i=0;i<6;++i) for (unsigned j=0;j<2;++j) {
        if (argc>1 && (unsigned)atoi(argv[1])!=i) continue;
        run(i,j!=0);
    }
}
