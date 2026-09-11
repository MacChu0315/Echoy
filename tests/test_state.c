// SPDX-License-Identifier: Apache-2.0
#include "echoy_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static echoy_core_t c;
static echoy_result_t send(int d, echoy_action_t type, double value) {
    echoy_input_t e={.type=type,.device=d,.at_us=c.time_us,.value=value,
        .boot_id=c.devices[d].boot_id,.generation=c.devices[d].role_generation};
    if(type==ECHOY_SYNC_READY||type==ECHOY_SYNC_FAIL)e.generation=c.sync.generation;
    if(type==ECHOY_PLAY_READY||type==ECHOY_PLAY_FAILED)e.generation=c.devices[d].play_generation;
    return echoy_dispatch(&c,&e);
}
static void boot(int d) { assert(send(d,ECHOY_BOOT,0)==ECHOY_OK);assert(send(d,ECHOY_BOOT_READY,0)==ECHOY_OK); }
static void reset(void) { const uint64_t ids[]={10,20,30};echoy_core_init(&c,ids);boot(0); }
static const echoy_plan_entry_t *plan(int d) {for(size_t i=0;i<c.plan.count;i++)if(c.plan.devices[i].device==d)return &c.plan.devices[i];return NULL;}
static echoy_session_t *session(int d) {for(int i=0;i<3;i++)if(c.sessions[i].id==c.devices[d].session_id)return &c.sessions[i];return NULL;}
static void contacts(int a,int b) {echoy_input_t e={.type=ECHOY_CONTACTS,.at_us=c.time_us,.edge_count=a<0?0:1,.edges={{a,b,false}}};assert(echoy_dispatch(&c,&e)==ECHOY_OK);}
static void nextbar(int d) {echoy_tick(&c,echoy_next_boundary(session(d),c.time_us+1,true));}
static void join(void) {boot(1);contacts(0,1);echoy_tick(&c,c.time_us+ECHOY_CONTACT_US);assert(c.sync.stage==ECHOY_SYNC_PREPARING);assert(send(0,ECHOY_SYNC_READY,0)==ECHOY_OK);echoy_tick(&c,c.sync.apply_at_us);assert(c.devices[0].session_id==c.devices[1].session_id);}
static void test_transport(void) {
    reset();echoy_tick(&c,75000);assert(send(0,ECHOY_PLAY_TOGGLE,0)==ECHOY_OK);
    assert(c.devices[0].transport==ECHOY_WAITING);assert(c.devices[0].play_at_us==700000);
    echoy_tick(&c,699999);assert(c.devices[0].transport==ECHOY_WAITING);
    echoy_tick(&c,700000);assert(c.devices[0].transport==ECHOY_PLAYING);
    echoy_motif_t motif=plan(0)->motif;assert(send(0,ECHOY_PAUSE,0)==ECHOY_OK);
    assert(plan(0)->events.count==0);assert(!memcmp(&motif,&plan(0)->motif,sizeof motif));
    send(0,ECHOY_PLAY_TOGGLE,0);send(0,ECHOY_PLAY_TOGGLE,0);echoy_tick(&c,1325000);assert(c.devices[0].transport==ECHOY_PAUSED);
}
static void test_role(void) {
    reset();send(0,ECHOY_ROLE_REQUEST,ECHOY_HARMONY);uint64_t g=c.devices[0].role_generation;
    assert(c.devices[0].role==ECHOY_PULSE);send(0,ECHOY_ROLE_CANCEL,0);
    echoy_input_t late={.type=ECHOY_ROLE_READY,.device=0,.boot_id=c.devices[0].boot_id,.generation=g};
    assert(echoy_dispatch(&c,&late)==ECHOY_STALE);assert(c.devices[0].role==ECHOY_PULSE);
    send(0,ECHOY_ROLE_REQUEST,ECHOY_HARMONY);send(0,ECHOY_PHRASE,3);send(0,ECHOY_ROLE_READY,0);
    assert(c.devices[0].role==ECHOY_HARMONY&&c.devices[0].phrase==3);
    send(0,ECHOY_ROLE_REQUEST,ECHOY_VOICE);send(0,ECHOY_ROLE_FAILED,0);
    assert(c.devices[0].role==ECHOY_HARMONY&&c.devices[0].phrase==3&&c.devices[0].role_error);
    late.boot_id=c.devices[0].boot_id;send(0,ECHOY_POWER_OFF,0);boot(0);
    assert(echoy_dispatch(&c,&late)==ECHOY_STALE);assert(c.devices[0].transport==ECHOY_PAUSED);
    assert(c.devices[0].preparing_role<0&&c.devices[0].pending_phrase==0);
}
static void test_boundary_parameters(void) {
    reset();join();send(1,ECHOY_PLAY_TOGGLE,0);echoy_tick(&c,c.devices[1].play_at_us);
    send(0,ECHOY_ROLE_REQUEST,ECHOY_HARMONY);send(0,ECHOY_ROLE_READY,0);
    assert(c.devices[0].role==ECHOY_PULSE);send(0,ECHOY_VOLUME,.2);assert(plan(0)->volume==.2);
    send(0,ECHOY_ACTIVITY,.9);send(0,ECHOY_FINISH_ACTIVITY,0);
    assert(c.devices[0].activity==.9&&c.devices[0].committed_activity==.58);
    send(0,ECHOY_VOLUME_OPEN,1);send(0,ECHOY_VOLUME_OPEN,0);assert(c.devices[0].pending_role==ECHOY_HARMONY);
    nextbar(0);assert(c.devices[0].role==ECHOY_HARMONY);
    send(0,ECHOY_PHRASE,3);send(1,ECHOY_PAUSE,0);assert(c.devices[0].phrase==3);
}
static void test_faults(void) {
    reset();join();send(0,ECHOY_PLAY_TOGGLE,0);send(1,ECHOY_PLAY_TOGGLE,0);echoy_tick(&c,c.devices[0].play_at_us);
    echoy_motif_t bm=plan(1)->motif;echoy_bar_plan_t copy;echoy_bar_plan(&c,&copy);
    send(0,ECHOY_OUTPUT_FAULT,1);assert(plan(0)->events.count==0&&plan(1)->source==0);
    assert(!memcmp(&bm,&plan(1)->motif,sizeof bm));assert(copy.devices[0].events.count>0);
    send(0,ECHOY_OUTPUT_FAULT,0);assert(plan(0)->events.count>0);
    send(0,ECHOY_PAUSE,0);assert(plan(1)->source==0);send(1,ECHOY_PAUSE,0);assert(plan(0)->motif.count>0);
    send(0,ECHOY_DATA_FAULT,1);assert(plan(1)->source==0);nextbar(0);assert(plan(1)->source==-1&&plan(0)->motif.count==0);
    send(0,ECHOY_DATA_FAULT,0);assert(plan(1)->source==-1);assert(!strcmp(echoy_feedback_key(&c,0),"motif-recovery"));
    nextbar(0);assert(plan(1)->source==0);send(0,ECHOY_DATA_LOADING,1);assert(plan(1)->source==0);nextbar(0);assert(plan(1)->source==-1);
}
static void test_sessions(void) {
    reset();boot(1);assert(c.devices[0].session_id!=c.devices[1].session_id);assert(c.devices[0].discovered==2);
    contacts(0,1);echoy_tick(&c,ECHOY_CONTACT_US-1);assert(c.sync.stage==ECHOY_SYNC_NONE);
    echoy_tick(&c,ECHOY_CONTACT_US);assert(c.sync.stage==ECHOY_SYNC_PREPARING);
    echoy_tick(&c,2000000);assert(c.devices[0].session_id!=c.devices[1].session_id);send(0,ECHOY_SYNC_READY,0);echoy_tick(&c,c.sync.apply_at_us);
    uint64_t sid=c.devices[0].session_id;assert(c.devices[1].session_id==sid);assert(c.devices[1].transport==ECHOY_PAUSED);
    contacts(-1,0);nextbar(0);assert(plan(1)->source==-1&&c.devices[1].session_id==sid);
    boot(2);contacts(1,2);echoy_tick(&c,c.time_us+ECHOY_CONTACT_US);send(1,ECHOY_SYNC_READY,0);echoy_tick(&c,c.sync.apply_at_us);
    assert(c.devices[2].session_id==sid&&c.devices[2].transport==ECHOY_PAUSED);
    int64_t epoch=session(1)->epoch_us;send(0,ECHOY_POWER_OFF,0);assert(session(1)->leader==1&&session(1)->epoch_us==epoch);
    send(2,ECHOY_LINK,0);int64_t lost=c.time_us;echoy_tick(&c,lost+ECHOY_HOLD_US-1);assert(c.devices[2].session_id==sid);
    long double before=echoy_phase_steps(session(2),lost+ECHOY_HOLD_US);echoy_tick(&c,lost+ECHOY_HOLD_US);
    assert(c.devices[2].session_id!=sid&&c.devices[2].lost_split);
    long double after=echoy_phase_steps(session(2),c.time_us);assert(after>=before&&after-before<.00001L);
}
static void test_rejections(void) {
    reset();boot(1);echoy_input_t e={.type=ECHOY_CONTACTS,.edge_count=2,.edges={{0,1,false},{1,0,false}}};
    assert(echoy_dispatch(&c,&e)==ECHOY_TOPOLOGY_REJECTED);e.edges[1]=e.edges[0];assert(echoy_dispatch(&c,&e)==ECHOY_TOPOLOGY_REJECTED);
    e.edge_count=1;e.edges[0].right=3;assert(echoy_dispatch(&c,&e)==ECHOY_TOPOLOGY_REJECTED);
    e.type=ECHOY_BOOT;e.device=3;assert(echoy_dispatch(&c,&e)==ECHOY_INVALID);
}

