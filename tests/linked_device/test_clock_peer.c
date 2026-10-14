#include "echoy_band.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR INT64_C(2500000)
static echoy_plan_t upstream;
static echoy_control_view_t current;

static void read_local(echoy_band_t *b)
{
    assert(echoy_band_read(b, ECHOY_TILE_A, &current));
}

static void prepare(echoy_band_t *b, int64_t now)
{
    echoy_band_job_t *job = echoy_band_take_job(b);
    assert(job && echoy_band_prepare_job(job));
    assert(echoy_band_finish_job(b, job, now));
}

static double energy(echoy_band_t *b, int64_t now, float *pcm, size_t count)
{
    double sum = 0;
    echoy_band_render(b, now, pcm, count);
    for (size_t i = 0; i < count; ++i) {
        assert(isfinite(pcm[i])); sum += pcm[i] * pcm[i];
    }
    return sum;
}

static echoy_band_t *playing(void)
{
    echoy_band_t *b = echoy_band_create(); assert(b);
    prepare(b, 0); echoy_band_tick(b, 45000);
    assert(echoy_compose_mask(0xffff, ECHOY_VOICE, 1, .5, &upstream));
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, 50000));
    assert(echoy_band_set_upstream(b, true, &upstream, true, 200000, true, 1000000));
    prepare(b, 1000000); echoy_band_tick(b, 2700000);
    read_local(b); assert(current.state.phase == ECHOY_IDLE);
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 3210000));
    echoy_band_tick(b, 3210000);
    read_local(b); assert(current.state.phase == ECHOY_PLAYING && current.state.start_us == 5200000);
    return b;
}

static void pending_refresh(void)
{
    echoy_band_t *b = playing();
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 2, 5900000));
    echoy_band_job_t *job = echoy_band_take_job(b); assert(job);
    read_local(b); assert(current.state.plan_pending && !current.state.apply_at_us);
    assert(echoy_band_set_upstream(b, true, &upstream, true, 201000, true, 5901000));
    read_local(b); assert(current.state.plan_pending && !current.state.apply_at_us);
    assert(!echoy_band_take_job(b));
    assert(echoy_band_prepare_job(job) && echoy_band_finish_job(b, job, 5902000));
    read_local(b); assert(current.state.plan_pending && current.state.apply_at_us == 7701000);
    assert(echoy_band_set_upstream(b, true, &upstream, true, 201000, true, 5903000));
    read_local(b); assert(current.state.plan_pending && current.state.apply_at_us == 7701000);
    echoy_band_tick(b, 7701000);
    read_local(b); assert(!current.state.plan_pending && current.plan.phrase == 2);
    echoy_band_delete(b);
    puts("PASS periodic clock refresh preserves synthesizing and staged plan_pending");
}

static void pending_clock_alignment(void)
{
    echoy_band_t *b = playing();
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 2, 5900000));
    prepare(b, 5900000);
    read_local(b); assert(current.state.plan_pending && current.state.apply_at_us == 7700000);
    assert(echoy_band_set_upstream(b, true, &upstream, true, 220000, true, 6000000));
    read_local(b);
    assert(current.state.plan_pending);
    const echoy_arrange_pending_t *pending = echoy_arrange_get_pending(echoy_band_arrangement(b));
    assert(pending);
    int64_t phase = (pending->at_us - current.state.clock_origin_us) % BAR;
    fprintf(stderr, "staged boundary=%" PRId64 " synchronized origin=%" PRId64
        " boundary phase=%" PRId64 " us\n", pending->at_us, current.state.clock_origin_us, phase);
    /* Applying a freshly synthesized loop away from the new bar permanently
     * gives it a different phase from the upstream clock. */
    assert(phase == 0);
    echoy_band_delete(b);
    puts("PASS staged replacement stays aligned after a clock correction");
}

static void equivalent_origins(void)
{
    echoy_band_t *a = playing(), *b = playing();
    float pcm_a[480], pcm_b[480];
    assert(echoy_band_set_upstream(b, true, &upstream, true, 200000 + BAR, true, 5300000));
    assert(energy(a, 5300000, pcm_a, 480) > 0);
    assert(energy(b, 5300000, pcm_b, 480) > 0);
    assert(memcmp(pcm_a, pcm_b, sizeof(pcm_a)) == 0);
    echoy_band_delete(a); echoy_band_delete(b);
    puts("PASS whole-bar clock-origin correction leaves actual PCM unchanged");
}

