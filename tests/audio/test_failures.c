#include "echoy_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned fail_at, calls, live, checks;
static void *tracked[8];
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void __real_free(void *pointer);

static void *track(void *pointer)
{
    if (!pointer || !fail_at) return pointer;
    for (unsigned i = 0; i < 8; ++i) if (!tracked[i]) {
        tracked[i] = pointer;
        ++live;
        return pointer;
    }
    abort();
}
void *__wrap_malloc(size_t size)
{
    return fail_at && ++calls == fail_at ? NULL : track(__real_malloc(size));
}
void *__wrap_calloc(size_t count, size_t size)
{
    return fail_at && ++calls == fail_at ? NULL : track(__real_calloc(count, size));
}
void __wrap_free(void *pointer)
{
    if (pointer) for (unsigned i = 0; i < 8; ++i) if (tracked[i] == pointer) {
        tracked[i] = NULL;
        --live;
        break;
    }
    __real_free(pointer);
}

#define CHECK(x) do { ++checks; assert(x); } while (0)

static void failure_paths(const echoy_plan_t *plan, int cue)
{
    bool complete = false;
    for (unsigned i = 1; i < 8 && !complete; ++i) {
        fail_at = i;
        calls = 0;
        echoy_audio_result_t result;
        if (cue < 0) {
            echoy_audio_loops_t *out = NULL;
            result = echoy_audio_prepare(plan, false, &out);
            CHECK(result == ECHOY_AUDIO_OK || result == ECHOY_AUDIO_NO_MEMORY);
            CHECK((result == ECHOY_AUDIO_OK) == (out != NULL));
            echoy_audio_free_loops(out);
        } else {
            echoy_audio_buffer_t *out = NULL;
            result = echoy_audio_render_cue((echoy_audio_cue_t)cue, &out);
            CHECK(result == ECHOY_AUDIO_OK || result == ECHOY_AUDIO_NO_MEMORY);
            CHECK((result == ECHOY_AUDIO_OK) == (out != NULL));
            echoy_audio_free_buffer(out);
        }
        complete = result == ECHOY_AUDIO_OK;
        CHECK(complete ? calls < fail_at : calls >= fail_at);
        CHECK(live == 0);
        fail_at = 0;
    }
    CHECK(complete);
}

int main(void)
{
    echoy_plan_t valid;
    CHECK(echoy_compose_mask(0x1085, ECHOY_HARMONY, 3, 1, &valid));
    failure_paths(&valid, -1);
    for (int cue = 0; cue < 3; ++cue) failure_paths(&valid, cue);
    for (unsigned change = 0; change < 14; ++change) {
        echoy_plan_t plan = valid;
        switch (change) {
        case 0: plan.count = ECHOY_MAX_EVENTS + 1; break;
        case 1: plan.source_count = 17; break;
        case 2: plan.role = (echoy_role_t)99; break;
        case 3: plan.phrase = 4; break;
        case 4: plan.events[0].step = NAN; break;
        case 5: plan.events[0].step = -1; break;
        case 6: plan.events[0].velocity = INFINITY; break;
        case 7: plan.events[0].duration = INFINITY; break;
        case 8: plan.events[0].max_duration = -1; break;
        case 9: plan.events[0].note_count = 4; break;
        case 10: plan.events[0].notes[0] = 128; plan.events[0].note_count = 1; break;
        case 11: plan.events[0].kind = (echoy_note_kind_t)99; break;
        case 12: plan.source[0].step = 16; break;
        case 13: plan.source[0].velocity = NAN; break;
        }
        echoy_audio_loops_t *out = NULL;
        CHECK(echoy_audio_prepare(&plan, false, &out) == ECHOY_AUDIO_INVALID && !out);
    }
    echoy_audio_loops_t *out = NULL;
    CHECK(echoy_audio_prepare(&valid, true, &out) == ECHOY_AUDIO_INVALID && !out);
    printf("audio error/ownership checks: %u\n", checks);
    return 0;
}
