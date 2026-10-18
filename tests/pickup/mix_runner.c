#include "echoy_audio.h"
#include "echoy_pickup.h"
#include "echoy_device_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *f=fopen(argv[1],"wb");assert(f);
    float *raw=malloc(60000*sizeof(float)),*layer=malloc(60000*sizeof(float));assert(raw&&layer);
    for(unsigned r=0;r<4;r++)for(unsigned p=1;p<=3;p++)for(unsigned profile=0;profile<2;profile++) {
        echoy_plan_t plan;assert(echoy_compose_mask(0x50,r,p,.7,&plan));
        echoy_audio_loops_t *loops;assert(echoy_audio_prepare(&plan,r==0&&p==1,&loops)==ECHOY_AUDIO_OK);
        if(profile)assert(echoy_audio_voice_for_speaker(&plan,loops));
        echoy_pickup_t intro;assert(echoy_pickup_apply(&plan,&intro));
        assert(echoy_audio_render_pickup(intro.notes,intro.count,r,profile,raw)==ECHOY_AUDIO_OK);
        assert(echoy_pickup_mix(&intro,loops->samples[2],raw,loops->full_gain,layer));
        printf("{\"head\":%.17g,\"start\":%.17g,\"gain\":%.17g,\"boundary\":%zu,\"end\":%zu,\"fade\":%zu}\n",
          plan.source[0].step,intro.notes[0].step,loops->full_gain,intro.boundary,intro.end,intro.fade);
        assert(fwrite(loops->samples[2],sizeof(float),60000,f)==60000);
        assert(fwrite(raw,sizeof(float),60000,f)==60000);
        for(unsigned i=0;i<intro.end;i++)layer[i]+=loops->samples[2][i];
        for(size_t i=intro.end;i<60000;i++)layer[i]=loops->samples[2][i];
        assert(fwrite(layer,sizeof(float),60000,f)==60000);
        echoy_audio_free_loops(loops);
    }
    fclose(f);free(raw);free(layer);
}
