#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "mosaico_peer_session.h"
#include "mosaico_topology.h"
const char *mosaico_edge_to_string(mosaico_edge_t edge){(void)edge;return "edge";}
static mosaico_peer_session_manager_t m[3];
static mosaico_peer_message_t q[4096];static unsigned head,tail;static uint32_t now;static unsigned rejects;
void fake_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
static void event(const mosaico_peer_session_event_t *e,void *ctx){
 unsigned n=*(unsigned*)ctx;
 if(e->type!=MOSAICO_PEER_SESSION_EVENT_SEND)return;
 assert(tail<4096);q[tail++]=(mosaico_peer_message_t){.type=e->message_type,.source_id=m[n].device_id,.target_id=e->target_id,.session_id=e->session_id,.local_edge=e->local_edge,.peer_edge=e->peer_edge,.relative_rotation=e->relative_rotation};
}
static void pump(void){
 while(head<tail){mosaico_peer_message_t msg=q[head++];for(unsigned n=0;n<3;n++)if(mosaico_peer_session_receive(&m[n],&msg,now)!=ESP_OK)rejects++;}
 head=tail=0;
}
int main(void){
 const uint64_t ids[]={0x30eda0f452a0ULL,0x30eda0f40c6cULL,0x30eda0f45936ULL};unsigned ns[]={0,1,2};
 for(unsigned n=0;n<3;n++)assert(mosaico_peer_session_init(&m[n],ids[n],NULL,event,&ns[n])==ESP_OK);
 now=10;assert(mosaico_peer_session_local_contact(&m[0],MOSAICO_EDGE_RIGHT,0,now)==ESP_OK);
 assert(mosaico_peer_session_local_contact(&m[1],MOSAICO_EDGE_LEFT,0,now)==ESP_OK);
 assert(mosaico_peer_session_local_contact(&m[1],MOSAICO_EDGE_RIGHT,0,now)==ESP_OK);
 assert(mosaico_peer_session_local_contact(&m[2],MOSAICO_EDGE_LEFT,0,now)==ESP_OK);pump();
 for(now=20;now<20000;now+=10){for(unsigned n=0;n<3;n++)mosaico_peer_session_tick(&m[n],now);pump();}
 printf("rejected_frames=%u\n",rejects);
 unsigned valid=0,bad=0;
 for(unsigned n=0;n<3;n++)for(unsigned s=0;s<4;s++){
  const mosaico_peer_session_slot_t *p=&m[n].slots[s];if(!p->local_present)continue;
  printf("node=%u local=%d state=%d peer=%012llx edge=%d rotation=%u\n",n,p->local_edge,p->state,(unsigned long long)p->peer_id,p->peer_edge,p->relative_rotation);
  if(p->state==MOSAICO_PEER_SESSION_COMMITTED){if(mosaico_edges_are_complementary(p->local_edge,p->peer_edge))valid++;else bad++;}
 }
 printf("committed_complementary=%u committed_same_edge=%u (desired four reciprocal horizontal edges)\n",valid,bad);
 return valid==4 && bad==0 ? 0:1;
}
