// SPDX-License-Identifier: Apache-2.0
#include "echoy_clock.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
int main(void) {
    echoy_clock_t c;echoy_clock_init(&c);int64_t mapped;
    assert(!echoy_clock_remote_to_local(&c,100,&mapped));
    for(int i=0;i<=80;i++) {
        int64_t t=1000000+(int64_t)i*250000, offset=5000+(t-1000000)/10000;
        assert(echoy_clock_observe(&c,t,t+500+offset,t+600+offset,t+1100));
        if(i<7)assert(!c.stable);
    }
    assert(c.stable);assert(fabs(c.rate-0.0001)<0.000001);
    assert(echoy_clock_remote_to_local(&c,22007100,&mapped));assert(llabs(mapped-22000000)<2);
    assert(!echoy_clock_observe(&c,1,2,3,4));
    assert(!echoy_clock_observe(&c,23000000,23000500,23000400,23001000));
    assert(!echoy_clock_observe(&c,23000000,23000500,23000600,24000000));
    assert(echoy_clock_remote_to_local(&c,0,&mapped));assert(mapped<0);
    echoy_clock_init(&c);
    for(int i=0;i<=80;i++) {
        int64_t t=1000000+(int64_t)i*250000, offset=(i&1)?10000:0;
        assert(echoy_clock_observe(&c,t,t+500+offset,t+600+offset,t+1100));
    }
    assert(!c.stable); /* Low RTT alone is insufficient when offset is unstable. */
    return 0;
}
