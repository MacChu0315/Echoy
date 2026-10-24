#include "echoy_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

echoy_audio_result_t reference_echoy_audio_prepare(const echoy_plan_t *,bool,echoy_audio_loops_t **);
bool reference_echoy_audio_voice_for_speaker(const echoy_plan_t *,echoy_audio_loops_t *);
void reference_echoy_audio_free_loops(echoy_audio_loops_t *);

static double old_ms[4],new_ms[4];
static unsigned cases;
static void compare(const echoy_plan_t *plan,bool original)
{
    echoy_audio_loops_t *old=NULL,*candidate=NULL;
    clock_t t=clock(); assert(reference_echoy_audio_prepare(plan,original,&old)==ECHOY_AUDIO_OK);
    old_ms[plan->role]+=1000.0*(clock()-t)/CLOCKS_PER_SEC;
    t=clock(); assert(echoy_audio_prepare(plan,original,&candidate)==ECHOY_AUDIO_OK);
    new_ms[plan->role]+=1000.0*(clock()-t)/CLOCKS_PER_SEC;
    for (unsigned speaker=0;speaker<2;speaker++) {
        if (speaker) {
            t=clock(); assert(reference_echoy_audio_voice_for_speaker(plan,old));
            old_ms[plan->role]+=1000.0*(clock()-t)/CLOCKS_PER_SEC;
            t=clock(); assert(echoy_audio_voice_for_speaker(plan,candidate));
            new_ms[plan->role]+=1000.0*(clock()-t)/CLOCKS_PER_SEC;
        }
        assert(old->frames==candidate->frames);
        assert(old->target_rms==candidate->target_rms && old->full_gain==candidate->full_gain);
        for (unsigned kind=0;kind<3;kind++) {
            if (memcmp(old->samples[kind],candidate->samples[kind],old->frames*sizeof(float))) {
                fprintf(stderr,"mismatch role=%u phrase=%u speaker=%u kind=%u case=%u\n",
                        plan->role,plan->phrase,speaker,kind,cases); assert(0);
            }
        }
    }
    reference_echoy_audio_free_loops(old); echoy_audio_free_loops(candidate); ++cases;
}

int main(void)
{
    const unsigned seeds[]={1,0x1085,0x2555,0xffff};
    for (unsigned role=0;role<4;role++) for(unsigned phrase=1;phrase<=3;phrase++)
        for(unsigned seed=0;seed<4;seed++) for(unsigned density=0;density<2;density++) {
            echoy_plan_t plan;
            assert(echoy_compose_mask(seeds[seed],role,phrase,density?1:0,&plan));
            compare(&plan,role==ECHOY_PULSE && phrase==1);
        }
    for(unsigned role=0;role<4;role++)printf("normal role=%u baseline_cpu_ms=%.3f candidate_cpu_ms=%.3f\n",role,old_ms[role],new_ms[role]);
    /* Public arbitrary plans retain all pitches, even outside the prebuilt
     * chord-table range; include overlapping and out-of-order note onsets. */
    for(unsigned note=0;note<128;note++) {
        echoy_plan_t plan={.role=ECHOY_HARMONY,.phrase=3,.source_count=1,.count=3,
            .source={{.step=3,.velocity=.42,.kind=ECHOY_HAT}},
            .events={{.step=3,.velocity=.31,.duration=.05,.kind=ECHOY_CHORD,.layer=ECHOY_ANCHOR},
                     {.step=2.5,.velocity=.22,.duration=.08,.kind=ECHOY_CHORD,.layer=ECHOY_ECHO},
                     {.step=3,.velocity=.61,.duration=.05,.kind=ECHOY_CHORD,.layer=ECHOY_ANCHOR}}};
        for(unsigned n=0;n<3;n++)plan.events[n].note=(int)note;
        compare(&plan,false);
    }
    printf("PASS %u plans: bit-identical SOURCE/FOREGROUND/FULL, gains and office voicing\n",cases);
    for(unsigned role=0;role<4;role++)printf("role=%u baseline_cpu_ms=%.3f candidate_cpu_ms=%.3f\n",role,old_ms[role],new_ms[role]);
}
