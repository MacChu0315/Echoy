// SPDX-License-Identifier: Apache-2.0
#include "echoy_control.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int from,to;uint8_t bytes[100];size_t n;} packet_t;
static echoy_control_t c[3];static packet_t network[4096],last_request[3][3];static unsigned used;static int64_t now;static unsigned offline;
static bool send(void *ctx,uint64_t target,const uint8_t *p,size_t n){int from=(int)(intptr_t)ctx;for(int to=0;to<3;to++)if(!(offline&(1u<<from))&&!(offline&(1u<<to))&&to!=from&&(!target||target==c[to].protocol.uid)){assert(used<4096);network[used++]=(packet_t){.from=from,.to=to,.n=n};memcpy(network[used-1].bytes,p,n);if(p[39]==1)last_request[from][to]=network[used-1];}return true;}
static void run(int64_t end){for(;now<end;now+=1000){for(int i=0;i<3;i++)echoy_control_tick(&c[i],now,false);unsigned n=used;packet_t packets[4096];memcpy(packets,network,n*sizeof *packets);used=0;for(unsigned i=0;i<n;i++){packet_t *p=&packets[i];echoy_control_receive(&c[p->to],c[p->from].protocol.uid,c[p->from].protocol.boot,p->bytes,p->n,now,now);}}}
static bool fail_publish(void *ctx,const echoy_snapshot_t *s,const echoy_bar_plan_t *p){(void)ctx;assert(s->revision==p->revision);return false;}
int main(void){
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
 echoy_control_physical(&c[0],0,101,false,now);echoy_control_physical(&c[1],100,0,false,now);
 run(30000000);assert(c[0].protocol.committed.member_count==2&&c[1].protocol.committed.member_count==2);
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
 return 0;
}
