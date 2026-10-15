#include "echoy_radio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { echoy_radio_t *r; uint64_t id; uint32_t boot; uint64_t edge[2];
 unsigned delivered,failed,received,lost,received_tags,delivered_tags; uint8_t payload[64]; int64_t last_send; } node_t;
static node_t nodes[3];static int64_t now;
static bool isolated[3],drop_ack,wire_busy[3];
static uint8_t saved_advert[3][100];static size_t saved_size[3];static unsigned send_fail_every,sends;
static bool stamp_enabled,fail_next_data;static unsigned stamps;static int64_t last_stamp;
static struct { unsigned from; uint64_t to; uint8_t bytes[100]; size_t size; uint32_t boot; } frames[256];
static unsigned head,tail;
static bool send_wire(void *ctx,uint64_t to,const uint8_t *p,size_t n){
 node_t *a=ctx;assert(n<=100);assert(now-a->last_send>=10000 || !a->last_send);a->last_send=now;
 if(p[5]==1){memcpy(saved_advert[a-nodes],p,n);saved_size[a-nodes]=n;}
 ++sends;if(fail_next_data && p[5]==4){fail_next_data=false;return false;}
 if(send_fail_every && sends%send_fail_every==0)return false;
 assert(tail<256);frames[tail].from=(unsigned)(a-nodes);frames[tail].to=to;frames[tail].size=n;
 frames[tail].boot=a->boot;memcpy(frames[tail++].bytes,p,n);return true;
}
static void edge(void *ctx,bool left,uint64_t peer,uint32_t session,int64_t t){
 (void)t;node_t *a=ctx;assert(!peer || session);a->edge[left?0:1]=peer;
}
static void lost(void *ctx,bool left,int64_t t){(void)left;(void)t;((node_t*)ctx)->lost++;}
static void receive(void *ctx,uint64_t source,const uint8_t *p,size_t n,int64_t t){
 (void)source;(void)t;node_t *a=ctx;assert(n<=64);a->received++;if(n && p[0]<32)a->received_tags|=1u<<p[0];memcpy(a->payload,p,n);
}
static void delivery(void *ctx,uint64_t to,uint16_t service,bool success,int64_t t){
 (void)to;(void)service;(void)t;node_t *a=ctx;if(success){a->delivered++;a->delivered_tags|=1u<<(service&31u);}else a->failed++;
}
static void pump(void){
 while(head<tail){unsigned q=head++,a=frames[q].from;
  if(drop_ack && frames[q].bytes[5]==5)continue;
  for(unsigned b=0;b<3;b++)if(a!=b && !isolated[a] && !isolated[b] && (!frames[q].to || frames[q].to==nodes[b].id))
   echoy_radio_receive(nodes[b].r,nodes[a].id,frames[q].boot,frames[q].bytes,frames[q].size,now);
 }head=tail=0;
}
static void advance(int64_t duration){int64_t end=now+duration;
 for(;now<end;now+=5000){for(unsigned i=0;i<3;i++)echoy_radio_tick(nodes[i].r,now);pump();}
}
static void stamp(void *ctx,uint8_t *data,size_t size,int64_t at){
 (void)ctx;assert(size==64);memcpy(data,&at,sizeof(at));stamps++;last_stamp=at;
}
static bool available(void *ctx){return !wire_busy[(node_t*)ctx-nodes];}
static void setup(void){
 memset(nodes,0,sizeof(nodes));memset(wire_busy,0,sizeof(wire_busy));memset(isolated,0,sizeof(isolated));now=10000;head=tail=sends=send_fail_every=0;drop_ack=false;fail_next_data=false;stamps=0;last_stamp=0;memset(saved_size,0,sizeof(saved_size));
 uint64_t ids[]={0x30eda0f45936ULL,0x30eda0f452a0ULL,0x30eda0f40c6cULL};
 for(unsigned i=0;i<3;i++){node_t *n=&nodes[i];n->id=ids[i];n->boot=10+i;n->r=calloc(1,echoy_radio_size());assert(n->r);
  echoy_radio_callbacks_t cb={send_wire,edge,lost,receive,delivery,n,stamp_enabled?stamp:NULL,available};assert(echoy_radio_init(n->r,echoy_radio_size(),n->id,n->boot,&cb));
  echoy_radio_contacts(n->r,true,0,now);
 }
}
static void cleanup(void){for(unsigned i=0;i<3;i++)free(nodes[i].r);}
static void chain(const unsigned order[3]){
 for(unsigned i=0;i<3;i++)echoy_radio_contacts(nodes[order[i]].r,true,i==0?2:i==1?3:1,now);
}
static void check_chain(const unsigned order[3]){
 for(unsigned i=0;i<3;i++){node_t *n=&nodes[order[i]];echoy_radio_info_t v;assert(echoy_radio_info(n->r,&v));
  assert(v.admitted && v.count==3 && v.rank==i);
  assert(n->edge[0]==(i?nodes[order[i-1]].id:0));assert(n->edge[1]==(i<2?nodes[order[i+1]].id:0));
 }
}
static void pair_and_invalid_graphs(void){
 setup();advance(1000000);
 echoy_radio_contacts(nodes[0].r,true,2,now);echoy_radio_contacts(nodes[1].r,true,1,now);advance(2500000);
 assert(nodes[0].edge[1]==nodes[1].id && nodes[1].edge[0]==nodes[0].id);
 echoy_radio_info_t v;assert(echoy_radio_info(nodes[2].r,&v));assert(!v.admitted && !nodes[2].edge[0] && !nodes[2].edge[1]);
 echoy_radio_contacts(nodes[0].r,true,1,now);advance(2500000);assert(echoy_radio_info(nodes[0].r,&v));assert(!v.admitted);cleanup();
 puts("PASS pair plus isolated third; same-side contact cannot pair");
}
static void missing_middle_and_legacy_are_not_absent(void){
 const unsigned order[3]={0,1,2};setup();advance(1500000);isolated[1]=true;chain(order);advance(3000000);
 for(unsigned i=0;i<3;i++){echoy_radio_info_t v;assert(echoy_radio_info(nodes[i].r,&v));assert(!v.admitted);assert(!nodes[i].edge[0]&&!nodes[i].edge[1]);}
 isolated[1]=false;advance(3000000);check_chain(order);
 isolated[1]=true;advance(2000000);assert(nodes[0].edge[1]==nodes[1].id && nodes[2].edge[0]==nodes[1].id);assert(nodes[0].lost && nodes[2].lost);
 isolated[1]=false;advance(3000000);check_chain(order);cleanup();
 setup();isolated[2]=true;echoy_radio_observe_unknown(nodes[0].r,nodes[2].id,nodes[2].boot,now);
 echoy_radio_contacts(nodes[0].r,true,2,now);echoy_radio_contacts(nodes[1].r,true,1,now);advance(3000000);
 assert(!nodes[0].edge[1]&&!nodes[1].edge[0]);cleanup();
 puts("PASS stale known middle/legacy blocks shortcut; radio loss retains physical edges and recovers");
}
static void transport_failure_and_backpressure(void){
 const unsigned order[3]={0,1,2};setup();chain(order);advance(3000000);check_chain(order);
 uint8_t p[64]={1,2,3};drop_ack=true;
 assert(echoy_radio_send(nodes[0].r,nodes[2].id,0xe002,p,sizeof(p),now));advance(2500000);
 assert(nodes[2].received==1 && nodes[0].delivered==0 && nodes[0].failed==1);
 drop_ack=false;send_fail_every=3;
 for(unsigned i=0;i<6;i++)assert(echoy_radio_send(nodes[0].r,nodes[2].id,(uint16_t)(0xe010+i),p,sizeof(p),now));
 assert(!echoy_radio_send(nodes[0].r,nodes[2].id,0xe099,p,sizeof(p),now));advance(2500000);
 assert(nodes[0].delivered+nodes[0].failed==7);assert(nodes[2].received<=7);
 echoy_radio_info_t v;assert(echoy_radio_info(nodes[0].r,&v));assert(!v.pending && v.queue_high_water==6 && v.send_failures>0);cleanup();
 puts("PASS lost acknowledgements never duplicate delivery; bounded slots/retries and send-error backoff");
}
static void reboot_and_stale_boot(void){
 const unsigned order[3]={0,1,2};setup();chain(order);advance(3000000);check_chain(order);
 uint8_t old[100];size_t size=saved_size[1];memcpy(old,saved_advert[1],size);uint32_t boot=nodes[1].boot;
 node_t *n=&nodes[1];free(n->r);n->r=calloc(1,echoy_radio_size());n->boot++;n->edge[0]=n->edge[1]=0;n->last_send=0;
 echoy_radio_callbacks_t cb={send_wire,edge,lost,receive,delivery,n,stamp_enabled?stamp:NULL,available};assert(echoy_radio_init(n->r,echoy_radio_size(),n->id,n->boot,&cb));echoy_radio_contacts(n->r,true,3,now);
 advance(3000000);check_chain(order);
 for(unsigned i=0;i<3;i++)if(i!=1)echoy_radio_receive(nodes[i].r,n->id,boot,old,size,now);
 advance(2000000);check_chain(order);cleanup();
 puts("PASS boot challenge rejects replayed pre-reboot advertisement");
}
static void overflow_and_transport_loss_preserve_contacts(void){
 const unsigned order[3]={0,1,2};setup();chain(order);advance(3000000);check_chain(order);
 echoy_radio_recover(nodes[0].r,now);echoy_radio_info_t v;assert(echoy_radio_info(nodes[0].r,&v));
 assert(!v.admitted && nodes[0].edge[1]==nodes[1].id);advance(3000000);check_chain(order);
 echoy_radio_observe_unknown(nodes[0].r,44,99,now);assert(echoy_radio_info(nodes[0].r,&v));
 assert(!v.admitted && nodes[0].edge[1]==nodes[1].id);cleanup();
 puts("PASS receive loss and excess scene capacity fail closed without inventing contact release");
}
static void stale_advertisement_after_recover(void){
 const unsigned abc[3]={0,1,2},cba[3]={2,1,0};setup();chain(abc);advance(3000000);check_chain(abc);
 uint8_t old[3][100];size_t size[3];memcpy(old,saved_advert,sizeof(old));memcpy(size,saved_size,sizeof(size));
 chain(cba);advance(3000000);check_chain(cba);
 echoy_radio_recover(nodes[1].r,now);
 echoy_radio_receive(nodes[1].r,nodes[0].id,nodes[0].boot,old[0],size[0],now);
 echoy_radio_receive(nodes[1].r,nodes[2].id,nodes[2].boot,old[2],size[2],now);
 echoy_radio_info_t v;assert(echoy_radio_info(nodes[1].r,&v));
 assert(!v.admitted && nodes[1].edge[0]==nodes[2].id && nodes[1].edge[1]==nodes[0].id);
 advance(3000000);check_chain(cba);cleanup();
 puts("PASS recovery preserves sequence/contact generation watermarks against old-position replay");
}
static void wire_timestamp_is_stable_across_ack_retries(void){
 const unsigned order[3]={0,1,2};stamp_enabled=true;setup();chain(order);advance(3000000);check_chain(order);
 uint8_t data[64]={0};fail_next_data=true;drop_ack=true;
 assert(echoy_radio_send(nodes[0].r,nodes[2].id,0xe001,data,sizeof(data),now));advance(2500000);
 int64_t received_stamp=0;memcpy(&received_stamp,nodes[2].payload,sizeof(received_stamp));
 assert(stamps==2 && received_stamp==last_stamp && nodes[2].received==1 && nodes[0].failed==1);
 cleanup();stamp_enabled=false;
 puts("PASS timestamp follows actual first accepted submission; ACK retries/forwarding retain identical bytes");
}
static void driver_completion_controls_wire_submission(void){
 const unsigned order[3]={0,1,2};setup();chain(order);advance(3000000);check_chain(order);
 uint8_t data[64]={7};wire_busy[0]=true;int64_t last=nodes[0].last_send;
 assert(echoy_radio_send(nodes[0].r,nodes[2].id,0xe001,data,sizeof(data),now));advance(250000);
 assert(nodes[0].last_send==last && nodes[2].received==0);
 wire_busy[0]=false;advance(1000000);assert(nodes[0].delivered==1 && nodes[2].received==1);cleanup();
 puts("PASS previous driver completion gates submission without consuming retries");
}
static void busy_middle_drains_every_relay_slot(void){
 const unsigned order[3]={0,1,2};setup();chain(order);advance(3000000);check_chain(order);
 wire_busy[1]=true;
 for(unsigned tag=1;tag<=4;tag++){uint8_t data[64]={(uint8_t)tag};assert(echoy_radio_send(nodes[0].r,nodes[2].id,(uint16_t)(0xe010+tag),data,sizeof(data),now));}
 advance(100000);assert(!nodes[2].received);
 int64_t until=now+1000000;
 for(;now<until;){
  /* A busy middle completes at20ms while endpoints can submit at10ms.
   * New packets must not continually reuse low slots ahead of older work. */
  wire_busy[1]=(now/10000)%2!=0;
  uint8_t data[64]={5};(void)echoy_radio_send(nodes[0].r,nodes[2].id,0xe020,data,sizeof(data),now);
  advance(5000);
 }
 assert((nodes[2].received_tags&0x1eu)==0x1eu);cleanup();
 puts("PASS a slower busy middle drains all relay slots despite continuous new arrivals");
}
static void busy_receiver_acknowledges_every_queued_packet(void){
 const unsigned order[3]={0,1,2};setup();chain(order);advance(3000000);check_chain(order);
 wire_busy[1]=true;
 for(unsigned tag=1;tag<=4;tag++){uint8_t data[64]={(uint8_t)tag};assert(echoy_radio_send(nodes[0].r,nodes[1].id,(uint16_t)(0xe010+tag),data,sizeof(data),now));}
 advance(100000);assert(nodes[1].received==4 && !nodes[0].delivered);
 int64_t until=now+1000000;
 for(;now<until;){
  wire_busy[1]=(now/10000)%2!=0;
  uint8_t data[64]={5};(void)echoy_radio_send(nodes[0].r,nodes[1].id,0xe020,data,sizeof(data),now);
  advance(5000);
 }
 assert((nodes[0].delivered_tags&0x1e0000u)==0x1e0000u);cleanup();
 puts("PASS older acknowledgements drain despite continuous new arrivals");
}
int main(void){
 const unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
 for(unsigned p=0;p<6;p++){
  setup();chain(orders[p]);advance(3000000);check_chain(orders[p]);
  const uint8_t bytes[64]={7,3,99};node_t *a=&nodes[orders[p][0]],*c=&nodes[orders[p][2]];
  assert(echoy_radio_send(a->r,c->id,0xe001,bytes,sizeof(bytes),now));advance(1000000);
  assert(a->delivered==1 && c->received==1 && !memcmp(c->payload,bytes,sizeof(bytes)));
  printf("PASS all-broadcast static order %u and real two-hop delivery\n",p);cleanup();
 }
 setup();advance(1000000);chain(orders[0]);advance(3000000);check_chain(orders[0]);
 echoy_radio_contacts(nodes[0].r,true,0,now);assert(!nodes[0].edge[1]);
 echoy_radio_contacts(nodes[1].r,true,2,now);assert(!nodes[1].edge[0]);
 /* No arbitrary user dwell: next sample can establish the new physical edges. */
 chain(orders[3]);advance(3000000);check_chain(orders[3]);cleanup();
 puts("PASS immediate physical release and fast ABC to BCA");
 pair_and_invalid_graphs();missing_middle_and_legacy_are_not_absent();
 transport_failure_and_backpressure();reboot_and_stale_boot();overflow_and_transport_loss_preserve_contacts();stale_advertisement_after_recover();wire_timestamp_is_stable_across_ack_retries();driver_completion_controls_wire_submission();busy_receiver_acknowledges_every_queued_packet();busy_middle_drains_every_relay_slot();
 return 0;
}
