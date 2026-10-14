#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR INT64_C(2500000)
static const float gains[4] = {1.6f, .52f, .0317f, 2.38f};
static struct observed_bank {
    echoy_audio_loops_t *loops;
    echoy_plan_t plan;
    float *before[ECHOY_AUDIO_KIND_COUNT];
    double target_rms;
} banks[16];
static unsigned bank_count;
echoy_audio_result_t __real_echoy_audio_prepare(const echoy_plan_t *, bool, echoy_audio_loops_t **);
void __real_echoy_audio_free_loops(echoy_audio_loops_t *);
echoy_audio_result_t __wrap_echoy_audio_prepare(const echoy_plan_t *p, bool original,
                                               echoy_audio_loops_t **out)
{
    echoy_audio_result_t error = __real_echoy_audio_prepare(p, original, out);
    if (error != ECHOY_AUDIO_OK) return error;
    assert(bank_count < 16);
    struct observed_bank *b = &banks[bank_count++];
    b->loops = *out;
    b->plan = *p;
    b->target_rms = (*out)->target_rms;
    for (unsigned kind = 0; kind < ECHOY_AUDIO_KIND_COUNT; ++kind) {
        b->before[kind] = malloc((*out)->frames * sizeof(float));
        assert(b->before[kind]);
        memcpy(b->before[kind], (*out)->samples[kind], (*out)->frames * sizeof(float));
    }
    return error;
}
void __wrap_echoy_audio_free_loops(echoy_audio_loops_t *loops)
{
    if (loops) {
        unsigned index = 0;
        while (index < bank_count && banks[index].loops != loops) ++index;
        assert(index < bank_count);
        banks[index].loops = NULL;
    }
    __real_echoy_audio_free_loops(loops);
}
static void cleanup(echoy_band_t *b)
{
    echoy_band_delete(b);
    for (unsigned i = 0; i < bank_count; ++i) {
        assert(!banks[i].loops);
        for (unsigned kind = 0; kind < ECHOY_AUDIO_KIND_COUNT; ++kind) free(banks[i].before[kind]);
    }
    memset(banks, 0, sizeof(banks));
    bank_count = 0;
}
static echoy_control_view_t view(echoy_band_t *b)
{
    echoy_control_view_t v;
    assert(echoy_band_read(b, ECHOY_TILE_A, &v));
    return v;
}
static echoy_band_t *new_band(unsigned role)
{
    echoy_band_t *b = echoy_band_create(); assert(b);
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_ROLE, role, 0));
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_EXAMPLE, 0, 0));
    return b;
}
static bool same_kind(unsigned index, echoy_audio_kind_t kind)
{
    struct observed_bank *b = &banks[index]; assert(b->loops);
    return !memcmp(b->before[kind], b->loops->samples[kind], b->loops->frames * sizeof(float));
}
static void only_harmony_full_changes(void)
{
    assert(!echoy_band_cut_harmony_bass_job(NULL));
    for (unsigned role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) {
        echoy_band_t *b = new_band(role);
        echoy_band_job_t *j = echoy_band_take_job(b); assert(j);
        assert(!echoy_band_cut_harmony_bass_job(j));
        assert(echoy_band_prepare_job(j) && bank_count == 1);
        assert(echoy_band_cut_harmony_bass_job(j));
        assert(!echoy_band_cut_harmony_bass_job(j));
        assert(same_kind(0, ECHOY_AUDIO_SOURCE) && same_kind(0, ECHOY_AUDIO_FOREGROUND));
        assert(same_kind(0, ECHOY_AUDIO_FULL) == (role != ECHOY_HARMONY));
        assert(banks[0].loops->target_rms == banks[0].target_rms);
        float *filtered = malloc(ECHOY_AUDIO_LOOP_FRAMES * sizeof(float)); assert(filtered);
        memcpy(filtered, banks[0].loops->samples[ECHOY_AUDIO_FULL], ECHOY_AUDIO_LOOP_FRAMES*sizeof(float));
        assert(echoy_band_calibrate_job(j, gains));
        assert(!echoy_band_cut_harmony_bass_job(j));
        for (size_t i = 0; i < ECHOY_AUDIO_LOOP_FRAMES; ++i)
            assert(banks[0].loops->samples[ECHOY_AUDIO_FULL][i] == filtered[i] * gains[role]);
        assert(echoy_band_finish_job(b, j, 0));
        echoy_band_tick(b, 45000);
        echoy_control_view_t v = view(b);
        assert(v.plan.role == role && v.plan.phrase == banks[0].plan.phrase);
        assert(v.plan.count == banks[0].plan.count && v.plan.density == banks[0].plan.density);
        assert(!memcmp(v.plan.events, banks[0].plan.events, v.plan.count * sizeof(v.plan.events[0])));
        free(filtered); cleanup(b);
    }
    puts("PASS only Harmony FULL changes, without plan or normalization changes, before role trim");
}
static void handed_off_and_calibrated_jobs_reject_cut(void)
{
    echoy_band_t *b = new_band(ECHOY_HARMONY);
    echoy_band_job_t *j = echoy_band_take_job(b); assert(echoy_band_prepare_job(j));
    assert(echoy_band_calibrate_job(j, gains));
    assert(!echoy_band_cut_harmony_bass_job(j));
    echoy_band_discard_job(j); cleanup(b);
    b = new_band(ECHOY_HARMONY);
    j = echoy_band_take_job(b); assert(echoy_band_prepare_job(j));
    assert(echoy_band_finish_job(b, j, 0)); /* Staged for 40 ms; j still lives in ready. */
    assert(!echoy_band_cut_harmony_bass_job(j));
    assert(same_kind(0, ECHOY_AUDIO_FULL));
    cleanup(b);
    puts("PASS prepared-job phase guards reject calibrated and published banks");
}
static void old_bank_remains_immutable_until_common_bar(void)
{
    echoy_band_t *b = new_band(ECHOY_HARMONY);
    echoy_band_job_t *j = echoy_band_take_job(b); assert(echoy_band_prepare_job(j));
    assert(echoy_band_finish_job(b, j, 0)); echoy_band_tick(b, 45000);
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, 3000000));
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 3100000));
    echoy_band_tick(b, 3100000);
    echoy_control_view_t old = view(b); assert(old.state.continuous);
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 2, 3200000));
    j = echoy_band_take_job(b); assert(echoy_band_prepare_job(j) && bank_count == 2);
    assert(echoy_band_cut_harmony_bass_job(j));
    assert(same_kind(0, ECHOY_AUDIO_FULL) && !same_kind(1, ECHOY_AUDIO_FULL));
    assert(view(b).plan.phrase == old.plan.phrase);
    assert(echoy_band_finish_job(b, j, 3250000));
    echoy_control_view_t pending = view(b);
    assert(pending.state.apply_at_us > 3250000);
    assert((pending.state.apply_at_us - old.state.clock_origin_us) % BAR == 0);
    echoy_band_tick(b, pending.state.apply_at_us - 1);
    assert(view(b).plan.phrase == old.plan.phrase && same_kind(0, ECHOY_AUDIO_FULL));
    echoy_band_tick(b, pending.state.apply_at_us);
    assert(view(b).plan.phrase == 2 && view(b).state.continuous);
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, pending.state.apply_at_us+10000));
    float pcm[480]; echoy_band_render(b, pending.state.apply_at_us+30000, pcm, 480);
    for (unsigned i=0;i<480;++i) assert(pcm[i]==0);
    cleanup(b); puts("PASS old bank stays immutable until the common bar and Pause clears sound");
}
static void stale_and_failed_jobs_do_not_replace_old_bank(void)
{
    echoy_band_t *b = new_band(ECHOY_HARMONY);
    echoy_band_job_t *j=echoy_band_take_job(b); assert(echoy_band_prepare_job(j));
    assert(echoy_band_finish_job(b,j,0));echoy_band_tick(b,45000);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,3000000));
    echoy_control_view_t old=view(b); assert(!old.state.continuous);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PHRASE,2,3100000));
    j=echoy_band_take_job(b);assert(echoy_band_prepare_job(j));
    assert(echoy_band_cut_harmony_bass_job(j));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PHRASE,3,3150000));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,3200000));
    assert(!echoy_band_finish_job(b,j,3220000));
    assert(same_kind(0,ECHOY_AUDIO_FULL) && view(b).state.phase==ECHOY_IDLE);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_PHRASE,2,3300000));
    j=echoy_band_take_job(b);assert(echoy_band_prepare_job(j));
    banks[bank_count-1].loops->samples[ECHOY_AUDIO_FULL][100]=NAN;
    assert(!echoy_band_cut_harmony_bass_job(j));
    assert(!echoy_band_calibrate_job(j,gains));
    assert(!echoy_band_prepare_job(j)); /* A failed cut cannot be repurposed or retried. */
    assert(!echoy_band_finish_job(b,j,3400000));
    echoy_band_tick(b,6000000);
    assert(same_kind(0,ECHOY_AUDIO_FULL));
    echoy_control_view_t failed=view(b);
    assert(failed.preparation_failed && !failed.state.continuous && failed.state.phase==ECHOY_IDLE);
    assert(failed.plan.phrase==old.plan.phrase);
    cleanup(b); puts("PASS cancelled/stale and failed filtered jobs preserve the paused valid bank");
}
static void later_bank_failure_rejects_whole_job(void)
{
    echoy_band_t *b = new_band(ECHOY_HARMONY);
    assert(echoy_band_action(b,ECHOY_TILE_B,ECHOY_ACT_PRESENT,1,0));
    assert(echoy_band_action(b,ECHOY_TILE_B,ECHOY_ACT_ROLE,ECHOY_HARMONY,0));
    assert(echoy_band_action(b,ECHOY_TILE_B,ECHOY_ACT_EXAMPLE,0,0));
    echoy_band_job_t *j=echoy_band_take_job(b);assert(echoy_band_prepare_job(j));
    assert(bank_count==2);
    banks[1].loops->samples[ECHOY_AUDIO_FULL][100]=NAN;
    assert(!echoy_band_cut_harmony_bass_job(j));
    assert(!same_kind(0,ECHOY_AUDIO_FULL)); /* First bank really finished filtering. */
    assert(same_kind(0,ECHOY_AUDIO_SOURCE) && same_kind(1,ECHOY_AUDIO_SOURCE));
    assert(!echoy_band_finish_job(b,j,0));
    echoy_band_tick(b,3000000);
    echoy_control_view_t v=view(b);
    assert(!v.plan.count && v.preparation_failed && !v.state.continuous);
    cleanup(b);puts("PASS later-bank filter failure cannot publish an earlier processed bank");
}
int main(void)
{
    only_harmony_full_changes();
    handed_off_and_calibrated_jobs_reject_cut();
    old_bank_remains_immutable_until_common_bar();
    stale_and_failed_jobs_do_not_replace_old_bank();
    later_bank_failure_rejects_whole_job();
    return 0;
}
