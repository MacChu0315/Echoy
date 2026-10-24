#include "echoy_audio.h"
#include <assert.h>
#include <stdio.h>

static unsigned pitches;
float __real_exp2f(float);
float __wrap_exp2f(float value) { ++pitches; return __real_exp2f(value); }

int main(void)
{
    for (unsigned phrase=1;phrase<=3;phrase++) {
        echoy_plan_t plan;
        assert(echoy_compose_mask(0x2555,ECHOY_VOICE,phrase,.75,&plan));
        echoy_audio_loops_t *loops=NULL; pitches=0;
        assert(echoy_audio_prepare(&plan,false,&loops)==ECHOY_AUDIO_OK);
        /* Voice and source drums resolve pitch once per rendered note. An
         * anchor belongs to both foreground and full but must synthesize once. */
        printf("phrase=%u notes=%zu pitch_resolutions=%u\n",phrase,plan.source_count+plan.count,pitches);
        fflush(stdout);
        assert(pitches==plan.source_count+plan.count);
        echoy_audio_free_loops(loops);
    }
    puts("PASS foreground/full reuse each anchor synthesis without an extra workspace");
}
