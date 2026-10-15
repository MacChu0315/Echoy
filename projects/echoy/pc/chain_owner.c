#include "chain_owner.h"
#include <stdlib.h>
#include <string.h>
#define PACKETS 64
struct chain_node {
    echoy_chain_t *chain;
    int id;
    echoy_band_t *band;
    echoy_group_t *group;
    echoy_link_update_t desired;
    bool queued;
    double energy;
};
struct echoy_chain {
    struct chain_node nodes[3];
    echoy_arrange_edge_t edges[2];
    size_t edge_count;
    uint32_t sessions[3][3],serial;
    int64_t now;
    struct {
        bool active;
        uint8_t from,to,bytes;
        uint16_t service;
        int64_t at;
        uint8_t payload[64];
    } packets[PACKETS];
};
int64_t echoy_chain_offset(int id)
{
    static const int64_t offsets[]={0,70000000,140000000};
    return id>=0 && id<3 ? offsets[id] : 0;
}
static unsigned hops(const echoy_chain_t *c,int from,int to)
{
    bool seen[3]={false,false,false}; seen[from]=true;
    for (unsigned pass=1;pass<=2;++pass) {
        bool next[3]; memcpy(next,seen,sizeof(next));
        for (size_t i=0;i<c->edge_count;++i) {
            int a=c->edges[i].left,b=c->edges[i].right;
            if (seen[a]) next[b]=true;
            if (seen[b]) next[a]=true;
        }
        memcpy(seen,next,sizeof(seen)); if (seen[to]) return pass;
    }
    return 0;
}
static bool read_owner(void *ctx,echoy_link_view_t *out)
{ return echoy_band_link_read(((struct chain_node *)ctx)->band,out); }
static bool receive_owner(void *ctx,const echoy_link_update_t *u)
{ struct chain_node *n=ctx; n->desired=*u; n->queued=true; return true; }
static bool send_packet(void *ctx,uint64_t to,uint16_t service,const uint8_t *bytes,size_t size)
{
    struct chain_node *n=ctx; echoy_chain_t *c=n->chain;
    if (to<1 || to>3 || to==(uint64_t)n->id+1 || size>64) return false;
    unsigned distance=hops(c,n->id,(int)to-1); if (!distance) return false;
    for (unsigned i=0;i<PACKETS;++i) if (!c->packets[i].active) {
        c->packets[i].active=true; c->packets[i].from=(uint8_t)n->id;
        c->packets[i].to=(uint8_t)(to-1); c->packets[i].service=service;
        c->packets[i].bytes=(uint8_t)size; memcpy(c->packets[i].payload,bytes,size);
        c->packets[i].at=c->now+(int64_t)distance*1000; return true;
    }
    return false;
}
echoy_chain_t *echoy_chain_create(void)
{
    echoy_chain_t *c=calloc(1,sizeof(*c)); if (!c) return NULL;
    c->serial=1000;
    for (int i=0;i<3;++i) {
        struct chain_node *n=&c->nodes[i]; n->chain=c; n->id=i;
        n->band=echoy_band_create(); n->group=calloc(1,echoy_group_size());
        echoy_group_callbacks_t cb={read_owner,receive_owner,send_packet,n};
        if (!n->band || !n->group || !echoy_group_init(n->group,echoy_group_size(),(uint64_t)i+1,100u+(unsigned)i,&cb)) {
            echoy_chain_delete(c); return NULL;
        }
    }
    return c;
}
void echoy_chain_delete(echoy_chain_t *c)
{
    if (!c) return;
    for (int i=0;i<3;++i) { echoy_band_delete(c->nodes[i].band); free(c->nodes[i].group); }
    free(c);
}
echoy_band_t *echoy_chain_band(echoy_chain_t *c,int id)
{ return c && id>=0 && id<3 ? c->nodes[id].band : NULL; }
bool echoy_chain_info(echoy_chain_t *c,int id,echoy_group_info_t *out)
{ return c && id>=0 && id<3 && echoy_group_info(c->nodes[id].group,out); }
bool echoy_chain_read(echoy_chain_t *c,int id,echoy_control_view_t *out)
{
    if (!c || id<0 || id>=3 || !echoy_band_read(c->nodes[id].band,ECHOY_TILE_A,out)) return false;
    echoy_group_info_t info; echoy_group_info(c->nodes[id].group,&info);
    out->tile=id;
    if (out->source!=ECHOY_TILE_NONE && info.rank) out->source=(int)info.members[info.rank-1]-1;
    out->clock=(int)info.members[0]-1;
    return true;
}
size_t echoy_chain_get_edges(echoy_chain_t *c,echoy_arrange_edge_t out[2])
{
    if (!c || !out) return 0;
    memcpy(out,c->edges,c->edge_count*sizeof(*out)); return c->edge_count;
}
bool echoy_chain_edges(echoy_chain_t *c,const echoy_arrange_edge_t *edges,size_t count,int64_t now)
{
    if (!c || now<0 || count>2 || (count && !edges)) return false;
    int left[3]={-1,-1,-1},right[3]={-1,-1,-1};
    for (size_t i=0;i<count;++i) {
        int a=edges[i].left,b=edges[i].right;
        if (a<0 || a>2 || b<0 || b>2 || a==b || right[a]>=0 || left[b]>=0) return false;
        right[a]=b; left[b]=a;
    }
    for (int i=0;i<3;++i) {
        int at=i;
        for (unsigned j=0;j<3 && at>=0;++j) { at=right[at]; if (at==i) return false; }
    }
    uint32_t next[3][3]={{0}};
    for (size_t i=0;i<count;++i) {
        int a=edges[i].left,b=edges[i].right;
        next[a][b]=c->sessions[a][b] ? c->sessions[a][b] : ++c->serial;
    }
    c->edge_count=count;
    if (count) memcpy(c->edges,edges,count*sizeof(*edges));
    memcpy(c->sessions,next,sizeof(next)); c->now=now;
    for (int i=0;i<3;++i) {
        echoy_group_edge(c->nodes[i].group,true,left[i]>=0 ? (uint64_t)left[i]+1 : 0,
            left[i]>=0 ? next[left[i]][i] : 0,now+echoy_chain_offset(i));
        echoy_group_edge(c->nodes[i].group,false,right[i]>=0 ? (uint64_t)right[i]+1 : 0,
            right[i]>=0 ? next[i][right[i]] : 0,now+echoy_chain_offset(i));
    }
    return true;
}
bool echoy_chain_action(echoy_chain_t *c,int id,echoy_action_t action,double value,int64_t now)
{
    if (!c || id<0 || id>=3) return false;
    if (action==ECHOY_ACT_PRESENT && value==0) {
        echoy_arrange_edge_t retained[2]; size_t count=0;
        for (size_t i=0;i<c->edge_count;++i)
            if (c->edges[i].left!=id && c->edges[i].right!=id) retained[count++]=c->edges[i];
        if (!echoy_chain_edges(c,retained,count,now)) return false;
    }
    return echoy_band_action(c->nodes[id].band,ECHOY_TILE_A,action,value,now+echoy_chain_offset(id));
}
void echoy_chain_tick(echoy_chain_t *c,int64_t now)
{
    if (!c) return;
    c->now=now;
    for (unsigned i=0;i<PACKETS;++i) if (c->packets[i].active && now>=c->packets[i].at) {
        unsigned a=c->packets[i].from,b=c->packets[i].to;
        bool connected=hops(c,(int)a,(int)b)!=0;
        if (connected) echoy_group_receive(c->nodes[b].group,(uint64_t)a+1,c->packets[i].payload,
            c->packets[i].bytes,now+echoy_chain_offset((int)b));
        echoy_group_delivery(c->nodes[a].group,(uint64_t)b+1,c->packets[i].service,connected,
            now+echoy_chain_offset((int)a));
        c->packets[i].active=false;
    }
    for (int i=0;i<3;++i) {
        struct chain_node *n=&c->nodes[i]; int64_t local=now+echoy_chain_offset(i);
        if (n->queued) { n->queued=false; echoy_band_link_update(n->band,&n->desired,local); }
        echoy_band_tick(n->band,local); echoy_group_tick(n->group,local);
    }
}
void echoy_chain_render(echoy_chain_t *c,int64_t now,float *out,size_t frames)
{
    if (!c || !out) return;
    memset(out,0,frames*sizeof(*out));
    for (int i=0;i<3;++i) {
        struct chain_node *n=&c->nodes[i]; n->energy=0;
        for (size_t pos=0;pos<frames;pos+=240) {
            float pcm[240]; size_t count=frames-pos; if (count>240) count=240;
            int64_t at=now+echoy_chain_offset(i)+(int64_t)(pos*1000000/ECHOY_AUDIO_SAMPLE_RATE);
            echoy_band_tick(n->band,at); echoy_band_render(n->band,at,pcm,count);
            for (size_t k=0;k<count;++k) { out[pos+k]+=pcm[k]; n->energy+=pcm[k]*pcm[k]; }
        }
    }
}
double echoy_chain_energy(echoy_chain_t *c,int id)
{ return c && id>=0 && id<3 ? c->nodes[id].energy : 0; }
