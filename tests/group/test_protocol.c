#include "echoy_link_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    echoy_link_receiver_t rx={0};
    const uint8_t data[]={1,2,3,4,5,6};
    assert(echoy_link_receiver_begin(&rx,17,echoy_link_crc32(data,6),6));
    assert(echoy_link_receiver_chunk(&rx,17,3,data+3,3)==ECHOY_LINK_INCOMPLETE);
    assert(echoy_link_receiver_chunk(&rx,17,3,data+3,3)==ECHOY_LINK_INCOMPLETE);
    assert(rx.covered==3);
    assert(echoy_link_receiver_chunk(&rx,16,0,data,3)==ECHOY_LINK_REJECTED);
    assert(echoy_link_receiver_chunk(&rx,17,0,data,3)==ECHOY_LINK_COMPLETE);
    assert(!memcmp(rx.bytes,data,6));
    assert(!echoy_link_receiver_begin(&rx,18,0,8193));
    assert(echoy_link_receiver_begin(&rx,18,echoy_link_crc32(data,6),6));
    assert(echoy_link_receiver_chunk(&rx,18,0,data,3)==ECHOY_LINK_INCOMPLETE);
    const uint8_t conflict[]={1,9,3};
    assert(echoy_link_receiver_chunk(&rx,18,0,conflict,3)==ECHOY_LINK_REJECTED);
    assert(rx.covered==0);
    assert(echoy_link_receiver_chunk(&rx,18,0,data,6)==ECHOY_LINK_COMPLETE);
    assert(echoy_link_receiver_begin(&rx,19,0,6));
    assert(echoy_link_receiver_chunk(&rx,19,0,data,6)==ECHOY_LINK_REJECTED);
    echoy_link_clock_t clock={0};
    /* Remote timeline is +200000; network each way 1000, processing 500. */
    for (unsigned i=0;i<3;++i)
        assert(echoy_link_clock_sample(&clock,1000000,1201000,1201500,1002500,false));
    assert(clock.valid && clock.offset==-200000 && clock.best_rtt==2000);
    assert(!echoy_link_clock_sample(&clock,2000000,2201000,2201500,2300000,false));
    assert(clock.offset==-200000);
    assert(echoy_link_clock_sample(&clock,3000000,3300100,3300600,3000700,true));
    assert(clock.offset==-200000); /* Armed mapping is immutable. */
    /* Equal RTT in a later window must track oscillator drift, retaining the
     * previous mapping until three fresh samples have arrived. */
    for (unsigned i=0;i<3;++i) {
        int64_t t=6000000+(int64_t)i*150000;
        assert(echoy_link_clock_sample(&clock,t,t+301000,t+301500,t+2500,false));
        if (i<2) assert(clock.offset==-200000);
    }
    assert(clock.offset==-300000);
    echoy_link_pool_t pool={0};
    uint16_t services[6];
    for (unsigned peer=0;peer<2;++peer) for (unsigned lane=0;lane<3;++lane) {
        echoy_link_slot_t *slot=echoy_link_pool_reserve(&pool,100+peer,lane==0,11,1000);
        assert(slot); services[peer*3+lane]=slot->service;
    }
    assert(!echoy_link_pool_reserve(&pool,100,false,11,1001));
    assert(!echoy_link_pool_reserve(&pool,102,true,12,1001));
    assert(!echoy_link_pool_reserve(&pool,102,false,12,1001));
    /* A new neighbor cannot bypass the six operations held by old peers. */
    echoy_link_slot_t done;
    assert(!echoy_link_pool_take(&pool,102,services[0],&done));
    assert(echoy_link_pool_take(&pool,100,services[0],&done));
    assert(done.generation==11 && done.destination==100);
    assert(echoy_link_pool_reserve(&pool,102,true,12,1001));
    assert(!echoy_link_pool_reserve(&pool,102,false,12,1001));
    assert(echoy_link_pool_take(&pool,100,services[1],&done));
    assert(echoy_link_pool_reserve(&pool,102,false,12,1001));
    assert(!echoy_link_pool_reserve(&pool,102,true,12,1001));
    puts("PASS portable fragments, CRC/conflicts, frozen clock and bounded control/data lanes");
}
