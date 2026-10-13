#include "echoy_band.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR INT64_C(2500000)
static const char *test_name;
static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", test_name, __LINE__, #x); exit(1); \
} } while (0)
static const float gains[4] = {1.6f, .52f, .0317f, 2.38f};
static struct {
    unsigned calls, by_role[4], live, released;
    size_t allocated_bytes, released_bytes;
    echoy_audio_loops_t *banks[32];
} synthesis;

/* Observe allocations while forwarding to the real synthesis and destructor.
 * The band API remains unchanged and the worker receives no live-band access. */
echoy_audio_result_t __real_echoy_audio_prepare(const echoy_plan_t *, bool, echoy_audio_loops_t **);
void __real_echoy_audio_free_loops(echoy_audio_loops_t *);
echoy_audio_result_t __wrap_echoy_audio_prepare(const echoy_plan_t *plan, bool original,
                                              echoy_audio_loops_t **out)
{
    ++synthesis.calls;
    ++synthesis.by_role[plan->role];
    echoy_audio_result_t result = __real_echoy_audio_prepare(plan, original, out);
    if (result == ECHOY_AUDIO_OK) {
        unsigned slot = 0;
        while (slot < 32 && synthesis.banks[slot]) ++slot;
        CHECK(slot < 32);
        synthesis.banks[slot] = *out;
        ++synthesis.live;
        synthesis.allocated_bytes += sizeof(**out) +
            ECHOY_AUDIO_KIND_COUNT * (*out)->frames * sizeof(float);
    }
    return result;
}
void __wrap_echoy_audio_free_loops(echoy_audio_loops_t *loops)
{
    if (loops) {
        unsigned slot = 0;
        while (slot < 32 && synthesis.banks[slot] != loops) ++slot;
        CHECK(slot < 32);
        synthesis.banks[slot] = NULL;
        --synthesis.live;
        ++synthesis.released;
        synthesis.released_bytes += sizeof(*loops) +
            ECHOY_AUDIO_KIND_COUNT * loops->frames * sizeof(float);
    }
    __real_echoy_audio_free_loops(loops);
}

static echoy_control_view_t view(echoy_band_t *band, echoy_tile_id_t tile)
{
    echoy_control_view_t result;
    CHECK(echoy_band_read(band, tile, &result));
    return result;
}
static echoy_plan_t remote_plan(uint16_t seed)
{
    echoy_plan_t plan;
    CHECK(echoy_compose_mask(seed, ECHOY_PULSE, 1, .5, &plan));
    return plan;
}
static void worker(echoy_band_job_t *job, unsigned expected_calls)
{
    unsigned before = synthesis.calls;
    size_t allocated = synthesis.allocated_bytes;
    CHECK(job && echoy_band_prepare_job(job));
    fprintf(stdout, "MEASURE %s: prepare_calls=%u loop_bytes=%zu calibration_frames=%u\n",
            test_name, synthesis.calls - before, synthesis.allocated_bytes - allocated,
            (synthesis.calls - before) * ECHOY_AUDIO_LOOP_FRAMES);
    CHECK(synthesis.calls - before == expected_calls);
    CHECK(echoy_band_calibrate_job(job, gains));
    CHECK(!echoy_band_calibrate_job(job, gains));
}
static void prepare(echoy_band_t *band, int64_t now, unsigned expected_calls)
{
    echoy_band_job_t *job = echoy_band_take_job(band);
    worker(job, expected_calls);
    CHECK(echoy_band_finish_job(band, job, now));
    echoy_band_tick(band, now + 45000);
    echoy_band_tick(band, now + 2 * BAR);
}
static void destroy(echoy_band_t *band)
{
    echoy_band_delete(band);
    CHECK(synthesis.live == 0);
    CHECK(synthesis.allocated_bytes == synthesis.released_bytes);
}

