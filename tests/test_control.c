// SPDX-License-Identifier: Apache-2.0
#include "echoy_control.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int from,to;uint8_t bytes[100];size_t n;} packet_t;
static echoy_control_t c[3];static packet_t network[4096],last_request[3][3];static unsigned used;static int64_t now;static unsigned offline;static bool late_delivery;static unsigned accepted[10],clock_calls,concurrent_packets;
static bool drop_commits;
static bool send(void *ctx,uint64_t target,const uint8_t *p,size_t n){int from=(int)(intptr_t)ctx;if(drop_commits&&p[3]==ECHOY_COMMIT)return true;for(int to=0;to<3;to++)if(!(offline&(1u<<from))&&!(offline&(1u<<to))&&to!=from&&(!target||target==c[to].protocol.uid)){assert(used<4096);network[used++]=(packet_t){.from=from,.to=to,.n=n};memcpy(network[used-1].bytes,p,n);if(p[39]==1)last_request[from][to]=network[used-1];}return true;}
static int64_t fake_processing_clock(void *ctx){(void)ctx;clock_calls++;return now;}
static void run(int64_t end){
 for(;now<end;now+=1000){
  for(int i=0;i<3;i++){assert(c[i].core.time_us<=now);assert(c[i].protocol.now_us<=now);echoy_control_tick(&c[i],now,false);assert(c[i].core.time_us==now);}
  if(late_delivery){
   unsigned initial=used,cursor=0;int64_t round=now,processed=round;
   /* Responses emitted during receive append to the same live callback queue;
    * their timestamps are later than both the loop start and earlier packets. */
   while(cursor<used){
    packet_t packet=network[cursor++];int64_t received=now+10;now+=15;
    assert(received>round);unsigned before=used;
    bool ok=echoy_control_receive_queued(&c[packet.to],c[packet.from].protocol.uid,c[packet.from].protocol.boot,
        packet.bytes,packet.n,received,&processed,fake_processing_clock,NULL);
    if(ok)accepted[packet.bytes[3]]++;
    if(packet.bytes[3]==ECHOY_HELLO)assert(ok&&c[packet.to].protocol.discovered>0);
    assert(processed>=received&&processed>=now);
    if(used>before)concurrent_packets+=used-before;
   }
   assert(cursor>=initial);used=0;
   for(int i=0;i<3;i++){echoy_control_tick(&c[i],processed,false);assert(c[i].core.time_us==processed);}
  }else{
   unsigned n=used;packet_t packets[4096];memcpy(packets,network,n*sizeof *packets);used=0;
   for(unsigned i=0;i<n;i++){packet_t *p=&packets[i];echoy_control_receive(&c[p->to],c[p->from].protocol.uid,c[p->from].protocol.boot,p->bytes,p->n,now,now);}
  }
 }
}
static bool fail_publish(void *ctx,const echoy_snapshot_t *s,const echoy_bar_plan_t *p){(void)ctx;assert(s->revision==p->revision);return false;}
static echoy_session_t *session(int node){
 for(int i=0;i<3;i++)if(c[node].core.sessions[i].id==c[node].core.devices[0].session_id)return &c[node].core.sessions[i];
 assert(false);return NULL;
}
static const echoy_plan_entry_t *entry(int node,uint64_t uid){
 for(size_t i=0;i<c[node].core.plan.count;i++){const echoy_plan_entry_t *p=&c[node].core.plan.devices[i];if(c[node].core.devices[p->device].uid==uid)return p;}
 assert(false);return NULL;
}
static void dispatch(int node,echoy_action_t action,double value){
 echoy_input_t e={.device=0,.type=action,.value=value,.at_us=now};assert(echoy_dispatch(&c[node].core,&e)==ECHOY_OK);
}
static void availability(bool loading,int source){
 assert(echoy_control_enqueue(&c[source],(echoy_ui_event_t){UI_PLAY,0}));run(now+1000000);
 uint32_t old=c[0].protocol.committed.revision;
 assert(entry(1-source,101-source)->source>=0);assert(entry(source,100+source)->motif.count>0);
 dispatch(source,loading?ECHOY_DATA_LOADING:ECHOY_DATA_FAULT,1);
 assert(entry(source,100+source)->events.count==0&&entry(source,100+source)->motif.count>0);
 for(int tries=0;tries<2000&&!(c[0].protocol.pending.active&&c[0].protocol.pending.commit_seen);tries++)run(now+1000);
 assert(c[0].protocol.pending.active&&c[0].protocol.pending.commit_seen);
 int64_t apply=c[0].protocol.pending.apply_us;
 run(apply-1000);
 for(int i=0;i<2;i++){assert(c[i].protocol.committed.revision==old);assert(entry(i,100+source)->motif_available);assert(entry(i,101-source)->source>=0);}
 run(apply+2000);
 for(int i=0;i<2;i++){
  assert(c[i].protocol.committed.revision>old);
  assert(!entry(i,100+source)->motif_available&&entry(i,100+source)->motif.count==0);
  assert(entry(i,101-source)->source==-1&&entry(i,101-source)->motif.count>0);
 }
 assert(c[0].protocol.committed.revision==c[1].protocol.committed.revision);
 assert(c[source].core.devices[0].transport==ECHOY_PLAYING&&c[1-source].core.devices[0].transport==ECHOY_PAUSED);
 old=c[0].protocol.committed.revision;dispatch(source,loading?ECHOY_DATA_LOADING:ECHOY_DATA_FAULT,0);
 assert(!entry(source,100+source)->motif_available);run(now+10000000);
 for(int i=0;i<2;i++){assert(c[i].protocol.committed.revision>old);assert(entry(i,100+source)->motif.count>0);assert(entry(i,101-source)->source>=0);}
 assert(c[0].protocol.committed.revision==c[1].protocol.committed.revision);
 assert(c[source].core.devices[0].transport==ECHOY_PLAYING&&c[1-source].core.devices[0].transport==ECHOY_PAUSED);
}
static void last_peer(bool playing){
 if(playing){assert(echoy_control_enqueue(&c[0],(echoy_ui_event_t){UI_PLAY,0}));run(now+1000000);}
 int64_t epoch=session(0)->epoch_us;int rate=session(0)->rate_ppm;offline=2;run(now+4000000);
 assert(c[0].protocol.committed.member_count==1);
 assert(session(0)->epoch_us==epoch&&session(0)->rate_ppm==rate);
 assert(!session(0)->wire_managed);
 assert(echoy_control_enqueue(&c[0],(echoy_ui_event_t){UI_ROLE_SELECT,ECHOY_HARMONY}));
 assert(echoy_control_enqueue(&c[0],(echoy_ui_event_t){UI_PHRASE_SELECT,3}));
 echoy_control_tick(&c[0],now,false);
 if(playing){assert(c[0].core.devices[0].role==ECHOY_PULSE);run(echoy_next_boundary(session(0),now+1,true)+1000);}
 assert(c[0].core.devices[0].role==ECHOY_HARMONY&&c[0].core.devices[0].phrase==3);
 assert(echoy_control_enqueue(&c[0],(echoy_ui_event_t){UI_SET_ACTIVITY,.8}));
 assert(echoy_control_enqueue(&c[0],(echoy_ui_event_t){UI_FINISH_ACTIVITY,0}));echoy_control_tick(&c[0],now,false);
 if(playing)run(echoy_next_boundary(session(0),now+1,true)+1000);
 assert(c[0].core.devices[0].committed_activity==.8&&c[0].core.devices[0].pending_role==-1&&!c[0].core.devices[0].pending_phrase);
 assert(c[0].core.devices[0].transport==(playing?ECHOY_PLAYING:ECHOY_PAUSED));
 offline=0;run(now+32000000);
 assert(c[0].protocol.committed.member_count==2&&c[1].protocol.committed.member_count==2);
 assert(session(0)->wire_managed&&session(1)->wire_managed);
}
static void degraded_feedback(void){
 assert(echoy_control_enqueue(&c[1],(echoy_ui_event_t){UI_PLAY,0}));run(now+1000000);
 uint32_t old=c[1].protocol.committed.revision;drop_commits=true;
 assert(echoy_control_enqueue(&c[0],(echoy_ui_event_t){UI_PHRASE_SELECT,2}));run(now+10000000);
 assert(c[1].protocol.degraded&&c[1].protocol.syncing);
 assert(c[1].core.sync.stage==ECHOY_SYNC_FAILED);
 assert(!strcmp(echoy_feedback_key(&c[1].core,0),"sync-failed"));
 assert(c[1].protocol.committed.revision==old&&entry(1,100)->phrase==1);
 assert(c[1].core.devices[0].transport==ECHOY_PLAYING&&entry(1,101)->events.count>0);
 drop_commits=false;run(now+12000000);
 for(int i=0;i<2;i++){assert(!c[i].protocol.degraded&&!c[i].protocol.syncing);assert(c[i].core.sync.stage==ECHOY_SYNC_NONE);assert(strcmp(echoy_feedback_key(&c[i].core,0),"sync-failed"));assert(entry(i,100)->phrase==2);}
}
int main(int argc,char **argv){
 late_delivery=argc>1&&!strcmp(argv[1],"queued");
 echoy_settings_t settings;echoy_settings_defaults(&settings);
 for(int i=0;i<3;i++)echoy_control_init(&c[i],100+i,10+i,50+i,&settings,send,(void *)(intptr_t)i);
 assert(c[0].core.devices[0].lifecycle==ECHOY_READY&&c[0].core.devices[0].transport==ECHOY_PAUSED);
 for(int i=0;i<ECHOY_CONTROL_DEPTH;i++)assert(echoy_control_enqueue(&c[0],(echoy_ui_event_t){UI_SET_VOLUME,.4}));
 assert(!echoy_control_enqueue(&c[0],(echoy_ui_event_t){UI_PLAY,0})&&c[0].dropped==1);
 c[0].publish=fail_publish;echoy_control_tick(&c[0],0,true);assert(c[0].core.devices[0].output_fault&&!c[0].core.devices[0].data_fault&&c[0].publication_failures==1);c[0].publish=NULL;
 echoy_packet_t request={.kind=ECHOY_PREPARE,.flags=1,.sender_boot=11,.sequence=1,.session={101,11,51},.transaction=1,.apply_bar=1,.fragment_count=2,.body_length=60};
 uint8_t bytes[100];size_t size;assert(echoy_wire_encode(&request,bytes,&size));
 assert(!echoy_control_receive(&c[0],101,11,bytes,size,0,0));assert(!c[0].protocol.peers[0].authorized);
 request.flags=2;assert(!echoy_wire_encode(&request,bytes,&size));request.flags=1;request.kind=ECHOY_COMMIT;assert(!echoy_wire_encode(&request,bytes,&size));
 run(1000000);for(int i=0;i<3;i++)assert(!c[i].protocol.peers[0].authorized); /* HELLO cannot authorize. */
 if(argc>1&&strstr(argv[1],"follower")){echoy_control_physical(&c[0],101,0,false,now);echoy_control_physical(&c[1],0,100,false,now);}
 else {echoy_control_physical(&c[0],0,101,false,now);echoy_control_physical(&c[1],100,0,false,now);}
 run(30000000);assert(c[0].protocol.committed.member_count==2&&c[1].protocol.committed.member_count==2);
 if(argc>1&&!strcmp(argv[1],"availability-follower")){availability(false,1);return 0;}
 if(argc>1&&!strcmp(argv[1],"loading-follower")){availability(true,1);return 0;}
 if(argc>1&&!strcmp(argv[1],"availability")){availability(false,0);return 0;}
 if(argc>1&&!strcmp(argv[1],"loading")){availability(true,0);return 0;}
 if(argc>1&&!strcmp(argv[1],"split-playing")){last_peer(true);return 0;}
 if(argc>1&&!strcmp(argv[1],"split-paused")){last_peer(false);return 0;}
 if(argc>1&&!strcmp(argv[1],"degraded")){degraded_feedback();return 0;}
 echoy_control_physical(&c[1],100,102,false,now);echoy_control_physical(&c[2],101,0,false,now);
 run(65000000);for(int i=0;i<3;i++)assert(c[i].protocol.committed.member_count==3&&c[i].protocol.committed.edge_count==2);
 static echoy_core_t future;echoy_protocol_pending_t saved=c[2].protocol.pending;
 c[2].protocol.pending.active=true;c[2].protocol.pending.commit_seen=true;c[2].protocol.pending.apply_us=now+30000;
 c[2].protocol.pending.state=c[2].protocol.committed;c[2].protocol.pending.session=c[2].protocol.session;
 for(int j=0;j<3;j++)if(c[2].protocol.pending.state.members[j].uid==102)c[2].protocol.pending.state.members[j].role=3;
 assert(echoy_control_forecast(&c[2],c[2].core.time_us+40000,&future));assert(future.devices[0].role==3&&c[2].core.devices[0].role==0);
 c[2].protocol.pending=saved;
 assert(echoy_control_enqueue(&c[2],(echoy_ui_event_t){UI_ROLE_SELECT,ECHOY_HARMONY}));run(76000000);
 for(int i=0;i<3;i++){bool found=false;for(int j=0;j<3;j++)if(c[i].protocol.committed.members[j].uid==102){assert(c[i].protocol.committed.members[j].role==3);found=true;}assert(found);}
 packet_t *replay=&last_request[2][0];assert(replay->n);unsigned rejects=c[0].rejected;
 assert(!echoy_control_receive(&c[0],102,12,replay->bytes,replay->n,now,now)&&c[0].rejected==rejects+1);
 assert(!echoy_protocol_receive(&c[0].protocol,102,12,replay->bytes,replay->n,now,now));
 assert(!echoy_control_receive(&c[0],102,99,replay->bytes,replay->n,now,now));
 offline=1;run(82000000);assert(c[1].protocol.committed.member_count==2&&c[2].protocol.committed.member_count==2);
 if(late_delivery){assert(clock_calls>0&&concurrent_packets>0);assert(accepted[ECHOY_HELLO]&&accepted[ECHOY_TIME_REQ]&&accepted[ECHOY_TIME_RESP]&&accepted[ECHOY_PREPARE]&&accepted[ECHOY_COMMIT]);}
 return 0;
}
