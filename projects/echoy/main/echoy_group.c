#include "echoy_group.h"
#include "echoy_link_protocol.h"
#include <limits.h>
#include <string.h>
#ifdef ECHOY_GROUP_TRACE
#include <stdio.h>
#define TRACE(...) fprintf(stderr,__VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

#define PERIOD INT64_C(2500000)
#define LIMIT INT64_C(30000000)
#define LEAD INT64_C(1000000)
#define GUARD INT64_C(200000)
#define RETRY INT64_C(750000)
#define VERSION 3u
#define HEADER 16u
#define CHUNK (64u-HEADER-10u)
enum { HELLO=1,PING,PONG,START,META,DATA,STATE,ARM,AUTHORIZE,ABORT,CLOCK,WANT };
enum { READY=1,ARMED=2,AUTHORIZED=4,COMMITTED=8,FAILED=16,LOOPING=32,BUSY=64,RECOVERY=128 };
enum { IDLE,PREPARING,ARMING,AUTHORIZING,ACTIVATING,DONE,BROKEN };
typedef struct {
    uint64_t peer,left,right,root;
    uint32_t session,boot,left_session,right_session,root_boot,sequence;
    uint8_t flags;
    bool known;
    int64_t root_offset,last_seen,next_hello,attached_at;
} edge_t;
typedef struct {
    uint64_t id,revision,input,output,owner_revision;
    uint32_t edits,start,sequence,any_sequence;
    uint8_t flags;
    int64_t seen,next_control,next_clock,last_packet;
} participant_t;
struct echoy_group {
    uint64_t self;
    uint32_t boot,identity,generation,serial,attempt,sequence,clock_sequence,clock_epoch,clock_commit;
    echoy_group_callbacks_t cb;
    edge_t edges[2]; /* left, right; only physical release clears these */
    participant_t members[3];
    unsigned count,rank,phase;
    bool mapped,music_valid,publish,offer_issued,have_view,output_valid,meta_acked;
    bool hello_dirty,recovery_requested;
    int64_t root_offset,origin,last_boundary,at,deadline,began,next_read,last_tick;
    int64_t next_state,next_clock,next_meta,next_send,retry_at,last_progress,next_want;
    int64_t ping_t1,next_ping,pong_t1,pong_t2;
    uint32_t ping_serial,ping_wait,pong_serial,transfer;
    bool pong_pending;
    uint32_t acknowledged[3];
    uint8_t playing,clock_votes,radio_lost;
    uint64_t input,output,local_revision;
    uint32_t output_crc;
    uint16_t output_size,queued,acked;
    uint8_t output_bytes[ECHOY_LINK_PLAN_MAX_BYTES];
    echoy_link_receiver_t rx;
    echoy_link_clock_t neighbor_clock;
    /* Persistent six-slot pool. Changing a peer or group never clears it. */
    echoy_link_pool_t pool;
    echoy_link_view_t view;
    echoy_link_update_t update;
    echoy_plan_t incoming;
};
static uint16_t r16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1]<<8; }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p) | (uint32_t)r16(p+2)<<16; }
static uint64_t r64(const uint8_t *p) { return (uint64_t)r32(p) | (uint64_t)r32(p+4)<<32; }
static int64_t rt(const uint8_t *p) { uint64_t t=r64(p); return t<INT64_MAX/4 ? (int64_t)t : -1; }
static void w16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void w32(uint8_t *p,uint32_t v) { w16(p,(uint16_t)v); w16(p+2,(uint16_t)(v>>16)); }
static void w64(uint8_t *p,uint64_t v) { w32(p,(uint32_t)v); w32(p+4,(uint32_t)(v>>32)); }
static bool newer(uint32_t a,uint32_t b) { return a!=b && (uint32_t)(a-b)<UINT32_C(0x80000000); }
static int member(const echoy_group_t *g,uint64_t id)
{ for (unsigned i=0;i<g->count;++i) if (g->members[i].id==id) return (int)i; return -1; }
static bool owner_current(const echoy_group_t *g)
{
    return g->have_view && g->view.session==g->identity &&
        g->view.connection_generation==g->generation;
}
static bool left_allowed(const echoy_group_t *g)
{ return !g->have_view || !g->view.busy || g->view.following; }
static bool admitted(const echoy_group_t *g,unsigned side)
{
    const edge_t *e=&g->edges[side];
    if (!e->peer || !e->known) return false;
    return side==0 ? left_allowed(g) && e->right==g->self && e->right_session==e->session :
        (e->flags&1u) && e->left==g->self && e->left_session==e->session;
}
static void status(echoy_group_t *g)
{
    echoy_link_update_t *u=&g->update;
    bool unknown=false;
    if (g->count==1 && !g->view.busy) for (unsigned side=0;side<2;++side) {
        const edge_t *e=&g->edges[side];
        if (e->peer && !e->known && g->last_tick-e->attached_at>=3000000) unknown=true;
    }
    u->attached=g->count>1 || unknown;
    u->following=u->attached && g->rank>0;
    /* Retained membership also holds retry edits during recovery. A local
     * worker must not publish a new bank before the group hears that edit. */
    u->hold_changes=g->count>1;
    u->recovering=g->phase==BROKEN || unknown;
    u->connection_generation=g->generation;
    u->session=g->identity;
    u->peer_looping=(g->playing & ~(1u<<g->rank))!=0;
    u->looping=u->peer_looping;
    u->clock_sync=u->attached && g->mapped && g->music_valid;
    u->clock_started=u->clock_sync;
    u->clock_origin_us=u->clock_sync ? g->origin+g->root_offset : 0;
    if (u->clock_origin_us<0) {
        u->clock_origin_us%=PERIOD; if (u->clock_origin_us<0) u->clock_origin_us+=PERIOD;
    }
    u->clock_start_ack=g->acknowledged[g->rank];
    u->clock_committed_transaction=g->clock_commit;
    u->align_boundary=true;
    g->publish=true;
}
static void command(echoy_group_t *g,echoy_link_command_t cmd,uint64_t revision)
{
    g->update.command=cmd; g->update.transaction=g->attempt;
    g->update.revision=revision;
    g->update.commit_at_us=g->at ? g->at+g->root_offset : 0;
    g->update.failed=g->phase==BROKEN;
    status(g);
}
static void reset_attempt(echoy_group_t *g,uint32_t attempt,int64_t deadline,int64_t now)
{
    g->attempt=attempt; g->phase=PREPARING; g->deadline=deadline;
    g->recovery_requested=false;
    g->offer_issued=g->output_valid=g->meta_acked=false;
    g->input=g->output=g->local_revision=0; g->output_size=g->queued=g->acked=0;
    g->rx.revision=0; g->rx.size=g->rx.covered=0;
    g->at=0; g->next_state=g->next_meta=g->retry_at=0;
    g->last_progress=now; ++g->transfer;
    for (unsigned i=0;i<g->count;++i) {
        g->members[i].flags=0; g->members[i].input=g->members[i].output=0;
        g->members[i].sequence=0;
        g->members[i].seen=g->members[i].next_control=g->members[i].next_clock=0;
    }
    g->update.valid=false; command(g,ECHOY_LINK_NONE,0);
}
static void broken(echoy_group_t *g)
{
    if (g->phase==BROKEN) return;
    TRACE("BROKEN self=%llu attempt=%u phase=%u at=%lld\n",(unsigned long long)g->self,g->attempt,g->phase,(long long)g->at);
    g->phase=BROKEN; g->next_state=0;
    if (g->rank) { g->neighbor_clock.valid=false; g->neighbor_clock.samples=0; g->next_ping=0; }
    command(g,ECHOY_LINK_ABORT,g->local_revision);
}
static void topology(echoy_group_t *g,int64_t now)
{
    uint64_t ids[3]={g->self,0,0}; uint32_t sessions[2]={0,0};
    uint64_t root=g->self; uint32_t boot=g->boot;
    unsigned count=1,rank=0;
    bool l=admitted(g,0),r=admitted(g,1);
    const edge_t *le=&g->edges[0],*re=&g->edges[1];
    if (l) {
        root=le->root; boot=le->root_boot;
        if (!root || !boot) return;
        if (le->flags&2u) {
            if (r || !le->left || le->left==g->self) { broken(g); return; }
            ids[0]=le->left; ids[1]=le->peer; ids[2]=g->self;
            sessions[0]=le->left_session; sessions[1]=le->session; count=3; rank=2;
        } else {
            ids[0]=le->peer; ids[1]=g->self; sessions[0]=le->session;
            count=2; rank=1;
            if (r) { ids[2]=re->peer; sessions[1]=re->session; count=3; }
        }
        if (ids[0]!=root) return; /* Wait for upstream's new admitted root. */
    } else if (r) {
        ids[1]=re->peer; sessions[0]=re->session; count=2;
        if (re->flags&4u) {
            if (!re->right || re->right==g->self) { broken(g); return; }
            ids[2]=re->right; sessions[1]=re->right_session; count=3;
        }
    }
    if (count==3 && (ids[0]==ids[1] || ids[0]==ids[2] || ids[1]==ids[2])) { broken(g); return; }
    uint8_t key[40]={0};
    for (unsigned i=0;i<3;++i) w64(key+8*i,ids[i]);
    w32(key+24,boot); w32(key+28,sessions[0]); w32(key+32,sessions[1]); w32(key+36,count);
    uint32_t identity=echoy_link_crc32(key,sizeof(key)); if (!identity) identity=1;
    bool changed=identity!=g->identity;
    if (changed) {
        TRACE("GROUP self=%llu ids=%llu,%llu,%llu identity=%u\n",(unsigned long long)g->self,(unsigned long long)ids[0],(unsigned long long)ids[1],(unsigned long long)ids[2],identity);
        g->identity=identity; if (!++g->generation) ++g->generation;
        memset(g->members,0,sizeof(g->members));
        for (unsigned i=0;i<count;++i) g->members[i].id=ids[i];
        g->count=count; g->rank=rank; g->phase=IDLE; g->attempt=0;
        g->offer_issued=g->output_valid=false;
        g->at=g->last_boundary=0; g->playing=g->clock_votes=g->radio_lost=0;
        g->clock_sequence=g->clock_epoch=g->clock_commit=0;
        memset(g->acknowledged,0,sizeof(g->acknowledged));
        g->acknowledged[rank]=rank ? 0 : g->view.local_start_serial;
        g->origin=g->view.clock_origin_us;
        g->music_valid=g->view.clock_started;
        g->update=(echoy_link_update_t){0};
        g->next_state=g->next_clock=0; g->hello_dirty=true;
    }
    g->mapped=!rank || (g->neighbor_clock.valid && (le->flags&8u));
    if (g->phase<ARMING || g->phase>=DONE || changed) {
        g->root_offset=rank ? le->root_offset+g->neighbor_clock.offset : 0;
    }
    if (changed) {
        g->origin=g->view.clock_origin_us-g->root_offset;
        status(g);
    }
    (void)now;
}
size_t echoy_group_size(void) { return sizeof(echoy_group_t); }
bool echoy_group_init(echoy_group_t *g,size_t size,uint64_t self,uint32_t boot,
                       const echoy_group_callbacks_t *cb)
{
    if (!g || size<sizeof(*g) || !self || !boot || !cb || !cb->read || !cb->receive || !cb->send) return false;
    memset(g,0,sizeof(*g)); g->self=self; g->boot=boot; g->cb=*cb;
    g->count=1; g->members[0].id=self; g->mapped=true;
    topology(g,0); return true;
}
void echoy_group_edge(echoy_group_t *g,bool left,uint64_t peer,uint32_t session,int64_t now)
{
    if (!g || now<0 || (peer && (!session || peer==g->self))) return;
    edge_t *e=&g->edges[left ? 0 : 1];
    if (e->peer==peer && e->session==session) return;
    if (peer && e->peer==peer && e->known) {
        /* The physical seam and admitted direction survive radio-session
         * renewal. Retire wire identity without inventing a physical release;
         * fresh HELLO/clock samples must still authorize the new group. */
        e->session=session;
        if (left) e->right_session=session;
        else e->left_session=session;
        e->sequence=0; e->next_hello=0;
    } else *e=(edge_t){.peer=peer,.session=peer ? session : 0};
    if (left) { memset(&g->neighbor_clock,0,sizeof(g->neighbor_clock)); g->ping_wait=0; g->next_ping=0; }
    e->attached_at=now;
    g->hello_dirty=true;
    topology(g,now);
    /* A physical release must reach the owner before another packet/commit.
     * Cached upstream advertisements cannot postpone release of this seam. */
    status(g);
}
void echoy_group_radio_lost(echoy_group_t *g,bool left,int64_t now)
{
    if (!g || now<0 || !admitted(g,left ? 0 : 1) || g->count<2) return;
    g->radio_lost|=(uint8_t)(left ? 1u : 2u); broken(g);
}
static bool emit(echoy_group_t *g,uint64_t to,uint8_t *p,size_t size,bool control,int64_t now)
{
    echoy_link_slot_t *s=echoy_link_pool_reserve(&g->pool,to,control,g->identity,now);
    if (!s) return false;
    s->transfer=g->transfer; s->kind=p[1];
    if (p[1]==DATA) { s->offset=r16(p+24); s->bytes=(uint16_t)(size-HEADER-10); }
    if (!g->cb.send(g->cb.ctx,to,s->service,p,size)) {
        /* Reserve this lane for the bounded backoff even when the SDK did not
         * queue an operation. Other destinations retain their own lanes. */
        s->kind=0; return false;
    }
    return true;
}
static void frame(echoy_group_t *g,uint8_t *p,uint8_t kind)
{
    memset(p,0,64); p[0]=VERSION; p[1]=kind;
    w32(p+4,g->identity); w32(p+8,g->attempt); w32(p+12,++g->sequence);
}
static uint8_t flags(const echoy_group_t *g)
{
    const echoy_link_view_t *v=&g->view; uint8_t f=0;
    if (v->busy) f|=BUSY;
    if (g->recovery_requested) f|=RECOVERY;
    if (v->looping) f|=LOOPING;
    if (g->phase==BROKEN) f|=FAILED;
    if (!owner_current(g)) return f;
    if (g->attempt && v->failed_transaction==g->attempt) f|=FAILED;
    if (!g->attempt || !g->offer_issued || v->offered_transaction!=g->attempt) return f;
    if (v->offered_revision==g->local_revision && v->offer_ready && !v->offer_failed && !v->deferred) f|=READY;
    if (v->armed_transaction==g->attempt && v->armed_revision==g->local_revision &&
        v->armed_at_us==g->at+g->root_offset) f|=ARMED;
    if (v->authorized_transaction==g->attempt) f|=AUTHORIZED;
    if (v->committed_transaction==g->attempt) f|=COMMITTED;
    if (v->offer_failed) f|=FAILED;
    return f;
}
static void clock_due(echoy_group_t *g)
{
    for (unsigned i=0;i<g->count;++i) g->members[i].next_clock=0;
}
static void start_request(echoy_group_t *g,unsigned rank,uint32_t serial,int64_t origin,bool looping)
{
    if (serial!=g->acknowledged[rank]) TRACE("START_REQ self=%llu rank=%u serial=%u ack=%u origin=%lld old=%lld votes=%u playing=%u boundary=%lld\n",(unsigned long long)g->self,rank,serial,g->acknowledged[rank],(long long)origin,(long long)g->origin,g->clock_votes,g->playing,(long long)g->last_boundary);
    if (serial!=g->acknowledged[rank] &&
        (!g->acknowledged[rank] || newer(serial,g->acknowledged[rank]))) {
        if (origin>=0) {
            bool others=(g->clock_votes & ~(1u<<rank))!=0;
            if (looping && origin>=g->last_boundary &&
                (!others || !g->music_valid || origin<g->origin)) {
                g->origin=origin; g->music_valid=true; ++g->clock_epoch;
            }
            g->acknowledged[rank]=serial; clock_due(g);
        }
    }
    /* An unmapped request carries local permission but cannot vote a
     * historical origin into the first-start election. ACK only valid starts. */
    if (looping && origin>=0 && serial==g->acknowledged[rank])
        g->clock_votes|=(uint8_t)(1u<<rank);
    if (!looping) g->clock_votes&=(uint8_t)~(1u<<rank);
    uint8_t old=g->playing;
    if (looping) g->playing|=(uint8_t)(1u<<rank);
    else g->playing&=(uint8_t)~(1u<<rank);
    if (old!=g->playing) clock_due(g);
}
static void encode_output(echoy_group_t *g)
{
    if (!g->offer_issued || !owner_current(g) || !g->view.offer_ready || g->output_valid ||
        g->view.offered_transaction!=g->attempt ||
        g->view.offered_revision!=g->local_revision) return;
    const echoy_plan_t *plan;
    if (g->view.proposal_pending) {
        if (!g->view.proposal_ready || g->view.proposal_revision!=g->view.requested_revision) return;
        plan=&g->view.proposal;
    } else plan=&g->view.current;
    size_t size=echoy_link_plan_encode(plan,g->output_bytes,sizeof(g->output_bytes));
    if (!size) { broken(g); return; }
    g->output_size=(uint16_t)size;
    g->output_crc=echoy_link_crc32(g->output_bytes,size);
    g->output=((uint64_t)g->output_crc<<32) | ((uint32_t)g->view.requested_revision+1u);
    if (!g->output) g->output=1;
    g->output_valid=true; g->next_meta=0;
}
static void begin_root(echoy_group_t *g,int64_t now,bool continuing)
{
    if (!continuing) g->began=now;
    if (!++g->serial) ++g->serial;
    TRACE("BEGIN root=%llu attempt=%u now=%lld\n",(unsigned long long)g->self,g->serial,(long long)now);
    reset_attempt(g,g->serial,g->began+LIMIT,now);
    for (unsigned i=0;i<g->count;++i) g->members[i].edits=(i==g->rank ? g->view.local_edit_serial : g->members[i].edits);
}
static void advance(echoy_group_t *g,int64_t now)
{
    if (g->count<2) return;
    if (g->phase==DONE) {
        if (!g->rank) {
            for (unsigned i=1;i<g->count;++i)
                if (g->members[i].last_packet && now-g->members[i].last_packet>=LIMIT) broken(g);
        } else if (g->members[0].seen && now-g->members[0].seen>=LIMIT) broken(g);
    }
    uint8_t own=flags(g);
    participant_t *self=&g->members[g->rank];
    if (g->rank==0) {
        bool changed=false;
        for (unsigned i=0;i<g->count;++i) {
            uint32_t edits=i ? g->members[i].edits : g->view.local_edit_serial;
            /* Snapshot the edit serial for the current attempt separately
             * from the latest participant state, below in ready revision. */
            if (g->members[i].revision!=edits) changed=true;
        }
        if (g->phase==IDLE || ((g->phase==DONE || g->phase==BROKEN || g->phase==PREPARING || g->phase==ARMING) && (changed || g->recovery_requested))) {
            bool continuing=g->phase==PREPARING || g->phase==ARMING;
            g->recovery_requested=false;
            begin_root(g,now,continuing);
            for (unsigned i=0;i<g->count;++i)
                g->members[i].revision=i ? g->members[i].edits : g->view.local_edit_serial;
        }
    }
    if (g->attempt && g->phase>=PREPARING && g->phase<DONE &&
        (now-g->root_offset>=g->deadline ||
         (g->at && now-g->root_offset>=g->at-GUARD && !(own&AUTHORIZED)))) broken(g);
    if (g->phase==BROKEN) {
        if (!owner_current(g) || (g->view.failed_transaction!=g->attempt &&
            g->view.committed_transaction!=g->attempt))
            command(g,ECHOY_LINK_ABORT,g->local_revision);
        else command(g,ECHOY_LINK_NONE,g->local_revision);
        return;
    }
    if (g->phase==PREPARING && !g->offer_issued && g->mapped && owner_current(g)) {
        if (g->rank==0) {
            if (g->view.proposal_pending && g->view.proposal_revision!=g->view.requested_revision) return;
            g->local_revision=g->view.proposal_pending ? g->view.proposal_revision : 0;
            command(g,ECHOY_LINK_BEGIN,g->local_revision);
            g->offer_issued=true;
        } else if (g->rx.size && g->rx.covered==g->rx.size && g->input) {
            g->update.valid=true; g->update.plan=g->incoming;
            command(g,ECHOY_LINK_OFFER,0); g->offer_issued=true;
        }
    }
    if (g->offer_issued && owner_current(g) && g->view.offered_transaction==g->attempt) {
        if (g->rank) g->local_revision=g->view.offered_revision;
        if (g->update.command==ECHOY_LINK_BEGIN || g->update.command==ECHOY_LINK_OFFER)
            command(g,ECHOY_LINK_NONE,g->local_revision);
    }
    encode_output(g);
    self->flags=flags(g); self->input=g->input; self->output=g->output;
    if (self->flags&FAILED) { broken(g); return; }
    if ((self->flags&COMMITTED) && g->at>g->last_boundary) {
        g->last_boundary=g->at; g->origin=g->at; g->music_valid=true; g->clock_commit=g->attempt;
        if (!g->rank) { ++g->clock_epoch; clock_due(g); }
        status(g);
    }
    if (!g->rank) {
        if (g->phase==PREPARING && now%500000==0) TRACE("READY self=%llu attempt=%u flags=%u,%u,%u output=%llu,%llu,%llu in=%llu,%llu,%llu cmd=%u rev=%llu\n",(unsigned long long)g->self,g->attempt,g->members[0].flags,g->members[1].flags,g->members[2].flags,(unsigned long long)g->members[0].output,(unsigned long long)g->members[1].output,(unsigned long long)g->members[2].output,(unsigned long long)g->members[0].input,(unsigned long long)g->members[1].input,(unsigned long long)g->members[2].input,g->update.command,(unsigned long long)g->view.requested_revision);
        bool all=true;
        for (unsigned i=0;i<g->count;++i) {
            if (g->members[i].flags&FAILED) { broken(g); return; }
            if (!(g->members[i].flags&READY) || !g->members[i].output ||
                (i && g->members[i].input!=g->members[i-1].output)) all=false;
        }
        if (g->phase==PREPARING && all && g->music_valid && g->mapped) {
            int64_t earliest=now+LEAD;
            g->at=g->origin;
            if (g->at<earliest) g->at+=((earliest-g->at+PERIOD-1)/PERIOD)*PERIOD;
            TRACE("ARM root=%llu attempt=%u at=%lld\n",(unsigned long long)g->self,g->attempt,(long long)g->at);
            g->phase=ARMING; command(g,ECHOY_LINK_ARM,g->local_revision);
            g->next_state=0;
        }
        unsigned bit=g->phase==ARMING ? ARMED : g->phase==AUTHORIZING ? AUTHORIZED : COMMITTED;
        all=true;
        for (unsigned i=0;i<g->count;++i) if (!(g->members[i].flags&bit)) all=false;
        if (g->phase==ARMING && all) {
            g->phase=AUTHORIZING; command(g,ECHOY_LINK_COMMIT,g->local_revision); g->next_state=0;
        } else if (g->phase==AUTHORIZING && all) {
            g->phase=ACTIVATING; command(g,ECHOY_LINK_NONE,g->local_revision); g->next_state=0;
        } else if (g->phase==ACTIVATING && all) {
            g->phase=DONE; command(g,ECHOY_LINK_NONE,g->local_revision);
        }
    } else {
        if (g->phase==ARMING && (self->flags&ARMED)) command(g,ECHOY_LINK_NONE,g->local_revision);
        if (g->phase==AUTHORIZING && (self->flags&AUTHORIZED)) {
            g->phase=ACTIVATING; command(g,ECHOY_LINK_NONE,g->local_revision);
        }
        if (g->phase==ACTIVATING && (self->flags&COMMITTED)) g->phase=DONE;
    }
}
void echoy_group_receive(echoy_group_t *g,uint64_t source,const uint8_t *p,size_t size,int64_t now)
{
    if (!g || !p || size<4 || size>64 || p[0]!=VERSION || p[3] || now<0) return;
    int side=source==g->edges[0].peer ? 0 : source==g->edges[1].peer ? 1 : -1;
    if (p[1]==HELLO) {
        if (size!=60 || side<0 || r32(p+56)!=g->edges[side].session || !r32(p+8) || (p[2]&~31u)) return;
        edge_t *e=&g->edges[side]; uint32_t sequence=r32(p+4);
        if (e->known && !newer(sequence,e->sequence)) return;
        if ((side==0 && (r64(p+24)!=g->self || r32(p+32)!=e->session)) ||
            (side==1 && (r64(p+12)!=g->self || r32(p+20)!=e->session))) return;
        int64_t offset=(int64_t)r64(p+48);
        if (offset<INT64_C(-1000000000000) || offset>INT64_C(1000000000000)) return;
        e->known=true; e->sequence=sequence; e->boot=r32(p+8); e->left=r64(p+12);
        e->right=r64(p+24); e->left_session=r32(p+20); e->right_session=r32(p+32);
        e->root=r64(p+36); e->root_boot=r32(p+44); e->root_offset=offset;
        e->flags=p[2];
        if (g->radio_lost & (1u<<(unsigned)side)) {
            g->radio_lost&=(uint8_t)~(1u<<(unsigned)side);
            if (!g->radio_lost && g->phase==BROKEN) g->recovery_requested=true;
        }
        if (g->phase==BROKEN && e->last_seen && now-e->last_seen>=3000000) g->recovery_requested=true;
        e->last_seen=now;
        topology(g,now); return;
    }
    if (p[1]==PING) {
        if (size!=20 || side!=1 || r32(p+4)!=g->edges[1].session || rt(p+12)<0) return;
        g->pong_serial=r32(p+8); g->pong_t1=rt(p+12); g->pong_t2=now; g->pong_pending=true;
        return;
    }
    if (p[1]==PONG) {
        if (size!=36 || side!=0 || r32(p+4)!=g->edges[0].session ||
            !g->ping_wait || r32(p+8)!=g->ping_wait || rt(p+12)!=g->ping_t1) return;
        g->ping_wait=0;
        bool frozen=g->phase>=ARMING && g->phase<DONE;
        if (echoy_link_clock_sample(&g->neighbor_clock,rt(p+12),rt(p+20),rt(p+28),now,frozen)) {
            g->next_ping=now+(g->neighbor_clock.samples<3 ? 150000 : 5000000);
            topology(g,now);
        }
        return;
    }
    if (size<HEADER || p[2] || r32(p+4)!=g->identity || g->count<2) return;
    int rank=member(g,source); if (rank<0 || (unsigned)rank==g->rank) return;
    uint32_t attempt=r32(p+8);
    if (p[1]==START) {
        if (size!=24 || rank!=0 || !g->rank || !attempt || rt(p+16)<0 ||
            (g->attempt && attempt!=g->attempt && !newer(attempt,g->attempt))) return;
        if (attempt!=g->attempt) reset_attempt(g,attempt,rt(p+16),now);
        g->members[0].seen=now; return;
    }
    if (p[1]==CLOCK) {
        if (size!=56 || rank!=0 || !g->rank || !g->mapped || rt(p+16)<0 || rt(p+24)<0 ||
            (p[48]&~((1u<<g->count)-1u)) || p[49] || p[50] || p[51] ||
            !newer(r32(p+12),g->clock_sequence) || rt(p+24)<g->last_boundary) return;
        if (g->phase==BROKEN && g->members[0].seen && now-g->members[0].seen>=3000000)
            g->recovery_requested=true;
        g->clock_sequence=r32(p+12); g->clock_epoch=r32(p+32); g->clock_commit=r32(p+52);
        g->origin=rt(p+16); g->music_valid=true; g->playing=p[48];
        for (unsigned i=0;i<g->count;++i) g->acknowledged[i]=r32(p+36+4*i);
        g->members[0].seen=now; status(g); return;
    }
    if (p[1]==STATE && size==64 && !g->rank) {
        participant_t *m=&g->members[rank];
        /* A node that missed START still reports its previous attempt. Its
         * fresh, group-fenced status after real silence proves route recovery;
         * it does not authorize the stale transaction or restore membership. */
        if (newer(r32(p+12),m->any_sequence)) {
            if (g->phase==BROKEN && (!m->last_packet || now-m->last_packet>=3000000))
                g->recovery_requested=true;
            m->any_sequence=r32(p+12); m->last_packet=now;
        }
    }
    if (!attempt || attempt!=g->attempt) return;
    if (p[1]==STATE) {
        if (size!=64 || g->rank || p[61] || p[62] || p[63]) return;
        participant_t *m=&g->members[rank];
        if (!newer(r32(p+12),m->sequence)) return;
        m->sequence=r32(p+12); m->owner_revision=r64(p+16); m->flags=p[60];
        if (g->phase==BROKEN && (m->flags&RECOVERY)) g->recovery_requested=true;
        m->input=r64(p+24); m->output=r64(p+32);
        m->edits=r32(p+40); m->start=r32(p+44);
        if (g->phase==BROKEN && m->seen && now-m->seen>=3000000) g->recovery_requested=true;
        m->seen=now;
        start_request(g,(unsigned)rank,m->start,rt(p+48),(m->flags&LOOPING)!=0);
        return;
    }
    if (p[1]==META) {
        if (size!=32 || !g->rank || (unsigned)rank!=g->rank-1 || g->phase!=PREPARING ||
            p[30] || p[31]) return;
        uint64_t token=r64(p+16);
        if (g->offer_issued && token!=g->input) return; /* A new root attempt must rebind offers. */
        bool fresh=g->rx.revision!=token || g->rx.crc!=r32(p+24) || g->rx.size!=r16(p+28);
        if (!echoy_link_receiver_begin(&g->rx,token,r32(p+24),r16(p+28))) return;
        g->input=token;
        if (fresh) g->last_progress=now;
        return;
    }
    if (p[1]==DATA) {
        if (size<=HEADER+10 || !g->rank || (unsigned)rank!=g->rank-1 ||
            g->phase!=PREPARING || r64(p+16)!=g->input) return;
        uint16_t old=g->rx.covered;
        echoy_link_fragment_result_t result=echoy_link_receiver_chunk(&g->rx,r64(p+16),
            r16(p+24),p+26,size-26);
        if (g->rx.covered!=old) g->last_progress=now;
        if (result==ECHOY_LINK_COMPLETE &&
            !echoy_link_plan_decode(g->rx.bytes,g->rx.size,&g->incoming)) {
            g->rx.covered=0; memset(g->rx.coverage,0,sizeof(g->rx.coverage));
        }
        return;
    }
    if (p[1]==WANT) {
        if (size!=24 || (unsigned)rank!=g->rank+1 || r64(p+16)!=g->output || g->phase!=PREPARING) return;
        ++g->transfer; g->queued=g->acked=0; g->meta_acked=false; g->next_meta=0; g->retry_at=now;
        return;
    }
    if (p[1]==ABORT) {
        if (size!=16 || rank!=0 || !g->rank) return;
        broken(g); return;
    }
    if (p[1]==ARM || p[1]==AUTHORIZE) {
        if (size!=32 || rank!=0 || !g->rank || !g->mapped || !g->offer_issued ||
            rt(p+16)<=now-g->root_offset || r64(p+24)!=g->local_revision) return;
        int64_t at=rt(p+16);
        if (p[1]==ARM) {
            if (g->phase!=PREPARING && (g->phase!=ARMING || g->at!=at)) return;
            if (!(flags(g)&READY)) return;
            g->at=at; g->phase=ARMING; command(g,ECHOY_LINK_ARM,g->local_revision);
        } else {
            if (g->at!=at || !(flags(g)&ARMED) ||
                (g->phase!=ARMING && g->phase!=AUTHORIZING)) return;
            g->phase=AUTHORIZING; command(g,ECHOY_LINK_COMMIT,g->local_revision);
        }
    }
}
void echoy_group_delivery(echoy_group_t *g,uint64_t destination,uint16_t service,
                           bool success,int64_t now)
{
    if (!g) return;
    echoy_link_slot_t done;
    if (!echoy_link_pool_take(&g->pool,destination,service,&done)) return;
    if (done.generation!=g->identity || done.transfer!=g->transfer) return;
    if (done.kind==META || done.kind==DATA) {
        if (!success) {
            ++g->transfer; g->queued=g->acked=0; g->meta_acked=false;
            g->next_meta=0; g->retry_at=now+RETRY;
        } else if (done.kind==META) g->meta_acked=true;
        else if (g->acked<=g->output_size && done.bytes<=g->output_size-g->acked)
            g->acked+=done.bytes;
    }
}
static void send_hello(echoy_group_t *g,unsigned side,int64_t now)
{
    edge_t *e=&g->edges[side];
    if (!e->peer || (!g->hello_dirty && now<e->next_hello)) return;
    uint8_t p[64]={VERSION,HELLO};
    p[2]=(left_allowed(g) ? 1u : 0u) | (admitted(g,0) ? 2u : 0u) |
        (admitted(g,1) ? 4u : 0u) | (g->mapped ? 8u : 0u) | (g->view.busy ? 16u : 0u);
    w32(p+4,++g->sequence); w32(p+8,g->boot);
    w64(p+12,g->edges[0].peer); w32(p+20,g->edges[0].session);
    w64(p+24,g->edges[1].peer); w32(p+32,g->edges[1].session);
    w64(p+36,g->members[0].id);
    w32(p+44,g->rank ? g->edges[0].root_boot : g->boot);
    w64(p+48,(uint64_t)g->root_offset); w32(p+56,e->session);
    if (emit(g,e->peer,p,60,true,now)) e->next_hello=now+500000;
}
static void send_clock_sample(echoy_group_t *g,int64_t now)
{
    uint8_t p[64]={VERSION};
    if (g->pong_pending && g->edges[1].peer) {
        p[1]=PONG; w32(p+4,g->edges[1].session); w32(p+8,g->pong_serial);
        w64(p+12,(uint64_t)g->pong_t1); w64(p+20,(uint64_t)g->pong_t2); w64(p+28,(uint64_t)now);
        if (emit(g,g->edges[1].peer,p,36,true,now)) g->pong_pending=false;
    }
    if (g->edges[0].peer && now>=g->next_ping &&
        (!g->ping_wait || now-g->ping_t1>=500000)) {
        p[1]=PING; w32(p+4,g->edges[0].session); w32(p+8,++g->ping_serial); w64(p+12,(uint64_t)now);
        if (emit(g,g->edges[0].peer,p,20,true,now)) {
            g->ping_wait=g->ping_serial; g->ping_t1=now;
            g->next_ping=now+(g->neighbor_clock.valid ? 5000000 : 150000);
        }
    }
}
static void send_state(echoy_group_t *g,int64_t now)
{
    if (!g->rank || now<g->next_state || !g->attempt) return;
    uint8_t p[64]; frame(g,p,STATE);
    w64(p+16,g->local_revision); w64(p+24,g->input); w64(p+32,g->output);
    w32(p+40,g->view.local_edit_serial); w32(p+44,g->view.local_start_serial);
    w64(p+48,g->mapped && owner_current(g) ? (uint64_t)(g->view.clock_origin_us-g->root_offset) : UINT64_MAX);
    w32(p+56,g->clock_epoch); p[60]=flags(g);
    if (emit(g,g->members[0].id,p,64,true,now)) g->next_state=now+100000;
}
static void send_root(echoy_group_t *g,int64_t now)
{
    if (g->rank || !g->attempt) return;
    for (unsigned i=1;i<g->count;++i) {
        uint8_t p[64]; size_t size=0; uint8_t kind=0;
        if (g->phase==BROKEN) { kind=ABORT; size=16; }
        else if (!g->members[i].seen) { kind=START; size=24; }
        else if (g->phase==ARMING && !(g->members[i].flags&ARMED)) { kind=ARM; size=32; }
        else if ((g->phase==AUTHORIZING || g->phase==ACTIVATING) &&
            !(g->members[i].flags&AUTHORIZED)) { kind=AUTHORIZE; size=32; }
        if (kind && now>=g->members[i].next_control) {
            frame(g,p,kind);
            if (kind==START) w64(p+16,(uint64_t)g->deadline);
            else if (kind==ARM || kind==AUTHORIZE) {
                w64(p+16,(uint64_t)g->at);
                /* READY binds each participant's actual owner revision. */
                w64(p+24,g->members[i].owner_revision);
            }
            if (emit(g,g->members[i].id,p,size,true,now)) {
                g->members[i].next_control=now+100000; continue;
            }
        }
        if (g->music_valid && now>=g->members[i].next_clock) {
            frame(g,p,CLOCK); w64(p+16,(uint64_t)g->origin); w64(p+24,(uint64_t)g->last_boundary);
            w32(p+32,g->clock_epoch);
            for (unsigned k=0;k<g->count;++k) w32(p+36+4*k,g->acknowledged[k]);
            p[48]=g->playing; w32(p+52,g->clock_commit);
            if (emit(g,g->members[i].id,p,56,true,now)) g->members[i].next_clock=now+500000;
        }
    }
    g->next_clock=1;
    /* Each recipient's reserved control slot gives backpressure; attempting
     * these again on the next worker tick does not allocate additional lanes. */
}
static void send_phrase(echoy_group_t *g,int64_t now)
{
    if (g->rank+1>=g->count || !g->output_valid || g->phase!=PREPARING || now<g->retry_at) return;
    uint64_t to=g->members[g->rank+1].id; uint8_t p[64];
    if (now>=g->next_meta) {
        frame(g,p,META); w64(p+16,g->output); w32(p+24,g->output_crc); w16(p+28,g->output_size);
        if (emit(g,to,p,32,true,now)) g->next_meta=now+500000;
    }
    if (!g->meta_acked || g->queued>=g->output_size) return;
    frame(g,p,DATA); w64(p+16,g->output); w16(p+24,g->queued);
    size_t size=g->output_size-g->queued; if (size>CHUNK) size=CHUNK;
    memcpy(p+26,g->output_bytes+g->queued,size);
    if (emit(g,to,p,26+size,false,now)) g->queued+=(uint16_t)size;
}
void echoy_group_tick(echoy_group_t *g,int64_t now)
{
    if (!g || now<0) return;
    g->last_tick=now;
    for (unsigned i=0;i<6;++i) if (g->pool.slots[i].active &&
        now-g->pool.slots[i].since>=(g->pool.slots[i].kind ? PERIOD : RETRY))
        echoy_group_delivery(g,g->pool.slots[i].destination,g->pool.slots[i].service,false,now);
    bool was_unavailable=g->have_view && !g->view.available;
    bool was_current=owner_current(g);
    if (now>=g->next_read && g->cb.read(g->cb.ctx,&g->view)) {
        g->have_view=true; g->next_read=now+10000;
        topology(g,now);
        if (g->phase==BROKEN && ((was_unavailable && g->view.available) ||
            (!was_current && owner_current(g)))) g->recovery_requested=true;
        if (!g->rank && owner_current(g)) {
            if (!g->music_valid && g->view.clock_started) {
                g->origin=g->view.clock_origin_us; g->music_valid=true;
            }
            start_request(g,0,g->view.local_start_serial,g->view.clock_origin_us,g->view.looping);
        }
    }
    advance(g,now); status(g);
    if (g->publish && g->cb.receive(g->cb.ctx,&g->update)) g->publish=false;
    send_clock_sample(g,now);
    send_hello(g,0,now); send_hello(g,1,now); g->hello_dirty=false;
    send_state(g,now); send_root(g,now); send_phrase(g,now);
    if (g->rank && g->rx.size && g->rx.covered<g->rx.size &&
        now-g->last_progress>=3000000 && now>=g->next_want) {
        uint8_t p[64]; frame(g,p,WANT); w64(p+16,g->input);
        if (emit(g,g->members[g->rank-1].id,p,24,true,now)) g->next_want=now+RETRY;
    }
}
bool echoy_group_info(const echoy_group_t *g,echoy_group_info_t *out)
{
    if (!g || !out) return false;
    *out=(echoy_group_info_t){.identity=g->identity,.generation=g->generation,
        .attempt=g->attempt,.count=g->count,.rank=g->rank,.phase=g->phase,
        .clock_mapped=g->mapped,.root_offset_us=g->root_offset,
        .origin_us=g->origin,.boundary_us=g->last_boundary,.deadline_us=g->deadline};
    for (unsigned i=0;i<g->count;++i) out->members[i]=g->members[i].id;
    for (unsigned i=0;i<6;++i) if (g->pool.slots[i].active) ++out->operations;
    return true;
}
