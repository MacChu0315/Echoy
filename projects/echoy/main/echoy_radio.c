#include "echoy_radio.h"
#include <string.h>

#define FRESH INT64_C(1500000)
#define INTERVAL INT64_C(250000)
#define RETRY INT64_C(200000)
#define EXPIRE INT64_C(2000000)
/* The100Hz worker can return slightly under10ms after sensor/work jitter.
 * Keep a5ms floor and the driver-completion gate rather than losing that slot. */
#define WIRE_INTERVAL INT64_C(5000)
#define RECEIPTS 512u
#define READY 4u
#define OWN 6u
#define RELAY 4u
#define ACKS 4u
enum { ADVERTISE=1,PROBE,PROOF,DATA,ACK };
typedef struct { uint64_t id; uint32_t boot,generation; uint8_t state; } tuple_t;
typedef struct {
    tuple_t value,view[3];
    bool verified,advertised,ready;
    uint8_t count;
    uint32_t sequence,pending_boot,challenge,reply_boot,reply_challenge;
    uint64_t candidate;
    int64_t seen,next_probe;
} peer_t;
typedef struct {
    bool used,stamped; uint64_t next_hop,destination; uint16_t service;
    uint8_t frame[88],size,attempts; uint32_t sequence;
    int64_t created,due,first_sent_at; uint32_t measure_generation;
} pending_t;
struct echoy_radio {
    echoy_radio_callbacks_t cb;
    peer_t peers[3]; unsigned known;
    uint32_t sequence,challenge,data_sequence;
    uint64_t candidate,graph,members[3],edges[2];
    uint32_t sessions[2];
    unsigned count,rank,own_cursor,relay_cursor,ack_cursor,kind_cursor;
    bool ready,admitted,scene_fresh,overflow,lost;
    int64_t next_advertise,next_wire;
    pending_t own[OWN],relay[RELAY],acks[ACKS];
    uint32_t latest[3]; uint64_t received_bits[3][RECEIPTS/64];
    echoy_radio_info_t stats;
};
static uint32_t r32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t r64(const uint8_t *p){return (uint64_t)r32(p)|((uint64_t)r32(p+4)<<32);}
static void w32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static void w64(uint8_t *p,uint64_t x){w32(p,(uint32_t)x);w32(p+4,(uint32_t)(x>>32));}
static bool newer(uint32_t a,uint32_t b){return a!=b && (uint32_t)(a-b)<UINT32_C(0x80000000);}
static void header(uint8_t *p,unsigned type){memset(p,0,88);memcpy(p,"ECHN",4);p[4]=1;p[5]=(uint8_t)type;}
static int find(const echoy_radio_t *r,uint64_t id){for(unsigned i=0;i<r->known;i++)if(r->peers[i].value.id==id)return (int)i;return -1;}
static int rank_of(const echoy_radio_t *r,uint64_t id){for(unsigned i=0;i<r->count;i++)if(r->members[i]==id)return (int)i;return -1;}
static bool same(tuple_t a,tuple_t b){return a.id==b.id && a.boot==b.boot && a.generation==b.generation && a.state==b.state;}
static uint32_t serial(uint32_t *v){if(!++*v)++*v;return *v;}
static bool measured(const echoy_radio_t *r,const pending_t *p){
 return p->measure_generation && p->measure_generation==r->stats.phrase.generation;
}
static void measure_done(echoy_radio_t *r,const pending_t *p,bool ok,int64_t now){
 if(!measured(r,p))return;
 echoy_radio_phrase_timing_t *m=&r->stats.phrase;
 if(!ok){++m->failed;return;}
 ++m->acked;m->last_acked_us=now;
 if(p->stamped && now>=p->first_sent_at){uint64_t elapsed=(uint64_t)(now-p->first_sent_at);
  ++m->ack_samples;m->ack_us_sum+=elapsed;if(elapsed>m->ack_us_max)m->ack_us_max=elapsed;}
}
static void measure_queued(echoy_radio_t *r,pending_t *p,const uint8_t *data,size_t size,int64_t now){
 /* Application V3 DATA header: group, attempt and immutable plan token.
  * Classification affects diagnostics only, never acceptance or scheduling. */
 if(size<=26 || data[0]!=3 || data[1]!=6 || !r32(data+4) || !r32(data+8) || !r64(data+16))return;
 echoy_radio_phrase_timing_t *m=&r->stats.phrase;
 if(!m->generation || m->graph!=r->graph || m->destination!=p->destination ||
    m->identity!=r32(data+4) || m->attempt!=r32(data+8) || m->plan!=r64(data+16)){
  uint32_t generation=m->generation;serial(&generation);
  *m=(echoy_radio_phrase_timing_t){.generation=generation,.graph=r->graph,
   .destination=p->destination,.identity=r32(data+4),.attempt=r32(data+8),
   .plan=r64(data+16),.first_queued_us=now};
 }
 p->measure_generation=m->generation;++m->queued;
}
static void measure_sent(echoy_radio_t *r,pending_t *p,bool ok,int64_t now){
 if(!measured(r,p))return;
 echoy_radio_phrase_timing_t *m=&r->stats.phrase;
 if(!ok){++m->submit_failures;return;}
 if(p->stamped){++m->retransmitted;return;}
 p->first_sent_at=now;++m->first_sent;
 uint64_t elapsed=(uint64_t)(now-p->created);
 m->queue_us_sum+=elapsed;if(elapsed>m->queue_us_max)m->queue_us_max=elapsed;
}
static void edge(echoy_radio_t *r,unsigned side,uint64_t id,uint32_t session,int64_t now){
 if(r->edges[side]==id && r->sessions[side]==session)return;
 r->edges[side]=id;r->sessions[side]=session;r->cb.edge(r->cb.ctx,side==0,id,session,now);
}
static void invalidate(echoy_radio_t *r,int64_t now){
 r->admitted=false;
 for(unsigned i=0;i<OWN;i++)if(r->own[i].used){pending_t old=r->own[i];measure_done(r,&old,false,now);r->own[i].used=false;r->cb.delivery(r->cb.ctx,old.destination,old.service,false,now);}
 memset(r->relay,0,sizeof(r->relay));memset(r->acks,0,sizeof(r->acks));
 if(!r->lost){for(unsigned i=0;i<2;i++)if(r->edges[i])r->cb.lost(r->cb.ctx,i==0,now);r->lost=true;}
}
static peer_t *learn(echoy_radio_t *r,tuple_t hint){
 int at=find(r,hint.id);if(at>=0)return &r->peers[at];
 if(r->known==3){r->overflow=true;return NULL;}
 peer_t *p=&r->peers[r->known++];memset(p,0,sizeof(*p));p->value=hint;return p;
}
static void challenge(echoy_radio_t *r,peer_t *p,uint32_t boot){
 if(p->pending_boot==boot)return;
 p->pending_boot=boot;p->challenge=serial(&r->challenge);p->next_probe=0;
}
void echoy_radio_observe_unknown(echoy_radio_t *r,uint64_t id,uint32_t boot,int64_t now){
 if(!r||!id||id==r->peers[0].value.id||!boot||now<0)return;
 peer_t *p=learn(r,(tuple_t){.id=id,.boot=boot});
 if(!p || !p->verified)invalidate(r,now);
 if(p && (!p->verified || p->value.boot!=boot))challenge(r,p,boot);
}
static void tuples(const echoy_radio_t *r,tuple_t out[3]){
 for(unsigned i=0;i<r->known;i++)out[i]=r->peers[i].value;
 for(unsigned i=1;i<r->known;i++)for(unsigned j=i;j && out[j].id<out[j-1].id;j--){tuple_t t=out[j];out[j]=out[j-1];out[j-1]=t;}
}
static uint64_t hash_view(const tuple_t *v,unsigned n){
 uint64_t h=UINT64_C(14695981039346656037);
 for(unsigned i=0;i<n;i++){
  uint8_t bytes[17];w64(bytes,v[i].id);w32(bytes+8,v[i].boot);w32(bytes+12,v[i].generation);bytes[16]=v[i].state;
  for(unsigned j=0;j<sizeof(bytes);j++){h^=bytes[j];h*=UINT64_C(1099511628211);}
 }return h?h:1;
}
static void topology(echoy_radio_t *r,int64_t now){
 tuple_t view[3];tuples(r,view);r->scene_fresh=!r->overflow;
 for(unsigned i=0;i<r->known;i++){
  peer_t *p=&r->peers[i];
  if(!p->verified || !p->value.generation || !(p->value.state&READY) ||
     (i && (!p->advertised || now-p->seen>=FRESH)))r->scene_fresh=false;
 }
 /* A peer's fresh confirmed release is physical evidence, unlike silence. */
 for(unsigned s=0;s<2;s++)if(r->edges[s]){
  int i=find(r,r->edges[s]);
  if(i>=0){peer_t *p=&r->peers[i];unsigned opposite=s==0?ECHOY_CONTACT_RIGHT:ECHOY_CONTACT_LEFT;
   if(p->verified && p->advertised && now-p->seen<FRESH && (p->value.state&READY) && !(p->value.state&opposite))edge(r,s,0,0,now);
  }
 }
 uint64_t order[3]={0};unsigned left=0,middle=0,right=0,active=0;
 if(r->scene_fresh)for(unsigned i=0;i<r->known;i++){
  tuple_t t=r->peers[i].value;unsigned mask=t.state&3u;
  if(mask==2){left++;order[0]=t.id;active++;}
  else if(mask==3){middle++;order[1]=t.id;active++;}
  else if(mask==1){right++;order[2]=t.id;active++;}
 }
 bool valid=r->scene_fresh && left==1 && right==1 && middle<=1 && active==2+middle;
 uint64_t candidate=valid?hash_view(view,r->known):0;
 if(!middle){order[1]=order[2];order[2]=0;}
 bool ready=valid;
 for(unsigned i=1;i<r->known && ready;i++){
  peer_t *p=&r->peers[i];
  if(p->count!=r->known || p->candidate!=candidate){ready=false;break;}
  for(unsigned j=0;j<r->known;j++)if(!same(view[j],p->view[j]))ready=false;
 }
 if(candidate!=r->candidate || ready!=r->ready){r->candidate=candidate;r->ready=ready;r->next_advertise=0;}
 bool committed=ready;
 for(unsigned i=1;i<r->known && committed;i++)if(!r->peers[i].ready)committed=false;
 int rank=-1;for(unsigned i=0;i<active && i<3;i++)if(order[i]==r->peers[0].value.id)rank=(int)i;
 if(!committed || rank<0){if(r->admitted)invalidate(r,now);return;}
 if(r->admitted && r->graph==candidate)return;
 if(r->graph!=candidate){invalidate(r,now);memset(r->latest,0,sizeof(r->latest));memset(r->received_bits,0,sizeof(r->received_bits));}
 r->graph=candidate;r->count=active;r->rank=(unsigned)rank;memcpy(r->members,order,sizeof(order));r->admitted=true;r->lost=false;
 uint32_t session=(uint32_t)candidate^(uint32_t)(candidate>>32);if(!session)session=1;
 edge(r,0,rank?order[rank-1]:0,rank?session:0,now);
 edge(r,1,(unsigned)rank+1<active?order[rank+1]:0,(unsigned)rank+1<active?session:0,now);
}
size_t echoy_radio_size(void){return sizeof(echoy_radio_t);}
bool echoy_radio_init(echoy_radio_t *r,size_t n,uint64_t id,uint32_t boot,const echoy_radio_callbacks_t *cb){
 if(!r||n<sizeof(*r)||!id||!boot||!cb||!cb->send||!cb->edge||!cb->lost||!cb->receive||!cb->delivery)return false;
 memset(r,0,sizeof(*r));r->cb=*cb;r->known=1;r->peers[0].value=(tuple_t){id,boot,1,0};r->peers[0].verified=true;return true;
}
void echoy_radio_contacts(echoy_radio_t *r,bool ready,uint8_t mask,int64_t now){
 if(!r||now<0||(mask&~3u))return;
 uint8_t state=ready?(uint8_t)(READY|mask):0;peer_t *p=&r->peers[0];if(p->value.state==state)return;
 p->value.state=state;serial(&p->value.generation);r->next_advertise=0;r->candidate=0;r->ready=false;
 for(unsigned s=0;s<2;s++)if(!(state&(1u<<s)))edge(r,s,0,0,now);
 invalidate(r,now);topology(r,now);
}
void echoy_radio_recover(echoy_radio_t *r,int64_t now){
 if(!r||now<0)return;
 invalidate(r,now);r->candidate=0;r->ready=false;r->next_advertise=0;
 for(unsigned i=1;i<r->known;i++)r->peers[i].advertised=false;
}
static void advertisement(echoy_radio_t *r,uint8_t frame[88]){
 header(frame,ADVERTISE);frame[6]=(uint8_t)r->known;frame[7]=r->ready?1:0;
 w32(frame+8,r->peers[0].value.boot);w32(frame+12,serial(&r->sequence));w64(frame+16,r->candidate);
 tuple_t view[3];tuples(r,view);
 for(unsigned i=0;i<r->known;i++){uint8_t *p=frame+24+20*i;w64(p,view[i].id);w32(p+8,view[i].boot);w32(p+12,view[i].generation);p[16]=view[i].state;}
}
static void read_advertisement(echoy_radio_t *r,uint64_t source,uint32_t boot,const uint8_t *data,size_t size,int64_t now){
 unsigned count=data[6];if(count<1||count>3||size!=24+20*count||data[7]>1||r32(data+8)!=boot||!r32(data+12)){r->stats.rejected++;return;}
 tuple_t view[3];int own=-1;
 for(unsigned i=0;i<count;i++){
  const uint8_t *p=data+24+20*i;view[i]=(tuple_t){r64(p),r32(p+8),r32(p+12),p[16]};
  if(!view[i].id||!view[i].boot||(view[i].state&~7u)||p[17]||p[18]||p[19]||(i&&view[i-1].id>=view[i].id)){r->stats.rejected++;return;}
  if(view[i].id==source)own=(int)i;
 }
 if(own<0||view[own].boot!=boot||!view[own].generation){r->stats.rejected++;return;}
 peer_t *p=learn(r,view[own]);if(!p){invalidate(r,now);return;}
 if(!p->verified)invalidate(r,now);
 if(!p->verified||p->value.boot!=boot){challenge(r,p,boot);return;}
 uint32_t seq=r32(data+12);
 /* Freshness may be invalidated on RX loss; accepted sequence/generation
  * watermarks survive until a challenged new boot is confirmed. */
 if(p->sequence && !newer(seq,p->sequence))return;
 if(p->sequence && view[own].generation!=p->value.generation && !newer(view[own].generation,p->value.generation))return;
 if(p->sequence && view[own].generation==p->value.generation && view[own].state!=p->value.state){r->stats.rejected++;return;}
 p->pending_boot=p->challenge=0;
 p->value=view[own];p->advertised=true;p->sequence=seq;p->candidate=r64(data+16);p->ready=data[7]!=0;p->count=(uint8_t)count;p->seen=now;memcpy(p->view,view,count*sizeof(tuple_t));
 for(unsigned i=0;i<count;i++)if(view[i].id!=r->peers[0].value.id){peer_t *other=learn(r,view[i]);if(other&&!other->verified)challenge(r,other,view[i].boot);}
 topology(r,now);
}
static uint64_t next_hop(const echoy_radio_t *r,unsigned destination){return r->members[destination>r->rank?r->rank+1:r->rank-1];}
static pending_t *queued(pending_t *queue,unsigned n,const uint8_t *frame){
 for(unsigned i=0;i<n;i++)if(queue[i].used && queue[i].frame[5]==frame[5] && queue[i].sequence==r32(frame+16) && queue[i].frame[22]==frame[22] && queue[i].frame[23]==frame[23])return &queue[i];
 for(unsigned i=0;i<n;i++)if(!queue[i].used)return &queue[i];
 return NULL;
}
static void enqueue(pending_t *p,const uint8_t *frame,size_t n,uint64_t hop,int64_t now){
 if(p->used)return;
 memset(p,0,sizeof(*p));p->used=true;p->size=(uint8_t)n;memcpy(p->frame,frame,n);p->sequence=r32(frame+16);p->next_hop=hop;p->created=now;p->due=now;
}
/* 512 positions cover400 accepted sends at5ms within the source pending's
 * two-second lifetime, plus its bounded six-operation enqueue window. A relay
 * can outlive that pending; this is not a guarantee for every delayed relay.
 * An older unknown packet is rejected, never acknowledged as delivered. */