static void test_chain_and_swap(void) {
    reset();join();boot(2);
    echoy_input_t e={.type=ECHOY_CONTACTS,.at_us=c.time_us,.edge_count=2,.edges={{0,1,false},{1,2,false}}};
    assert(echoy_dispatch(&c,&e)==ECHOY_OK);echoy_tick(&c,c.time_us+ECHOY_CONTACT_US);
    send(0,ECHOY_SYNC_READY,0);echoy_tick(&c,c.sync.apply_at_us);
    assert(plan(1)->source==0&&plan(2)->source==1);
    echoy_motif_t a=plan(0)->motif;send(2,ECHOY_ACTIVITY,.95);send(2,ECHOY_FINISH_ACTIVITY,0);
    assert(!memcmp(&a,&plan(0)->motif,sizeof a));
    send(1,ECHOY_DATA_FAULT,1);assert(plan(2)->source==1);nextbar(0);
    assert(plan(1)->source==0&&plan(2)->source==-1);
    send(1,ECHOY_DATA_FAULT,0);nextbar(0);assert(plan(2)->source==1);
    e.at_us=c.time_us;e.edges[0]=(echoy_edge_t){2,1,false};e.edges[1]=(echoy_edge_t){1,0,false};
    assert(echoy_dispatch(&c,&e)==ECHOY_OK);assert(plan(2)->source==1);
    echoy_tick(&c,c.time_us+ECHOY_CONTACT_US);nextbar(0);
    assert(plan(2)->source==-1&&plan(1)->source==2&&plan(0)->source==1);
}
static void test_merge_reversal(void) {
    reset();join();boot(2);
    echoy_input_t e={.type=ECHOY_CONTACTS,.at_us=c.time_us,.edge_count=2,.edges={{1,0,false},{0,2,false}}};
    assert(echoy_dispatch(&c,&e)==ECHOY_OK);echoy_tick(&c,c.time_us+ECHOY_CONTACT_US);
    send(0,ECHOY_SYNC_READY,0);echoy_tick(&c,c.sync.apply_at_us);
    assert(c.plan.count==3&&plan(0)->source==1&&plan(2)->source==0);
}
static void test_stale_sync_and_play(void) {
    reset();boot(1);contacts(0,1);echoy_tick(&c,ECHOY_CONTACT_US);
    echoy_input_t late={.type=ECHOY_SYNC_READY,.device=0,.at_us=c.time_us,.boot_id=c.devices[0].boot_id,.generation=c.sync.generation};
    contacts(-1,0);assert(echoy_dispatch(&c,&late)==ECHOY_STALE);
    contacts(0,1);echoy_tick(&c,c.time_us+ECHOY_CONTACT_US);assert(echoy_dispatch(&c,&late)==ECHOY_STALE);
    send(0,ECHOY_SYNC_FAIL,0);assert(!strcmp(echoy_feedback_key(&c,0),"sync-failed"));
    send(1,ECHOY_LINK,0);send(1,ECHOY_LINK,1);assert(c.sync.stage==ECHOY_SYNC_PREPARING);
    send(0,ECHOY_PLAY_PREPARE,0);late=(echoy_input_t){.type=ECHOY_PLAY_READY,.device=0,.boot_id=c.devices[0].boot_id,.generation=c.devices[0].play_generation};
    send(0,ECHOY_PAUSE,0);assert(echoy_dispatch(&c,&late)==ECHOY_STALE);
    send(0,ECHOY_PLAY_PREPARE,0);send(0,ECHOY_POWER_OFF,0);boot(0);assert(echoy_dispatch(&c,&late)==ECHOY_STALE);
}
static void test_missed_play_boundary(void) {
    reset();send(0,ECHOY_PLAY_TOGGLE,0);int64_t due=c.devices[0].play_at_us;
    send(0,ECHOY_OUTPUT_FAULT,1);echoy_tick(&c,due+100000);send(0,ECHOY_OUTPUT_FAULT,0);
    assert(c.devices[0].transport==ECHOY_WAITING&&c.devices[0].play_at_us>c.time_us);
    echoy_tick(&c,c.devices[0].play_at_us);assert(c.devices[0].transport==ECHOY_PLAYING);
    send(0,ECHOY_OUTPUT_FAULT,1);send(0,ECHOY_PAUSE,0);send(0,ECHOY_OUTPUT_FAULT,0);assert(c.devices[0].transport==ECHOY_PAUSED);
}
static void test_feedback_and_hold(void) {
    reset();join();send(0,ECHOY_ROLE_REQUEST,ECHOY_VOICE);assert(!strcmp(echoy_feedback_key(&c,0),"loading-role"));
    send(0,ECHOY_DATA_LOADING,1);assert(!strcmp(echoy_feedback_key(&c,0),"loading-role"));
    send(0,ECHOY_DATA_LOADING,0);send(0,ECHOY_ROLE_FAILED,0);assert(!strcmp(echoy_feedback_key(&c,0),"error-role"));
    send(0,ECHOY_OUTPUT_FAULT,1);assert(!strcmp(echoy_feedback_key(&c,0),"error-role"));send(0,ECHOY_OUTPUT_FAULT,0);
    send(0,ECHOY_ROLE_REQUEST,ECHOY_PULSE);send(0,ECHOY_ACTIVITY,.6);assert(!strcmp(echoy_feedback_key(&c,0),"pending-activity"));
    send(0,ECHOY_FINISH_ACTIVITY,0);send(0,ECHOY_MOVE,1);assert(!strcmp(echoy_feedback_key(&c,1),"moving"));
    int64_t t=c.time_us;echoy_tick(&c,t+ECHOY_HOLD_US-1);assert(plan(1)->source==0);
    echoy_tick(&c,t+ECHOY_HOLD_US);nextbar(0);assert(plan(1)->source==-1);
    send(0,ECHOY_MOVE,0);nextbar(0);assert(plan(1)->source==0);
    send(1,ECHOY_LINK,0);int64_t lost=c.time_us;echoy_tick(&c,lost+1000000);send(1,ECHOY_LINK,0);
    echoy_tick(&c,lost+ECHOY_HOLD_US);assert(c.devices[1].lost_split);
    send(1,ECHOY_BATTERY,0);assert(c.devices[1].lifecycle==ECHOY_OFF&&c.devices[1].session_id==0);
}

