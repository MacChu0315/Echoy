/* Count expensive operations instead of imposing a host-dependent time limit.
 * The ESP32-S31 has single-precision hardware, but software double math. */
#include "echoy_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

static unsigned double_calls, powers, trigonometry;
#define WRAP_ONE(name) \
    double __real_##name(double); \
    double __wrap_##name(double x) { ++double_calls; return __real_##name(x); }
WRAP_ONE(sin)
WRAP_ONE(cos)
WRAP_ONE(exp)
WRAP_ONE(exp2)
WRAP_ONE(sqrt)
double __real_pow(double, double);
double __wrap_pow(double x, double y) { ++double_calls; return __real_pow(x, y); }
float __real_sinf(float);
float __real_cosf(float);
float __wrap_sinf(float x) { ++trigonometry; return __real_sinf(x); }
float __wrap_cosf(float x) { ++trigonometry; return __real_cosf(x); }
float __real_powf(float, float);
float __wrap_powf(float x, float y) { ++powers; return __real_powf(x, y); }

int main(void)
{
    for (unsigned role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) {
        echoy_plan_t plan;
        assert(echoy_compose_mask(0x1085, (echoy_role_t)role, 3, 1, &plan));
        double_calls = powers = trigonometry = 0;
        clock_t start = clock();
        echoy_audio_loops_t *loops = NULL;
        assert(echoy_audio_prepare(&plan, false, &loops) == ECHOY_AUDIO_OK);
        printf("role=%u events=%zu prepare_ms=%.3f double_libm=%u powers=%u trig=%u\n",
               role, plan.count, 1000.0 * (clock() - start) / CLOCKS_PER_SEC, double_calls, powers, trigonometry);
        fflush(stdout);
        /* No per-sample software double math or envelope power evaluation. */
#ifndef ECHOY_PERFORMANCE_BASELINE
        assert(double_calls < 1000);
        assert(powers < 30000);
        assert(trigonometry < 10000);
#endif
        echoy_audio_free_loops(loops);
    }
    /* The public plan accepts the whole MIDI range. Upper partial setup must
     * not overflow the uint32 phase conversion, even when later band-limited. */
    for (unsigned kind = ECHOY_TONE; kind <= ECHOY_CHORD; ++kind) {
        echoy_plan_t plan = {.role = ECHOY_HARMONY, .phrase = 1, .source_count = 1, .count = 1,
            .source = {{.step = 0, .velocity = .74, .kind = ECHOY_KICK}},
            .events = {{.step = 0, .velocity = .74, .duration = .1, .note = 127, .layer = ECHOY_ANCHOR}}};
        plan.events[0].kind = (echoy_note_kind_t)kind;
        echoy_audio_loops_t *loops = NULL;
        assert(echoy_audio_prepare(&plan, false, &loops) == ECHOY_AUDIO_OK);
        for (unsigned k = 0; k < ECHOY_AUDIO_KIND_COUNT; ++k)
            for (size_t i = 0; i < loops->frames; ++i) assert(isfinite(loops->samples[k][i]));
        echoy_audio_free_loops(loops);
    }
    return 0;
}