static void physical_replica_has_no_audio_bank(void)
{
    echoy_band_t *band = echoy_band_create();
    CHECK(band);
    echoy_plan_t remote = remote_plan(0x1128), local, expected;
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_DUST, 0));
    CHECK(echoy_band_set_upstream(band, true, &remote, true, 200000, true, 0));
    echoy_arrange_job_t *compiled = calloc(1, sizeof(*compiled));
    CHECK(compiled && echoy_arrange_compile(echoy_band_arrangement(band), compiled));
    CHECK(compiled->changed_mask == ((1u << ECHOY_TILE_A) | (1u << ECHOY_TILE_B)));
    CHECK(compiled->plans[ECHOY_TILE_B].valid);
    CHECK(compiled->clock_id[ECHOY_TILE_A] == ECHOY_TILE_B);
    CHECK(compiled->plans[ECHOY_TILE_A].source == ECHOY_TILE_B);
    free(compiled);
    prepare(band, 0, 1);
    CHECK(synthesis.by_role[ECHOY_DUST] == 1);
    CHECK(synthesis.by_role[ECHOY_PULSE] == 0);
    CHECK(synthesis.live == 1);
    echoy_control_view_t ready = view(band, ECHOY_TILE_A);
    CHECK(ready.source == ECHOY_TILE_B && ready.clock == ECHOY_TILE_B);
    CHECK(ready.state.clock_origin_us == 200000 && ready.state.clock_started);
    CHECK(ready.state.phase == ECHOY_IDLE);
    CHECK(echoy_compose(remote.source, remote.source_count, ECHOY_DUST, 1, .5, &local));
    CHECK(echoy_arrange_linked(&local, &remote, &expected));
    CHECK(ready.plan.count == expected.count);
    CHECK(!memcmp(ready.plan.events, expected.events, expected.count * sizeof(expected.events[0])));
    echoy_audio_loops_t *expected_audio = NULL;
    CHECK(__real_echoy_audio_prepare(&expected, false, &expected_audio) == ECHOY_AUDIO_OK);
    for (size_t i = 0; i < expected_audio->frames; ++i)
        expected_audio->samples[ECHOY_AUDIO_FULL][i] *= gains[ECHOY_DUST];
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 5100000));
    echoy_band_tick(band, 5100000);
    int64_t start = view(band, ECHOY_TILE_A).state.start_us;
    float *pcm = calloc(ECHOY_AUDIO_LOOP_FRAMES, sizeof(*pcm));
    CHECK(pcm);
    echoy_band_render(band, start, pcm, ECHOY_AUDIO_LOOP_FRAMES);
    double energy = 0;
    for (size_t i = 0; i < ECHOY_AUDIO_LOOP_FRAMES; ++i) {
        double want = expected_audio->samples[ECHOY_AUDIO_FULL][i] * .65 * .45;
        CHECK(fabs(pcm[i] - want) < 1e-7);
        energy += (double)pcm[i] * pcm[i];
    }
    CHECK(energy > 1e-6);
    free(pcm);
    __real_echoy_audio_free_loops(expected_audio);
    destroy(band);
}

static void pc_keeps_all_three_audio_banks(void)
{
    echoy_band_t *band = echoy_band_create();
    CHECK(band);
    CHECK(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_PRESENT, 1, 0));
    CHECK(echoy_band_action(band, ECHOY_TILE_C, ECHOY_ACT_PRESENT, 1, 0));
    for (int tile = ECHOY_TILE_A; tile <= ECHOY_TILE_C; ++tile)
        CHECK(echoy_band_action(band, tile, ECHOY_ACT_EXAMPLE, 0, 0));
    prepare(band, 0, 3);
    CHECK(synthesis.live == 3);
    for (int tile = ECHOY_TILE_A; tile <= ECHOY_TILE_C; ++tile) {
        CHECK(view(band, tile).plan.count);
        CHECK(echoy_band_action(band, tile, ECHOY_ACT_TRANSPORT, 0, 5100000));
        echoy_band_tick(band, 5100000);
        CHECK(view(band, tile).state.phase == ECHOY_PLAYING);
    }
    destroy(band);
}

static void physical_job_keeps_snapshot_after_detach(void)
{
    echoy_band_t *band = echoy_band_create();
    CHECK(band);
    echoy_plan_t remote = remote_plan(0x1128);
    CHECK(echoy_band_set_upstream(band, true, &remote, true, 200000, true, 0));
    echoy_band_job_t *old = echoy_band_take_job(band);
    CHECK(old);
    CHECK(echoy_band_set_upstream(band, false, NULL, false, 0, false, 1));
    worker(old, 1); /* The worker still sees physical B despite live detach. */
    CHECK(!echoy_band_finish_job(band, old, 2));
    CHECK(synthesis.live == 0);
    remote = remote_plan(0x0041);
    CHECK(echoy_band_set_upstream(band, true, &remote, true, 200000, true, 3));
    prepare(band, 3, 1);
    CHECK(synthesis.live == 1);
    CHECK(view(band, ECHOY_TILE_A).source == ECHOY_TILE_B);
    destroy(band);
}