static void test_sync_generation_changes_during_tick(void) {
    reset();boot(1);boot(2);contacts(0,1);echoy_tick(&c,ECHOY_CONTACT_US);
    echoy_input_t late={.type=ECHOY_SYNC_READY,.device=0,.at_us=c.time_us+ECHOY_CONTACT_US,
        .boot_id=c.devices[0].boot_id,.generation=c.sync.generation};
    echoy_input_t edges={.type=ECHOY_CONTACTS,.at_us=c.time_us,.edge_count=2,.edges={{0,1,false},{1,2,false}}};
    assert(echoy_dispatch(&c,&edges)==ECHOY_OK);
    assert(echoy_dispatch(&c,&late)==ECHOY_STALE);assert(c.sync.stage==ECHOY_SYNC_PREPARING&&c.sync.members==7);
}

static void test_monotonic_limits(void) {
    reset();echoy_tick(&c,INT64_MAX-ECHOY_BAR_US);
    send(0,ECHOY_NOTICE,3600000000.0);assert(c.devices[0].notice_until_us==INT64_MAX);
    echoy_tick(&c,0);assert(c.time_us==INT64_MAX-ECHOY_BAR_US);
}

static void test_old_boot_fault_callback(void) {
    reset();echoy_input_t old={.type=ECHOY_DATA_LOADING,.device=0,.boot_id=c.devices[0].boot_id,.value=1};
    send(0,ECHOY_POWER_OFF,0);boot(0);assert(echoy_dispatch(&c,&old)==ECHOY_STALE);
    assert(!c.devices[0].loading);
}

