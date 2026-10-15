#include "echoy_link_protocol.h"
#include <string.h>

bool echoy_link_receiver_begin(echoy_link_receiver_t *rx, uint64_t revision,
                               uint32_t crc, size_t size)
{
    if (!rx || !revision || !size || size>ECHOY_LINK_PLAN_MAX_BYTES) return false;
    if (rx->revision==revision && rx->crc==crc && rx->size==size) return true;
    rx->revision=revision; rx->crc=crc; rx->size=(uint16_t)size;
    rx->covered=0; memset(rx->coverage,0,sizeof(rx->coverage));
    return true;
}
echoy_link_fragment_result_t echoy_link_receiver_chunk(echoy_link_receiver_t *rx,
    uint64_t revision, size_t offset, const uint8_t *data, size_t size)
{
    if (!rx || !data || !size || rx->revision!=revision || !rx->size ||
        offset>rx->size || size>rx->size-offset) return ECHOY_LINK_REJECTED;
    for (size_t i=0;i<size;++i) {
        size_t p=offset+i;
        if ((rx->coverage[p/8] & (1u<<(p%8))) && rx->bytes[p]!=data[i]) {
            rx->covered=0; memset(rx->coverage,0,sizeof(rx->coverage));
            return ECHOY_LINK_REJECTED;
        }
    }
    for (size_t i=0;i<size;++i) {
        size_t p=offset+i; uint8_t bit=(uint8_t)(1u<<(p%8));
        if (!(rx->coverage[p/8] & bit)) {
            rx->bytes[p]=data[i]; rx->coverage[p/8]|=bit; ++rx->covered;
        }
    }
    if (rx->covered!=rx->size) return ECHOY_LINK_INCOMPLETE;
    if (echoy_link_crc32(rx->bytes,rx->size)==rx->crc) return ECHOY_LINK_COMPLETE;
    rx->covered=0; memset(rx->coverage,0,sizeof(rx->coverage));
    return ECHOY_LINK_REJECTED;
}
bool echoy_link_clock_sample(echoy_link_clock_t *c, int64_t t1, int64_t t2,
                             int64_t t3, int64_t t4, bool frozen)
{
    if (!c || t1<0 || t2<0 || t3<t2 || t4<t1 || t4-t1>250000) return false;
    int64_t rtt=(t4-t1)-(t3-t2);
    if (rtt<0 || rtt>80000) return false;
    if (!c->samples || t4-c->window_start>=5000000) {
        c->samples=0; c->window_start=t4;
    }
    int64_t offset=((t1-t2)+(t4-t3))/2;
    if (!c->samples || rtt<c->best_rtt) { c->best_rtt=rtt; c->best_offset=offset; }
    if (c->samples<3) ++c->samples;
    if (c->samples>=3) {
        c->valid=true;
        if (!frozen) c->offset=c->best_offset;
    }
    return true;
}
echoy_link_slot_t *echoy_link_pool_reserve(echoy_link_pool_t *l,uint64_t destination,
    bool control,uint32_t generation,int64_t now)
{
    if (!l || !destination || !generation || now<0) return NULL;
    unsigned first=control ? 0 : 2,last=control ? 2 : 6,occupied=0;
    for (unsigned i=first;i<last;++i)
        if (l->slots[i].active && l->slots[i].destination==destination) ++occupied;
    if (occupied>=(control ? 1u : 2u)) return NULL;
    for (unsigned i=first;i<last;++i) {
        echoy_link_slot_t *s=&l->slots[i]; if (s->active) continue;
        uint16_t serial; bool collision;
        do {
            serial=(uint16_t)(0xe000 | (++l->serial & 0x0fff)); collision=false;
            for (unsigned j=0;j<6;++j)
                if (l->slots[j].active && l->slots[j].service==serial) collision=true;
        } while (collision);
        *s=(echoy_link_slot_t){.active=true,.destination=destination,.service=serial,
            .generation=generation,.since=now};
        return s;
    }
    return NULL;
}
bool echoy_link_pool_take(echoy_link_pool_t *l,uint64_t destination,
    uint16_t service,echoy_link_slot_t *completed)
{
    if (!l || !completed) return false;
    for (unsigned i=0;i<6;++i) {
        echoy_link_slot_t *s=&l->slots[i];
        if (s->active && s->service==service && s->destination==destination) {
            *completed=*s; s->active=false; return true;
        }
    }
    return false;
}
