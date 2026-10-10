#include "echoy_device_pcm.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
    const float input[]={0,.01f,-.01f,.2f,-.2f,.2925f,-.2925f,.5f,-.5f};
    int16_t output[9];size_t clipped=99;
    assert(echoy_device_pcm_convert(input,output,9,&clipped));
    for(unsigned i=0;i<7;++i)assert(output[i]==(int16_t)lrintf(input[i]*3*32767));
    assert(output[7]==32767&&output[8]==-32767&&clipped==2);
    float invalid[]={NAN,INFINITY,-INFINITY};
    for(unsigned i=0;i<3;i++)assert(!echoy_device_pcm_convert(&invalid[i],output,1,&clipped));
    assert(!echoy_device_pcm_convert(NULL,output,1,&clipped));
    assert(!echoy_device_pcm_convert(input,NULL,1,&clipped));
    assert(!echoy_device_pcm_convert(input,output,1,NULL));
    puts("PASS calibrated linear samples, headroom, clipping accounting and invalid PCM rejection");
}