static void test_repeated_contacts_preserve_boundary(void) {
    for(int reverse=0;reverse<2;reverse++) {
        reset();join();uint64_t sid=c.devices[0].session_id;
        echoy_input_t report={.type=ECHOY_CONTACTS,.at_us=c.time_us,
            .edge_count=reverse?1:0,.edges={{1,0,false}}};
        assert(echoy_dispatch(&c,&report)==ECHOY_OK);
        /* A reversed edge becomes the target only after reliable confirmation. */
        if(reverse)for(int n=0;n<45;n++) {
            report.at_us+=10000;assert(echoy_dispatch(&c,&report)==ECHOY_OK);
        }
        assert(session(0)->arrangement_pending);
        int64_t apply=session(0)->arrangement_at_us;
        int64_t end=c.time_us+2*ECHOY_BAR_US;
        while(report.at_us<end) {
            report.at_us+=10000;
            /* Dispatch is the only clock driver, as with repeated BSP reports. */
            assert(echoy_dispatch(&c,&report)==ECHOY_OK);
            if(c.time_us<apply) {
                assert(session(0)->arrangement_pending&&session(0)->arrangement_at_us==apply);
                assert(plan(1)->source==0);
            } else {
                assert(!session(0)->arrangement_pending);
                assert(plan(1)->source==-1&&plan(0)->source==(reverse?1:-1));
            }
            assert(c.devices[0].session_id==sid&&c.devices[1].session_id==sid);
        }
    }
}