static void pc_job_keeps_snapshot_after_physical_join(void)
{
    echoy_band_t *band = echoy_band_create();
    CHECK(band);
    CHECK(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_PRESENT, 1, 0));
    CHECK(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_EXAMPLE, 0, 0));
    echoy_band_job_t *old = echoy_band_take_job(band);
    CHECK(old);
    echoy_plan_t remote = remote_plan(0x1128);
    CHECK(echoy_band_set_upstream(band, true, &remote, true, 200000, true, 1));
    worker(old, 1); /* An earlier real B job must still synthesize its own B. */
    CHECK(synthesis.by_role[ECHOY_HARMONY] == 1);
    CHECK(!echoy_band_finish_job(band, old, 2));
    CHECK(synthesis.live == 0);
    prepare(band, 3, 1);
    CHECK(synthesis.live == 1);
    CHECK(synthesis.by_role[ECHOY_PULSE] == 1); /* Only audible A. */
    destroy(band);
}

static void reconnect_rejects_old_job_and_releases_replaced_banks(void)
{
    echoy_band_t *band = echoy_band_create();
    CHECK(band);
    echoy_plan_t remote = remote_plan(0x1128);
    CHECK(echoy_band_set_upstream(band, true, &remote, true, 200000, true, 0));
    prepare(band, 0, 1);
    CHECK(synthesis.live == 1);
    remote = remote_plan(0x8800);
    CHECK(echoy_band_set_upstream(band, true, &remote, true, 200000, true, 5100000));
    echoy_band_job_t *old = echoy_band_take_job(band);
    CHECK(old);
    CHECK(echoy_band_set_upstream(band, false, NULL, false, 0, false, 5100001));
    remote = remote_plan(0x0041);
    CHECK(echoy_band_set_upstream(band, true, &remote, true, 200000, true, 5100002));
    worker(old, 1);
    CHECK(synthesis.live == 2);
    CHECK(!echoy_band_finish_job(band, old, 5100003));
    CHECK(synthesis.live == 1);
    prepare(band, 5100004, 1);
    CHECK(synthesis.live == 1 && synthesis.released == 2);
    echoy_control_view_t joined = view(band, ECHOY_TILE_A);
    CHECK(joined.plan.source_count == 2);
    CHECK(joined.plan.source[0].step == 0 && joined.plan.source[1].step == 6);
    CHECK(echoy_band_set_upstream(band, false, NULL, false, 0, false, 11000000));
    prepare(band, 11000000, 1);
    CHECK(synthesis.live == 1);
    CHECK(view(band, ECHOY_TILE_A).source == ECHOY_TILE_NONE);
    destroy(band);
}

static void physical_identity_releases_old_b_bank_without_plan_change(void)
{
    echoy_band_t *band = echoy_band_create();
    CHECK(band);
    CHECK(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_PRESENT, 1, 0));
    CHECK(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_EXAMPLE, 0, 0));
    prepare(band, 0, 1);
    CHECK(synthesis.live == 1);
    echoy_plan_t same = view(band, ECHOY_TILE_B).plan;
    CHECK(echoy_band_set_upstream(band, true, &same, true, 200000, false, 5100000));
    echoy_arrange_job_t *compiled = calloc(1, sizeof(*compiled));
    CHECK(compiled && echoy_arrange_compile(echoy_band_arrangement(band), compiled));
    CHECK(compiled->changed_mask == (1u << ECHOY_TILE_A));
    free(compiled);
    prepare(band, 5100000, 1);
    CHECK(synthesis.released == 1); /* Obsolete B storage is not retained. */
    CHECK(synthesis.live == 1);
    CHECK(view(band, ECHOY_TILE_B).plan.count == same.count);
    destroy(band);
}

int main(int argc, char **argv)
{
    const struct { const char *name; void (*run)(void); } tests[] = {
        {"physical-replica", physical_replica_has_no_audio_bank},
        {"pc-three-banks", pc_keeps_all_three_audio_banks},
        {"physical-job-after-detach", physical_job_keeps_snapshot_after_detach},
        {"pc-job-after-physical-join", pc_job_keeps_snapshot_after_physical_join},
        {"reconnect-releases-banks", reconnect_rejects_old_job_and_releases_replaced_banks},
        {"old-b-bank-no-plan-change", physical_identity_releases_old_b_bank_without_plan_change},
    };
    unsigned ran = 0;
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (argc > 1 && strcmp(argv[1], tests[i].name)) continue;
        memset(&synthesis, 0, sizeof(synthesis));
        test_name = tests[i].name;
        tests[i].run();
        printf("PASS %s\n", test_name);
        ++ran;
    }
    CHECK(ran);
    printf("PASS %u upstream synthesis cases, %u assertions\n", ran, assertions);
    return 0;
}