static int receipt(echoy_radio_t *r,unsigned origin,uint32_t seq){
 uint32_t latest=r->latest[origin];uint64_t *bits=r->received_bits[origin];
 if(!latest || newer(seq,latest)){
  uint32_t gap=seq-latest;if(!latest || gap>=RECEIPTS)memset(bits,0,sizeof(r->received_bits[origin]));
  else for(uint32_t i=1;i<=gap;i++){unsigned b=(latest+i)&(RECEIPTS-1);bits[b/64]&=~(UINT64_C(1)<<(b%64));}
  r->latest[origin]=seq;
 }else if((uint32_t)(latest-seq)>=RECEIPTS)return -1;
 unsigned b=seq&(RECEIPTS-1);uint64_t bit=UINT64_C(1)<<(b%64);bool seen=(bits[b/64]&bit)!=0;bits[b/64]|=bit;return seen?0:1;
}
static void read_data(echoy_radio_t *r,uint64_t source,uint32_t boot,const uint8_t *data,size_t size,int64_t now){
 if(!r->admitted||size<24||size>88||r64(data+8)!=r->graph||!r32(data+16)||!data[6]||data[6]>2||data[7]!=size-24||
    (data[5]==ACK && size!=24)){r->stats.rejected++;return;}
 unsigned origin=data[22],destination=data[23];int src=rank_of(r,source),pi=find(r,source);
 if(origin>=r->count||destination>=r->count||origin==destination||origin==r->rank||src<0||pi<0||r->peers[pi].value.boot!=boot||
    src!=(int)(origin<r->rank?r->rank-1:r->rank+1)){r->stats.rejected++;return;}
 if(destination!=r->rank){
  if(data[6]!=2||!((origin<r->rank && destination>r->rank)||(origin>r->rank && destination<r->rank))){r->stats.rejected++;return;}
  pending_t *p=queued(r->relay,RELAY,data);if(p){uint8_t frame[88];memcpy(frame,data,size);frame[6]=1;enqueue(p,frame,size,next_hop(r,destination),now);}else ++r->stats.queue_drops;return;
 }
 uint16_t service=(uint16_t)data[20]|(uint16_t)data[21]<<8;if(!service){r->stats.rejected++;return;}
 if(data[5]==ACK){
  for(unsigned i=0;i<OWN;i++){pending_t *p=&r->own[i];if(p->used && p->sequence==r32(data+16) && p->destination==r->members[origin] && p->service==service){uint64_t to=p->destination;measure_done(r,p,true,now);p->used=false;r->cb.delivery(r->cb.ctx,to,service,true,now);break;}}return;
 }
 int seen=receipt(r,origin,r32(data+16));if(seen<0)return;
 uint8_t ack[88];header(ack,ACK);ack[6]=2;w64(ack+8,r->graph);w32(ack+16,r32(data+16));ack[20]=data[20];ack[21]=data[21];ack[22]=(uint8_t)r->rank;ack[23]=(uint8_t)origin;
 pending_t *p=queued(r->acks,ACKS,ack);if(p)enqueue(p,ack,24,next_hop(r,origin),now);else ++r->stats.queue_drops;
 if(seen)r->cb.receive(r->cb.ctx,r->members[origin],data+24,size-24,now);
}
void echoy_radio_receive(echoy_radio_t *r,uint64_t source,uint32_t boot,const uint8_t *data,size_t size,int64_t now){
 if(!r||!source||source==r->peers[0].value.id||!boot||!data||size<8||size>88||now<0)return;
 ++r->stats.received;
 if(memcmp(data,"ECHN",4)||data[4]!=1){echoy_radio_observe_unknown(r,source,boot,now);++r->stats.rejected;return;}
 if(data[5]==ADVERTISE){read_advertisement(r,source,boot,data,size,now);return;}
 if(data[5]==PROBE || data[5]==PROOF){
  if(size!=20||data[6]||data[7]||r32(data+8)!=boot||r32(data+12)!=r->peers[0].value.boot||!r32(data+16)){++r->stats.rejected;return;}
  peer_t *p=learn(r,(tuple_t){.id=source,.boot=boot});if(!p){invalidate(r,now);return;}
  if(!p->verified)invalidate(r,now);
  if(data[5]==PROBE){p->reply_boot=boot;p->reply_challenge=r32(data+16);return;}
  if(p->pending_boot!=boot || p->challenge!=r32(data+16))return;
  if(!p->verified || p->value.boot!=boot){tuple_t value={.id=source,.boot=boot};memset(p,0,sizeof(*p));p->value=value;p->verified=true;invalidate(r,now);r->next_advertise=0;}
  p->pending_boot=p->challenge=0;return;
 }
 if(data[5]==DATA||data[5]==ACK)read_data(r,source,boot,data,size,now);else ++r->stats.rejected;
}
bool echoy_radio_send(echoy_radio_t *r,uint64_t to,uint16_t service,const uint8_t *data,size_t size,int64_t now){
 if(!r||!r->admitted||!service||!data||size>64||now<0)return false;
 int destination=rank_of(r,to);if(destination<0||(unsigned)destination==r->rank)return false;
 pending_t *p=NULL;for(unsigned i=0;i<OWN;i++)if(!r->own[i].used){p=&r->own[i];break;}if(!p)return false;
 uint8_t frame[88];header(frame,DATA);frame[6]=2;frame[7]=(uint8_t)size;w64(frame+8,r->graph);w32(frame+16,serial(&r->data_sequence));frame[20]=(uint8_t)service;frame[21]=(uint8_t)(service>>8);frame[22]=(uint8_t)r->rank;frame[23]=(uint8_t)destination;memcpy(frame+24,data,size);
 enqueue(p,frame,size+24,next_hop(r,(unsigned)destination),now);p->destination=to;p->service=service;measure_queued(r,p,data,size,now);
 unsigned pending=0;for(unsigned i=0;i<OWN;i++)pending+=r->own[i].used;if(pending>r->stats.queue_high_water)r->stats.queue_high_water=pending;return true;
}
static bool transmit(echoy_radio_t *r,uint64_t to,const uint8_t *data,size_t size,int64_t now){
 bool ok=r->cb.send(r->cb.ctx,to,data,size);r->next_wire=now+(ok?WIRE_INTERVAL:50000);
 if(ok)++r->stats.transmitted;else ++r->stats.send_failures;return ok;
}
void echoy_radio_tick(echoy_radio_t *r,int64_t now){
 if(!r||now<0)return;
 topology(r,now);
 for(unsigned i=0;i<OWN;i++)if(r->own[i].used && (now-r->own[i].created>=EXPIRE || (r->own[i].attempts>=5 && now>=r->own[i].due))){pending_t old=r->own[i];measure_done(r,&old,false,now);r->own[i].used=false;r->cb.delivery(r->cb.ctx,old.destination,old.service,false,now);}
 if(now<r->next_wire || (r->cb.available && !r->cb.available(r->cb.ctx)))return;
 for(unsigned i=1;i<r->known;i++)if(r->peers[i].reply_challenge){peer_t *p=&r->peers[i];uint8_t frame[88];header(frame,PROOF);w32(frame+8,r->peers[0].value.boot);w32(frame+12,p->reply_boot);w32(frame+16,p->reply_challenge);if(transmit(r,p->value.id,frame,20,now))p->reply_challenge=0;return;}
 if(now>=r->next_advertise){uint8_t frame[88];advertisement(r,frame);if(transmit(r,0,frame,24+20*r->known,now))r->next_advertise=now+INTERVAL;return;}
 for(unsigned i=1;i<r->known;i++){peer_t *p=&r->peers[i];if(p->pending_boot && now>=p->next_probe){uint8_t frame[88];header(frame,PROBE);w32(frame+8,r->peers[0].value.boot);w32(frame+12,p->pending_boot);w32(frame+16,p->challenge);transmit(r,p->value.id,frame,20,now);p->next_probe=now+RETRY;return;}}
 for(unsigned j=0;j<ACKS;j++){
  unsigned i=(r->ack_cursor+j)%ACKS;pending_t *p=&r->acks[i];if(!p->used)continue;
  if(now-p->created>=EXPIRE){p->used=false;continue;}
  if(transmit(r,p->next_hop,p->frame,p->size,now))p->used=false;
  r->ack_cursor=(i+1)%ACKS;return;
 }
 /* Rotate within each queue as well as between forwarded/local work. A
  * newly reused low slot must not repeatedly overtake an older higher slot. */
 for(unsigned pass=0;pass<2;pass++){
  bool forwarding=(r->kind_cursor++&1u)==0;
  if(forwarding){for(unsigned j=0;j<RELAY;j++){
   unsigned i=(r->relay_cursor+j)%RELAY;pending_t *p=&r->relay[i];if(!p->used)continue;
   if(now-p->created>=EXPIRE){p->used=false;continue;}
   if(transmit(r,p->next_hop,p->frame,p->size,now))p->used=false;
   r->relay_cursor=(i+1)%RELAY;return;
  }}
  else for(unsigned j=0;j<OWN;j++){unsigned i=(r->own_cursor+j)%OWN;pending_t *p=&r->own[i];if(p->used&&now>=p->due){if(p->attempts)r->stats.retries++;p->attempts++;p->due=now+RETRY;
   if(!p->stamped && r->cb.stamp)r->cb.stamp(r->cb.ctx,p->frame+24,p->size-24,now);
   bool ok=transmit(r,p->next_hop,p->frame,p->size,now);measure_sent(r,p,ok,now);if(ok)p->stamped=true;
   r->own_cursor=(i+1)%OWN;return;}}
 }
}
bool echoy_radio_info(const echoy_radio_t *r,echoy_radio_info_t *out){
 if(!r||!out)return false;
 *out=r->stats;out->graph=r->graph;out->known=r->known;out->count=r->count;out->rank=r->rank;
 out->contact_generation=r->peers[0].value.generation;out->contacts=r->peers[0].value.state&3u;out->ready=(r->peers[0].value.state&READY)!=0;out->admitted=r->admitted;out->scene_fresh=r->scene_fresh;out->pending=0;
 memcpy(out->members,r->members,sizeof(out->members));memcpy(out->peers,r->edges,sizeof(out->peers));for(unsigned i=0;i<OWN;i++)out->pending+=r->own[i].used;return true;
}