static void test_link_split_preserves_bar_commit(void) {
    for(int change_role=0;change_role<2;change_role++) {
        reset();join();send(0,ECHOY_PLAY_TOGGLE,0);echoy_tick(&c,c.devices[0].play_at_us);
        send(0,ECHOY_DATA_FAULT,1);nextbar(0);
        assert(!c.devices[0].motif_available&&plan(0)->events.count==0);
        echoy_tick(&c,c.time_us+100000);send(0,ECHOY_LINK,0);
        int64_t split=c.time_us+ECHOY_HOLD_US;
        send(0,ECHOY_DATA_FAULT,0);
        if(change_role){send(0,ECHOY_ROLE_REQUEST,ECHOY_HARMONY);send(0,ECHOY_ROLE_READY,0);}
        send(0,ECHOY_PHRASE,3);send(0,ECHOY_ACTIVITY,.9);send(0,ECHOY_FINISH_ACTIVITY,0);
        echoy_tick(&c,split-1);
        echoy_device_t before=c.devices[0];echoy_motif_t motif=plan(0)->motif,events=plan(0)->events;
        int64_t bar=session(0)->last_bar;
        assert(before.pending_phrase==3&&before.activity!=before.committed_activity);
        echoy_tick(&c,split);
        assert(c.devices[0].session_id!=before.session_id&&c.devices[0].lost_split);
        assert(session(0)->last_bar==bar);
        assert(c.devices[0].motif_available==before.motif_available);
        assert(c.devices[0].role==before.role&&c.devices[0].phrase==before.phrase);
        assert(c.devices[0].activity==before.activity&&c.devices[0].committed_activity==before.committed_activity);
        assert(c.devices[0].pending_role==before.pending_role&&c.devices[0].pending_phrase==before.pending_phrase);
        assert(!memcmp(&motif,&plan(0)->motif,sizeof motif)&&!memcmp(&events,&plan(0)->events,sizeof events));
        assert(c.devices[0].transport==ECHOY_PLAYING);
        int64_t boundary=echoy_next_boundary(session(0),c.time_us+1,true);
        echoy_tick(&c,boundary-1);assert(!c.devices[0].motif_available&&c.devices[0].pending_phrase==3);
        echoy_tick(&c,boundary);
        assert(c.devices[0].motif_available&&c.devices[0].pending_phrase==0&&c.devices[0].pending_role<0);
        assert(c.devices[0].role==(change_role?ECHOY_HARMONY:ECHOY_PULSE)&&c.devices[0].phrase==3);
        assert(c.devices[0].committed_activity==(change_role?.5:.9));
        assert(plan(0)->motif.count>0&&plan(0)->events.count>0);
    }
    /* A hold expiry exactly on a real boundary must still perform its commit,
     * including when no intermediate tick processed that boundary. */
    reset();join();send(0,ECHOY_PLAY_TOGGLE,0);echoy_tick(&c,c.devices[0].play_at_us);
    send(0,ECHOY_DATA_FAULT,1);nextbar(0);
    echoy_tick(&c,c.time_us+ECHOY_BAR_US-ECHOY_HOLD_US);send(0,ECHOY_LINK,0);
    int64_t split=c.time_us+ECHOY_HOLD_US;
    send(0,ECHOY_DATA_FAULT,0);send(0,ECHOY_ACTIVITY,.9);send(0,ECHOY_PHRASE,3);
    echoy_tick(&c,split);
    assert(c.devices[0].lost_split&&c.devices[0].motif_available);
    assert(c.devices[0].committed_activity==.9&&c.devices[0].phrase==3&&plan(0)->events.count>0);
}
int main(void) {test_link_split_preserves_bar_commit();test_repeated_contacts_preserve_boundary();test_old_boot_fault_callback();test_monotonic_limits();test_sync_generation_changes_during_tick();test_chain_and_swap();test_merge_reversal();test_stale_sync_and_play();test_missed_play_boundary();test_feedback_and_hold();test_transport();test_role();test_boundary_parameters();test_faults();test_sessions();test_rejections();puts("state unit passed");}
