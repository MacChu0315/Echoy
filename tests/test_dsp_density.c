// SPDX-License-Identifier: Apache-2.0
#include "echoy_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double clock_seconds(void){struct timespec ts;assert(timespec_get(&ts,TIME_UTC)==TIME_UTC);return ts.tv_sec+ts.tv_nsec*1e-9;}
int main(void){
    echoy_audio_t *a=malloc(echoy_audio_size());assert(echoy_audio_init(a,echoy_audio_size(),0));
    float block[512];double peak=0,max_block=0,total_time=0;uint64_t expected=0;unsigned blocks=0;
    /* Three devices, all 12 phrases at maximum activity; sustained tones from
     * preceding bars and per-device feedback overlap the next committed plan. */
    for(int bar=0;bar<48;bar++){
        echoy_bar_plan_t plan={.count=3};echoy_motif_t left={0};
        for(int d=0;d<3;d++){
            echoy_plan_entry_t *p=&plan.devices[d];p->device=d;p->role=(echoy_role_t)((bar/3+d)%4);
            p->phrase=bar%3+1;p->bar=bar;p->activity=1;p->volume=1;p->motif_available=true;p->transport=ECHOY_PLAYING;
            echoy_music_input_t input={p->role,p->phrase,bar,0,1,96,d?&left:NULL};assert(echoy_music_compose(&input,&p->motif));p->events=p->motif;left=p->motif;expected+=p->events.count;
        }
        assert(echoy_audio_plan(a,&plan,7));
        int64_t end=(bar+1)*120000;
        while(echoy_audio_stats(a).cursor<end){size_t n=(size_t)(end-echoy_audio_stats(a).cursor);if(n>256)n=256;
            double begin=clock_seconds();echoy_audio_render(a,block,n);double took=clock_seconds()-begin;
            total_time+=took;if(took>max_block)max_block=took;blocks++;
            for(size_t i=0;i<n*2;i++){assert(isfinite(block[i]));if(fabs(block[i])>peak)peak=fabs(block[i]);}
        }
    }
    echoy_audio_stats_t stats=echoy_audio_stats(a);
    assert(stats.started==expected&&stats.capacity_errors==0&&stats.missed==0&&stats.clipped_samples==0);
    printf("PASS DSP density: 120s, %llu events, peak voices %u/64, PCM peak %.6f, memory %zu bytes, mean block %.3f ms, max block %.3f ms (PC wall clock only)\n",(unsigned long long)stats.started,stats.peak_voices,peak,echoy_audio_size(),1000*total_time/blocks,1000*max_block);
    free(a);return 0;
}
