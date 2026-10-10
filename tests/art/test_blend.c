#include "echoy_art_blend.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void check(unsigned base, unsigned ink, float alpha)
{
    if (alpha < 0 || alpha > 1) return;
    unsigned old = (unsigned)(base + ink * (double)alpha - base * alpha);
    unsigned result = echoy_art_blend_channel(base,ink,alpha);
    if (old != result) fprintf(stderr,"blend %u %u %.9g old=%u new=%u\n",base,ink,alpha,old,result);
    assert(old == result);
}
int main(void)
{
    unsigned checks=0;
    for(unsigned base=0;base<256;base++)for(unsigned ink=0;ink<256;ink++){
        for(unsigned j=0;j<=32;j++){check(base,ink,j/32.f);checks++;}
        if(base==ink)continue;
        for(unsigned target=0;target<256;target+=7){
            float at=((float)target-base)/((float)ink-base);
            check(base,ink,at);check(base,ink,nextafterf(at,0));check(base,ink,nextafterf(at,1));checks+=3;
        }
    }
    printf("PASS %u channel blend probes match original double truncation\n",checks);
}