static void positive_correction_does_not_suspend_playback(void)
{
    echoy_band_t *b = playing();
    float pcm[2400];
    assert(energy(b, 5200000, pcm, 2400) > 0);
    assert(echoy_band_set_upstream(b, true, &upstream, true, 500000, true, 5300000));
    read_local(b); assert(current.state.phase == ECHOY_PLAYING);
    double after = energy(b, 5300000, pcm, 2400);
    fprintf(stderr, "playing PCM energy after +300 ms clock correction: %.9g\n", after);
    /* A started loop must remain running; adjusting phase must not turn its
     * original start into a future start and insert silence. */
    assert(after > 0);
    echoy_band_delete(b);
    puts("PASS positive clock correction keeps already started PCM running");
}

static void pending_detach_keeps_committed_phrase(void)
{
    echoy_band_t *b = playing();
    static echoy_plan_t retained;
    read_local(b); retained = current.plan;
    assert(echoy_compose_mask(0x1128, ECHOY_HARMONY, 3, .8, &upstream));
    assert(echoy_band_set_upstream(b, true, &upstream, true, 200000, true, 5900000));
    /* Neither import nor a synthesis job may borrow the receive staging plan. */
    memset(&upstream, 0, sizeof(upstream));
    prepare(b, 5900000);
    read_local(b); assert(current.state.plan_pending);
    assert(echoy_band_set_upstream(b, false, NULL, false, 0, false, 6000000));
    read_local(b);
    assert(!current.state.following && current.cooperation == ECHOY_COOP_SOLO);
    assert(current.clock == ECHOY_TILE_A && current.source == ECHOY_TILE_NONE);
    assert(current.state.phase == ECHOY_PLAYING);
    assert(memcmp(&current.plan, &retained, sizeof(retained)) == 0);
    float pcm[2400];
    assert(energy(b, 6000000, pcm, 2400) > 0);
    prepare(b, 6000000); echoy_band_tick(b, 7700000);
    read_local(b);
    assert(!current.state.following && current.source == ECHOY_TILE_NONE);
    assert(current.state.phase == ECHOY_PLAYING && !current.state.plan_pending);
    assert(memcmp(&current.plan, &retained, sizeof(retained)) == 0);
    echoy_band_delete(b);
    puts("PASS pending detach retains exact committed phrase and import owns its data");
}

static void imminent_commit_survives_equivalent_clock(bool whole_bar)
{
    echoy_band_t *b = playing();
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 2, 5900000));
    prepare(b, 5900000);
    read_local(b); assert(current.state.plan_pending && current.state.apply_at_us == 7700000);
    /* A heartbeat 10 ms before the already reserved boundary needs no new
     * scheduling lead. Whole-bar origin changes describe the same clock. */
    int64_t origin = 200000 + (whole_bar ? BAR : 0);
    assert(echoy_band_set_upstream(b, true, &upstream, true, origin, true, 7690000));
    read_local(b);
    fprintf(stderr, "%s clock refresh changed imminent commit to %" PRId64 " us\n",
        whole_bar ? "whole-bar-equivalent" : "identical", current.state.apply_at_us);
    assert(current.state.plan_pending && current.state.apply_at_us == 7700000);
    echoy_band_tick(b, 7700000);
    read_local(b); assert(!current.state.plan_pending && current.plan.phrase == 2);
    echoy_band_delete(b);
    puts("PASS equivalent clock heartbeat preserves an imminent commit");
}

static void detached_replay_is_prompt(void)
{
    echoy_band_t *b = playing();
    assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, 6000000));
    assert(echoy_band_set_upstream(b, false, NULL, false, 0, false, 6010000));
    prepare(b, 6010000); echoy_band_tick(b, 6060000);
    for (int i=0; i<3; ++i) {
        int64_t now = 6100000 + i * 340000;
        assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, now));
        echoy_band_tick(b, now);
        read_local(b);
        assert(current.state.phase == ECHOY_PLAYING);
        assert(current.state.start_us == now + 50000);
        assert(echoy_band_action(b, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, now + 200000));
    }
    echoy_band_delete(b);
    puts("PASS independent replay ignores the former group and historical solo clock");
}

int main(int argc, char **argv)
{
    if (argc == 1 || !strcmp(argv[1], "pending")) pending_refresh();
    if (argc == 1 || !strcmp(argv[1], "equivalent")) equivalent_origins();
    if (argc == 1 || !strcmp(argv[1], "solo")) detached_replay_is_prompt();
    if (argc == 1 || !strcmp(argv[1], "detach")) pending_detach_keeps_committed_phrase();
    if (argc == 1 || !strcmp(argv[1], "alignment")) pending_clock_alignment();
    if (argc == 1 || !strcmp(argv[1], "running")) positive_correction_does_not_suspend_playback();
    if (argc == 1 || !strcmp(argv[1], "heartbeat")) imminent_commit_survives_equivalent_clock(false);
    if (argc == 1 || !strcmp(argv[1], "equivalent_pending")) imminent_commit_survives_equivalent_clock(true);
    return 0;
}
