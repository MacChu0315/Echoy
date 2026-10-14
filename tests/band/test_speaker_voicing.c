/* Include the production band implementation to exercise otherwise private
 * reference-ownership guards without adding test hooks to the product API. */
#include "../../projects/echoy/main/echoy_band.c"
#include <assert.h>
#include <stdio.h>

#define TEST_BAR INT64_C(2500000)
static const float speaker_gains[4] = {1.6f, .65f, .0634f, 1.6f};
static struct observed_bank {
    echoy_audio_loops_t *loops;
    echoy_plan_t plan;
    float *before[ECHOY_AUDIO_KIND_COUNT];
    double target_rms;
} observed[16];
static unsigned bank_count, voicing_calls, fail_voicing_at;

echoy_audio_result_t __real_echoy_audio_prepare(const echoy_plan_t *, bool, echoy_audio_loops_t **);
void __real_echoy_audio_free_loops(echoy_audio_loops_t *);
bool __real_echoy_audio_voice_for_speaker(const echoy_plan_t *, echoy_audio_loops_t *);

echoy_audio_result_t __wrap_echoy_audio_prepare(const echoy_plan_t *plan, bool original,
                                               echoy_audio_loops_t **out)
{
    echoy_audio_result_t error = __real_echoy_audio_prepare(plan, original, out);
    if (error != ECHOY_AUDIO_OK) return error;
    assert(bank_count < 16);
    struct observed_bank *bank = &observed[bank_count++];
    bank->loops = *out;
    memcpy(&bank->plan, plan, sizeof(*plan));
    bank->target_rms = (*out)->target_rms;
    for (unsigned kind = 0; kind < ECHOY_AUDIO_KIND_COUNT; ++kind) {
        bank->before[kind] = malloc((*out)->frames * sizeof(float));
        assert(bank->before[kind]);
        memcpy(bank->before[kind], (*out)->samples[kind], (*out)->frames * sizeof(float));
    }
    return error;
}
void __wrap_echoy_audio_free_loops(echoy_audio_loops_t *loops)
{
    if (loops) {
        unsigned index = 0;
        while (index < bank_count && observed[index].loops != loops) ++index;
        assert(index < bank_count);
        observed[index].loops = NULL;
    }
    __real_echoy_audio_free_loops(loops);
}
bool __wrap_echoy_audio_voice_for_speaker(const echoy_plan_t *plan, echoy_audio_loops_t *loops)
{
    unsigned index = 0;
    while (index < bank_count && observed[index].loops != loops) ++index;
    assert(index < bank_count);
    assert(memcmp(plan, &observed[index].plan, sizeof(*plan)) == 0);
    ++voicing_calls;
    if (fail_voicing_at == voicing_calls) return false;
    /* Success always invokes the real DSP. Only error propagation is injected;
     * this suite makes no claim about acoustic quality or loudness. */
    return __real_echoy_audio_voice_for_speaker(plan, loops);
}
static bool unchanged(unsigned index, echoy_audio_kind_t kind)
{
    struct observed_bank *bank = &observed[index];
    assert(bank->loops);
    return !memcmp(bank->before[kind], bank->loops->samples[kind],
                   bank->loops->frames * sizeof(float));
}
static echoy_control_view_t snapshot(echoy_band_t *band)
{
    echoy_control_view_t view;
    assert(echoy_band_read(band, ECHOY_TILE_A, &view));
    return view;
}
static echoy_band_t *new_band(unsigned role, unsigned phrase)
{
    echoy_band_t *band = echoy_band_create(); assert(band);
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_ROLE, role, 0));
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, phrase, 0));
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_EXAMPLE, 0, 0));
    return band;
}
static void cleanup(echoy_band_t *band)
{
    echoy_band_delete(band);
    for (unsigned i = 0; i < bank_count; ++i) {
        assert(!observed[i].loops);
        for (unsigned kind = 0; kind < ECHOY_AUDIO_KIND_COUNT; ++kind) free(observed[i].before[kind]);
    }
    memset(observed, 0, sizeof(observed));
    bank_count = voicing_calls = fail_voicing_at = 0;
}
static void all_expressions_forward_owned_full_before_calibration(void)
{
    assert(!echoy_band_voice_for_speaker_job(NULL));
    for (unsigned role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) {
        for (unsigned phrase = 1; phrase <= 3; ++phrase) {
            echoy_band_t *band = new_band(role, phrase);
            echoy_band_job_t *job = echoy_band_take_job(band); assert(job);
            assert(!echoy_band_voice_for_speaker_job(job));
            assert(echoy_band_prepare_job(job) && bank_count == 1);
            assert(echoy_band_voice_for_speaker_job(job) && voicing_calls == 1);
            assert(!echoy_band_voice_for_speaker_job(job) && voicing_calls == 1);
            assert(!echoy_band_cut_harmony_bass_job(job));
            assert(unchanged(0, ECHOY_AUDIO_SOURCE) && unchanged(0, ECHOY_AUDIO_FOREGROUND));
            assert(observed[0].target_rms == observed[0].loops->target_rms);
            float *voiced = malloc(ECHOY_AUDIO_LOOP_FRAMES * sizeof(float)); assert(voiced);
            memcpy(voiced, observed[0].loops->samples[ECHOY_AUDIO_FULL],
                   ECHOY_AUDIO_LOOP_FRAMES * sizeof(float));
            assert(echoy_band_calibrate_job(job, speaker_gains));
            assert(!echoy_band_voice_for_speaker_job(job));
            for (size_t i = 0; i < ECHOY_AUDIO_LOOP_FRAMES; ++i) {
                assert(isfinite(voiced[i]));
                assert(observed[0].loops->samples[ECHOY_AUDIO_FULL][i] == voiced[i] * speaker_gains[role]);
            }
            assert(echoy_band_finish_job(band, job, 0));
            echoy_band_tick(band, 45000);
            echoy_control_view_t view = snapshot(band);
            assert(view.plan.role == role && view.plan.phrase == phrase);
            assert(view.plan.count == observed[0].plan.count);
            assert(!memcmp(view.plan.events, observed[0].plan.events,
                           view.plan.count * sizeof(view.plan.events[0])));
            free(voiced); cleanup(band);
        }
    }
    puts("PASS all 12 prepared expressions forward their original plans; Source/Foreground/target stay unchanged and calibration follows voicing");
}
static void published_calibrated_and_old_trial_are_rejected(void)
{
    echoy_band_t *band = new_band(ECHOY_HARMONY, 1);
    echoy_band_job_t *job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(echoy_band_cut_harmony_bass_job(job));
    assert(!echoy_band_voice_for_speaker_job(job) && !voicing_calls);
    echoy_band_discard_job(job); cleanup(band);

    band = new_band(ECHOY_VOICE, 1);
    job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(echoy_band_calibrate_job(job, speaker_gains));
    assert(!echoy_band_voice_for_speaker_job(job) && !voicing_calls);
    echoy_band_discard_job(job); cleanup(band);

    band = new_band(ECHOY_HARMONY, 1);
    job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(echoy_band_finish_job(band, job, 0)); /* Still alive, waiting for its 40ms commit. */
    assert(job->staged && job->handed_off);
    assert(!echoy_band_voice_for_speaker_job(job) && !voicing_calls);
    assert(unchanged(0, ECHOY_AUDIO_FULL)); cleanup(band);

    /* A joint-change hold admits a ready bank without staging it. Exercise
     * that ownership interval separately from the staged guard above. */
    band = new_band(ECHOY_HARMONY, 1);
    job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    band->cooperation->active = band->cooperation->hold = true;
    assert(echoy_band_finish_job(band, job, 0));
    assert(job->handed_off && !job->staged);
    assert(!echoy_band_voice_for_speaker_job(job) && !voicing_calls);
    assert(unchanged(0, ECHOY_AUDIO_FULL)); cleanup(band);
    puts("PASS mutual exclusion and calibrated/staged/handed-off guards prevent modifying published banks");
}
static void a_shared_later_bank_rejects_before_any_processing(void)
{
    echoy_band_t *band = new_band(ECHOY_VOICE, 1);
    assert(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_PRESENT, 1, 0));
    assert(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_EXAMPLE, 0, 0));
    echoy_band_job_t *job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(bank_count == 2 && job->banks[0]->refs == 1 && job->banks[1]->refs == 1);
    struct bank *borrowed = retain(job->banks[1]);
    assert(!echoy_band_voice_for_speaker_job(job) && !voicing_calls);
    assert(unchanged(0, ECHOY_AUDIO_FULL) && unchanged(1, ECHOY_AUDIO_FULL));
    assert(!echoy_band_calibrate_job(job, speaker_gains));
    assert(!echoy_band_prepare_job(job));
    release(borrowed);
    assert(!echoy_band_finish_job(band, job, 0));
    cleanup(band);
    puts("PASS nonexclusive bank preflight rejects the entire job before any buffer mutation");
}
static void old_bank_survives_until_common_bar_and_pause(void)
{
    echoy_band_t *band = new_band(ECHOY_HARMONY, 1);
    echoy_band_job_t *job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(echoy_band_finish_job(band, job, 0)); echoy_band_tick(band, 45000);
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, 3000000));
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 3100000));
    echoy_band_tick(band, 3100000);
    echoy_control_view_t old = snapshot(band); assert(old.state.continuous);
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 2, 3200000));
    job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(echoy_band_voice_for_speaker_job(job));
    assert(unchanged(0, ECHOY_AUDIO_FULL));
    assert(echoy_band_finish_job(band, job, 3250000));
    echoy_control_view_t pending = snapshot(band);
    assert(pending.plan.phrase == 1 && pending.state.apply_at_us > 3250000);
    assert((pending.state.apply_at_us - old.state.clock_origin_us) % TEST_BAR == 0);
    echoy_band_tick(band, pending.state.apply_at_us - 1);
    assert(snapshot(band).plan.phrase == 1 && unchanged(0, ECHOY_AUDIO_FULL));
    echoy_band_tick(band, pending.state.apply_at_us);
    assert(snapshot(band).plan.phrase == 2 && snapshot(band).state.continuous);
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, pending.state.apply_at_us + 10000));
    float pcm[480]; echoy_band_render(band, pending.state.apply_at_us + 30000, pcm, 480);
    for (unsigned i = 0; i < 480; ++i) assert(pcm[i] == 0);
    cleanup(band);
    puts("PASS live bank stays immutable until common-bar replacement and Pause still stops PCM");
}
static void failed_job_preserves_continuous_old_sound(void)
{
    echoy_band_t *band = new_band(ECHOY_VOICE, 1);
    echoy_band_job_t *job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(echoy_band_finish_job(band, job, 0)); echoy_band_tick(band, 45000);
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, 3000000));
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 3100000));
    echoy_band_tick(band, 3100000);
    echoy_control_view_t old = snapshot(band); assert(old.state.continuous);
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 2, 3200000));
    job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    fail_voicing_at = 1;
    assert(!echoy_band_voice_for_speaker_job(job));
    assert(!echoy_band_finish_job(band, job, 3250000));
    echoy_control_view_t failed = snapshot(band);
    assert(failed.preparation_failed && failed.plan.phrase == old.plan.phrase);
    assert(failed.state.continuous && failed.state.phase == ECHOY_PLAYING);
    assert(failed.state.start_us == old.state.start_us && unchanged(0, ECHOY_AUDIO_FULL));
    float pcm[480];
    int64_t at = old.state.start_us + 312500;
    echoy_band_render(band, at, pcm, 480);
    double energy = 0;
    for (unsigned i = 0; i < 480; ++i) energy += pcm[i] * (double)pcm[i];
    assert(energy > 1e-6);
    cleanup(band);
    puts("PASS failed speaker preparation leaves the old continuous loop audible with its original clock");
}
static void stale_and_partial_failure_preserve_paused_valid_sound(void)
{
    echoy_band_t *band = new_band(ECHOY_VOICE, 1);
    echoy_band_job_t *job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(echoy_band_finish_job(band, job, 0)); echoy_band_tick(band, 45000);
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, 3000000));
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 2, 3100000));
    job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    assert(echoy_band_voice_for_speaker_job(job));
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 3, 3150000));
    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, 3200000));
    assert(!echoy_band_finish_job(band, job, 3220000));
    assert(unchanged(0, ECHOY_AUDIO_FULL) && snapshot(band).state.phase == ECHOY_IDLE);

    assert(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 2, 3300000));
    assert(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_PRESENT, 1, 3300000));
    assert(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_EXAMPLE, 0, 3300000));
    job = echoy_band_take_job(band); assert(echoy_band_prepare_job(job));
    unsigned before = voicing_calls;
    fail_voicing_at = before + 2;
    assert(!echoy_band_voice_for_speaker_job(job) && voicing_calls == before + 2);
    assert(!echoy_band_voice_for_speaker_job(job) && voicing_calls == before + 2);
    assert(!echoy_band_cut_harmony_bass_job(job));
    assert(!echoy_band_calibrate_job(job, speaker_gains));
    assert(!echoy_band_prepare_job(job));
    assert(!echoy_band_finish_job(band, job, 3400000));
    echoy_band_tick(band, 6000000);
    echoy_control_view_t failed = snapshot(band);
    assert(unchanged(0, ECHOY_AUDIO_FULL));
    assert(failed.preparation_failed && failed.plan.phrase == 1);
    assert(!failed.state.continuous && failed.state.phase == ECHOY_IDLE);
    cleanup(band);
    puts("PASS stale/cancelled jobs and a later-bank DSP failure cannot replace the paused valid sound");
}
int main(void)
{
    all_expressions_forward_owned_full_before_calibration();
    published_calibrated_and_old_trial_are_rejected();
    a_shared_later_bank_rejects_before_any_processing();
    old_bank_survives_until_common_bar_and_pause();
    failed_job_preserves_continuous_old_sound();
    stale_and_partial_failure_preserve_paused_valid_sound();
    return 0;
}
