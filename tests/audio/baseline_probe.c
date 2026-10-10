/* Shared-library entry point for comparing complete loops with a retained C
 * baseline. Deliberately builds the public plan rather than private filters. */
#include "echoy_audio.h"
#include <string.h>

int echoy_audio_baseline_probe(unsigned kind, unsigned note, float *output)
{
    echoy_plan_t plan = {.role = ECHOY_HARMONY, .phrase = 1, .source_count = 1, .count = 1,
        .source = {{.step = 0, .velocity = .74, .kind = ECHOY_KICK}},
        .events = {{.step = 0, .velocity = .74, .duration = 2.5, .layer = ECHOY_ANCHOR}}};
    plan.events[0].kind = (echoy_note_kind_t)kind;
    plan.events[0].note = (int)note;
    echoy_audio_loops_t *loops = NULL;
    echoy_audio_result_t result = echoy_audio_prepare(&plan, false, &loops);
    if (result != ECHOY_AUDIO_OK) return result;
    memcpy(output, loops->samples[ECHOY_AUDIO_FULL], loops->frames * sizeof(float));
    echoy_audio_free_loops(loops);
    return 0;
}
