#include "echoy_band.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real owner/worker calls with a simulated monotonic timeline. No device,
 * transport, model or synthesis substitute participates in these tests. */
#define BAR INT64_C(2500000)
#define STEP INT64_C(25000)
#define OWN_SEED UINT16_C(0x0104) /* Initial silence, then steps 2 and 8. */
static const char *current_test;
static unsigned checks;
#define CHECK(test) do { ++checks; if (!(test)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", current_test, __LINE__, #test); exit(1); \
} } while (0)

static echoy_control_view_t view(echoy_band_t *band, echoy_tile_id_t tile)
{
    echoy_control_view_t result;
    CHECK(echoy_band_read(band, tile, &result));
    return result;
}

/* New edges can be recognized without producing a synthesis job. Drain only
 * jobs that actually exist, and bound all owner advancement. */
static void owner(echoy_band_t *band, int64_t now)
{
    for (unsigned i = 0; i < 16; ++i) {
        echoy_band_tick(band, now);
        echoy_band_job_t *job = echoy_band_take_job(band);
        if (!job) return;
        CHECK(echoy_band_prepare_job(job));
        CHECK(echoy_band_finish_job(band, job, now));
    }
    CHECK(!"owner failed to drain finite jobs");
}

static void advance(echoy_band_t *band, int64_t *now, int64_t target)
{
    CHECK(target >= *now);
    while (*now < target) {
        *now = target - *now > STEP ? *now + STEP : target;
        owner(band, *now);
    }
}

static echoy_band_t *fresh(int64_t *now)
{
    echoy_band_t *band = echoy_band_create();
    CHECK(band);
    *now = 0;
    owner(band, *now);
    advance(band, now, 100000);
    CHECK(view(band, ECHOY_TILE_A).state.phase == ECHOY_IDLE);
    return band;
}

static echoy_plan_t upstream(uint16_t seed)
{
    echoy_plan_t plan;
    CHECK(echoy_compose_mask(seed, ECHOY_VOICE, 2, .5, &plan));
    return plan;
}

static uint16_t root_seed(const echoy_plan_t *plan)
{
    uint16_t mask = 0;
    for (size_t i = 0; i < plan->source_count; ++i)
        mask |= (uint16_t)(1u << (unsigned)plan->source[i].step);
    return mask;
}

static double energy(echoy_band_t *band, int64_t at, size_t frames)
{
    float *pcm = calloc(frames, sizeof(*pcm));
    CHECK(pcm);
    echoy_band_render(band, at, pcm, frames);
    double power = 0;
    for (size_t i = 0; i < frames; ++i) {
        CHECK(isfinite(pcm[i]));
        power += (double)pcm[i] * pcm[i];
    }
    free(pcm);
    return power;
}

static int64_t record(echoy_band_t *band, echoy_tile_id_t tile, int64_t now)
{
    CHECK(echoy_band_action(band, tile, ECHOY_ACT_RECORD, 0, now));
    echoy_control_view_t v = view(band, tile);
    CHECK(v.state.phase == ECHOY_COUNT_IN);
    CHECK(v.state.capture_at_us == now + 3060000);
    return v.state.capture_at_us;
}

static void tap(echoy_band_t *band, echoy_tile_id_t tile, int64_t *now, int64_t at)
{
    advance(band, now, at);
    CHECK(echoy_band_action(band, tile, ECHOY_ACT_TAP, 0, at));
}

static void attach(echoy_band_t *band, const echoy_plan_t *plan, int64_t now)
{
    CHECK(echoy_band_set_upstream(band, true, plan, true, 200000, true, now));
}

static void deferred_unchanged(echoy_band_t *band, echoy_tile_id_t tile,
                               echoy_control_view_t before)
{
    echoy_control_view_t after = view(band, tile);
    CHECK(after.state.phase == before.state.phase);
    CHECK(after.state.generation == before.state.generation);
    CHECK(after.state.capture_at_us == before.state.capture_at_us);
    CHECK(after.state.pending_seed == before.state.pending_seed);
    CHECK(after.cooperation == ECHOY_COOP_DEFERRED);
    CHECK(!after.state.following);
    CHECK(!after.state.continuous);
}

static int64_t await_reply(echoy_band_t *band, echoy_tile_id_t tile, int64_t *now)
{
    for (unsigned i = 0; i < 240; ++i) {
        owner(band, *now);
        echoy_control_view_t v = view(band, tile);
        if (v.state.phase == ECHOY_PREVIEW) {
            CHECK(!v.state.continuous);
            CHECK(root_seed(&v.plan) == OWN_SEED);
            CHECK(v.source == ECHOY_TILE_NONE);
            return v.state.start_us;
        }
        advance(band, now, *now + STEP);
    }
    CHECK(!"recorded phrase did not reach one-shot reply");
    return 0;
}

static void follow_silently(echoy_band_t *band, echoy_tile_id_t tile,
                            int64_t *now, uint16_t expected_seed, bool waiting)
{
    advance(band, now, *now + 2 * BAR);
    echoy_control_view_t v = view(band, tile);
    CHECK(v.state.phase == ECHOY_IDLE);
    CHECK(!v.state.continuous);
    CHECK(v.state.following);
    CHECK(v.cooperation == (waiting ? ECHOY_COOP_WAITING : ECHOY_COOP_FOLLOWING));
    CHECK(root_seed(&v.plan) == expected_seed);
    CHECK(!echoy_band_action(band, tile, ECHOY_ACT_RECORD, 0, *now));
    CHECK(energy(band, *now, 12000) == 0);
    *now += 500000;
    CHECK(echoy_band_action(band, tile, ECHOY_ACT_TRANSPORT, 0, *now) == (expected_seed != 0));
    if (expected_seed) {
        owner(band, *now);
        v = view(band, tile);
        CHECK(v.state.phase == ECHOY_PLAYING && v.state.continuous);
        CHECK(echoy_band_action(band, tile, ECHOY_ACT_PAUSE, 0, *now));
    }
}

static void no_reply_after_explicit_stop(echoy_band_t *band, echoy_tile_id_t tile,
                                        int64_t *now)
{
    for (unsigned i = 0; i < 200; ++i) {
        advance(band, now, *now + STEP);
        echoy_control_view_t v = view(band, tile);
        CHECK(v.state.phase == ECHOY_IDLE);
        CHECK(!v.state.continuous);
    }
}

/* A new physical neighbor must not cancel any part of a local recording cycle,
 * including a synthesis job or the original, finite automatic reply. */
static void physical_join_during_cycle(unsigned stage)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    echoy_plan_t remote = upstream(0x0041);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    if (stage == 0) {
        advance(band, &now, now + 300000);
        echoy_control_view_t before = view(band, ECHOY_TILE_A);
        attach(band, &remote, now);
        deferred_unchanged(band, ECHOY_TILE_A, before);
    }
    tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
    if (stage == 1) {
        echoy_control_view_t before = view(band, ECHOY_TILE_A);
        CHECK(before.state.pending_seed == 0x0004);
        attach(band, &remote, now);
        deferred_unchanged(band, ECHOY_TILE_A, before);
    }
    tap(band, ECHOY_TILE_A, &now, capture_at + 1250000);
    CHECK(view(band, ECHOY_TILE_A).state.pending_seed == OWN_SEED);
    advance(band, &now, capture_at + BAR - 1);
    CHECK(view(band, ECHOY_TILE_A).state.pending_seed == OWN_SEED);
    now = capture_at + BAR;
    echoy_band_tick(band, now); /* Leave the immutable job on its worker boundary. */
    CHECK(view(band, ECHOY_TILE_A).state.phase == ECHOY_PREPARING);
    if (stage == 2) {
        echoy_control_view_t before = view(band, ECHOY_TILE_A);
        attach(band, &remote, now);
        deferred_unchanged(band, ECHOY_TILE_A, before);
    }
    int64_t start = await_reply(band, ECHOY_TILE_A, &now);
    if (stage == 3) {
        advance(band, &now, start + 100000);
        echoy_control_view_t before = view(band, ECHOY_TILE_A);
        attach(band, &remote, now);
        deferred_unchanged(band, ECHOY_TILE_A, before);
    }
    CHECK(view(band, ECHOY_TILE_A).cooperation == ECHOY_COOP_DEFERRED);
    CHECK(energy(band, start + 150000, 1200) == 0); /* Keep the captured initial silence. */
    CHECK(energy(band, start + 312500, 12000) > 1e-6);
    if (now < start + 812500) now = start + 812500;
    advance(band, &now, start + BAR - 1);
    echoy_control_view_t reply = view(band, ECHOY_TILE_A);
    CHECK(reply.state.phase == ECHOY_PREVIEW);
    CHECK(reply.source == ECHOY_TILE_NONE);
    CHECK(root_seed(&reply.plan) == OWN_SEED);
    follow_silently(band, ECHOY_TILE_A, &now, 0x0041, false);
    echoy_band_delete(band);
}

/* Explicit cancellation ends only the local operation. A still-recognized
 * neighbor may then be admitted, but cannot grant continuous playback. */
static void cancel_or_pause_deferred(unsigned stage)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    echoy_plan_t remote = upstream(0x0041);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    attach(band, &remote, now);
    if (stage > 0) {
        tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
        CHECK(view(band, ECHOY_TILE_A).state.pending_seed == 0x0004);
    }
    if (stage > 1) {
        tap(band, ECHOY_TILE_A, &now, capture_at + 1250000);
        now = capture_at + BAR;
        echoy_band_tick(band, now);
        CHECK(view(band, ECHOY_TILE_A).state.phase == ECHOY_PREPARING);
    }
    if (stage > 2) {
        int64_t start = await_reply(band, ECHOY_TILE_A, &now);
        advance(band, &now, start + 200000);
    }
    echoy_control_view_t before = view(band, ECHOY_TILE_A);
    CHECK(before.cooperation == ECHOY_COOP_DEFERRED);
    echoy_band_job_t *old_job = stage == 2 ? echoy_band_take_job(band) : NULL;
    if (stage == 2) CHECK(old_job);
    echoy_action_t action = stage < 2 ? ECHOY_ACT_CANCEL_RECORD : ECHOY_ACT_PAUSE;
    CHECK(echoy_band_action(band, ECHOY_TILE_A, action, 0, now));
    echoy_control_view_t cancelled = view(band, ECHOY_TILE_A);
    CHECK(cancelled.state.phase == ECHOY_IDLE);
    CHECK(cancelled.state.generation != before.state.generation);
    CHECK(cancelled.state.pending_seed == 0);
    if (stage < 2) CHECK(cancelled.state.notice == ECHOY_NOTICE_CANCELLED);
    CHECK(!echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_CANCEL_RECORD, 0, now));
    if (old_job) {
        CHECK(echoy_band_prepare_job(old_job));
        CHECK(!echoy_band_finish_job(band, old_job, now));
    }
    no_reply_after_explicit_stop(band, ECHOY_TILE_A, &now);
    follow_silently(band, ECHOY_TILE_A, &now, 0x0041, false);
    echoy_band_delete(band);
}

/* Removing the deferred join must leave the captured input and original
 * reply intact; reconnecting before that reply ends must defer again. */
static void detach_or_rejoin_deferred(unsigned rejoin)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    echoy_plan_t old_remote = upstream(0x0041);
    echoy_plan_t new_remote = upstream(0x8800);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
    attach(band, &old_remote, now);
    echoy_control_view_t before = view(band, ECHOY_TILE_A);
    CHECK(before.cooperation == ECHOY_COOP_DEFERRED);
    CHECK(echoy_band_set_upstream(band, false, NULL, false, 0, false, now));
    echoy_control_view_t detached = view(band, ECHOY_TILE_A);
    CHECK(detached.state.phase == ECHOY_RECORDING);
    CHECK(detached.state.generation == before.state.generation);
    CHECK(detached.state.pending_seed == 0x0004);
    CHECK(detached.cooperation == ECHOY_COOP_SOLO);
    CHECK(!detached.state.following);
    if (rejoin) {
        attach(band, NULL, now);
        deferred_unchanged(band, ECHOY_TILE_A, detached);
        if (rejoin == 2) attach(band, &new_remote, now);
    }
    tap(band, ECHOY_TILE_A, &now, capture_at + 1250000);
    CHECK(view(band, ECHOY_TILE_A).state.pending_seed == OWN_SEED);
    advance(band, &now, capture_at + BAR);
    int64_t start = await_reply(band, ECHOY_TILE_A, &now);
    CHECK(energy(band, start + 312500, 12000) > 1e-6);
    now = start + 812500;
    advance(band, &now, start + BAR);
    if (rejoin) {
        /* NULL on the new connection must not revive the old 0x0041 source. */
        follow_silently(band, ECHOY_TILE_A, &now,
                        rejoin == 2 ? 0x8800 : OWN_SEED, rejoin == 1);
    } else {
        advance(band, &now, now + BAR);
        echoy_control_view_t completed = view(band, ECHOY_TILE_A);
        CHECK(completed.state.phase == ECHOY_IDLE);
        CHECK(completed.cooperation == ECHOY_COOP_SOLO);
        CHECK(completed.source == ECHOY_TILE_NONE);
        CHECK(root_seed(&completed.plan) == OWN_SEED);
        CHECK(energy(band, now, 12000) == 0);
        now += 500000;
        CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, now));
    }
    echoy_band_delete(band);
}

static void empty_capture_joins_without_reply(void)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    echoy_plan_t remote = upstream(0x0041);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    attach(band, &remote, now);
    advance(band, &now, capture_at + BAR - 1);
    echoy_control_view_t empty = view(band, ECHOY_TILE_A);
    CHECK(empty.state.phase == ECHOY_RECORDING && empty.state.pending_seed == 0);
    CHECK(empty.cooperation == ECHOY_COOP_DEFERRED);
    advance(band, &now, capture_at + BAR);
    CHECK(view(band, ECHOY_TILE_A).state.phase == ECHOY_IDLE);
    CHECK(view(band, ECHOY_TILE_A).state.notice == ECHOY_NOTICE_EMPTY);
    no_reply_after_explicit_stop(band, ECHOY_TILE_A, &now);
    follow_silently(band, ECHOY_TILE_A, &now, 0x0041, false);
    echoy_band_delete(band);
}

static void both_empty_wait_for_phrase(void)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    attach(band, NULL, now);
    echoy_control_view_t waiting = view(band, ECHOY_TILE_A);
    CHECK(waiting.cooperation == ECHOY_COOP_WAITING);
    CHECK(waiting.state.following);
    CHECK(waiting.plan.count == 0);
    CHECK(!echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, now));
    CHECK(!echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, now));
    follow_silently(band, ECHOY_TILE_A, &now, 0, true);
    echoy_band_delete(band);
}

/* Completion from a previous attachment cannot be accepted after detach and a
 * new empty attachment. Only a snapshot from the new attachment may supply it. */
static void reject_disconnected_preparation(void)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    echoy_plan_t old_remote = upstream(0x0041);
    echoy_plan_t latest = upstream(0x8800);
    attach(band, &old_remote, now);
    echoy_band_job_t *old_job = echoy_band_take_job(band);
    CHECK(old_job);
    CHECK(echoy_band_set_upstream(band, false, NULL, false, 0, false, now));
    attach(band, NULL, now);
    CHECK(echoy_band_prepare_job(old_job));
    CHECK(!echoy_band_finish_job(band, old_job, now));
    follow_silently(band, ECHOY_TILE_A, &now, 0, true);
    attach(band, &latest, now);
    follow_silently(band, ECHOY_TILE_A, &now, 0x8800, false);
    echoy_band_delete(band);
}

/* Frozen ArrangeModel owns the current cooperative phrase after a split; the
 * earlier own seed is not an implicit restore target. */
static void detach_keeps_cooperative_phrase(void)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    echoy_plan_t remote = upstream(0x0041);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
    tap(band, ECHOY_TILE_A, &now, capture_at + 1250000);
    advance(band, &now, capture_at + BAR);
    int64_t start = await_reply(band, ECHOY_TILE_A, &now);
    advance(band, &now, start + BAR);
    attach(band, &remote, now);
    follow_silently(band, ECHOY_TILE_A, &now, 0x0041, false);
    echoy_control_view_t joined = view(band, ECHOY_TILE_A);
    CHECK(joined.source == ECHOY_TILE_B);
    CHECK(echoy_band_set_upstream(band, false, NULL, false, 0, false, now));
    advance(band, &now, now + 2 * BAR);
    echoy_control_view_t detached = view(band, ECHOY_TILE_A);
    CHECK(detached.cooperation == ECHOY_COOP_SOLO);
    CHECK(detached.source == ECHOY_TILE_NONE);
    CHECK(!detached.state.following && detached.state.phase == ECHOY_IDLE);
    CHECK(root_seed(&detached.plan) == 0x0041);
    CHECK(detached.plan.count == joined.plan.count);
    CHECK(!memcmp(detached.plan.events, joined.plan.events,
                  joined.plan.count * sizeof(joined.plan.events[0])));
    CHECK(energy(band, now, 12000) == 0);
    now += 500000;
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, now));
    echoy_band_delete(band);
}

/* Deferring an incoming A->B edge must not turn B's own capture/reply into
 * A's phrase, block A's existing loop or withhold the independent B->C edge. */
static void pc_chain_defers_only_capturing_receiver(void)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    CHECK(echoy_band_action(band, ECHOY_TILE_B, ECHOY_ACT_PRESENT, 1, now));
    CHECK(echoy_band_action(band, ECHOY_TILE_C, ECHOY_ACT_PRESENT, 1, now));
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_EXAMPLE, 0, now));
    advance(band, &now, now + 2 * BAR);
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, now));
    owner(band, now);
    echoy_control_view_t playing = view(band, ECHOY_TILE_A);
    CHECK(playing.state.phase == ECHOY_PLAYING);
    advance(band, &now, playing.state.start_us);
    CHECK(energy(band, now, 12000) > 1e-6);
    now += 500000;
    int64_t capture_at = record(band, ECHOY_TILE_B, now);
    tap(band, ECHOY_TILE_B, &now, capture_at + 312500);
    echoy_control_view_t before = view(band, ECHOY_TILE_B);
    const echoy_arrange_edge_t chain[] = {
        {ECHOY_TILE_A, ECHOY_TILE_B}, {ECHOY_TILE_B, ECHOY_TILE_C}
    };
    CHECK(echoy_band_set_edges(band, chain, 2, now));
    deferred_unchanged(band, ECHOY_TILE_B, before);
    owner(band, now);
    CHECK(view(band, ECHOY_TILE_A).state.phase == ECHOY_PLAYING);
    CHECK(view(band, ECHOY_TILE_A).state.generation == playing.state.generation);
    CHECK(view(band, ECHOY_TILE_C).cooperation == ECHOY_COOP_WAITING);
    CHECK(!echoy_band_action(band, ECHOY_TILE_C, ECHOY_ACT_RECORD, 0, now));
    tap(band, ECHOY_TILE_B, &now, capture_at + 1250000);
    CHECK(view(band, ECHOY_TILE_B).state.pending_seed == OWN_SEED);
    advance(band, &now, capture_at + BAR);
    int64_t start = await_reply(band, ECHOY_TILE_B, &now);
    echoy_control_view_t reply = view(band, ECHOY_TILE_B);
    CHECK(reply.cooperation == ECHOY_COOP_DEFERRED);
    CHECK(reply.clock == ECHOY_TILE_B);
    CHECK(reply.state.clock_origin_us != playing.state.clock_origin_us);
    CHECK(view(band, ECHOY_TILE_A).state.phase == ECHOY_PLAYING);
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, now));
    CHECK(energy(band, start + 150000, 1200) == 0);
    CHECK(energy(band, start + 312500, 12000) > 1e-6);
    now = start + 812500;
    advance(band, &now, start + BAR - 1);
    CHECK(view(band, ECHOY_TILE_B).state.phase == ECHOY_PREVIEW);
    follow_silently(band, ECHOY_TILE_B, &now, 5, false);
    echoy_control_view_t right = view(band, ECHOY_TILE_C);
    CHECK(right.state.phase == ECHOY_IDLE && !right.state.continuous);
    CHECK(right.state.following && right.source == ECHOY_TILE_B);
    CHECK(root_seed(&right.plan) == 5);
    echoy_band_delete(band);
}

static echoy_link_update_t follower_message(echoy_link_command_t command, uint32_t transaction)
{
    return (echoy_link_update_t){.attached=true, .following=true,
        .connection_generation=1, .session=17, .transaction=transaction,
        .command=command, .clock_started=true, .clock_origin_us=200000};
}

static echoy_link_view_t link_view(echoy_band_t *band)
{
    echoy_link_view_t result;
    CHECK(echoy_band_link_read(band, &result));
    return result;
}

/* A network offer received while recording is outside the shared transaction.
 * Its aborted payload must not become current when the local reply ends. */
static void aborted_deferred_offer_cannot_join(void)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
    echoy_control_view_t before = view(band, ECHOY_TILE_A);
    echoy_link_update_t message = follower_message(ECHOY_LINK_OFFER, 11);
    message.valid = true;
    message.plan = upstream(0x0041);
    CHECK(echoy_band_link_update(band, &message, now));
    deferred_unchanged(band, ECHOY_TILE_A, before);
    message.command = ECHOY_LINK_ABORT;
    CHECK(echoy_band_link_update(band, &message, now));
    deferred_unchanged(band, ECHOY_TILE_A, before);
    tap(band, ECHOY_TILE_A, &now, capture_at + 1250000);
    advance(band, &now, capture_at + BAR);
    int64_t start = await_reply(band, ECHOY_TILE_A, &now);
    CHECK(energy(band, start + 312500, 12000) > 1e-6);
    now = start + 812500;
    advance(band, &now, start + BAR + 2 * BAR);
    echoy_control_view_t after = view(band, ECHOY_TILE_A);
    CHECK(after.state.phase == ECHOY_IDLE && !after.state.continuous);
    CHECK(root_seed(&after.plan) == OWN_SEED);
    CHECK(after.source == ECHOY_TILE_NONE);
    CHECK(after.cooperation == ECHOY_COOP_WAITING);
    CHECK(link_view(band).committed_transaction == 0);
    CHECK(energy(band, now, 12000) == 0);
    echoy_band_delete(band);
}

static void deferred_abort_is_idempotent(void)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
    echoy_control_view_t before = view(band, ECHOY_TILE_A);
    echoy_link_update_t message = follower_message(ECHOY_LINK_OFFER, 11);
    message.valid = true;
    message.plan = upstream(0x0041);
    CHECK(echoy_band_link_update(band, &message, now));
    message.command = ECHOY_LINK_ABORT;
    CHECK(echoy_band_link_update(band, &message, now));
    CHECK(echoy_band_link_update(band, &message, now));
    deferred_unchanged(band, ECHOY_TILE_A, before);
    CHECK(view(band, ECHOY_TILE_A).state.pending_seed == 0x0004);
    echoy_band_delete(band);
}

static echoy_band_t *playing_example(int64_t *now)
{
    echoy_band_t *band = fresh(now);
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_EXAMPLE, 0, *now));
    advance(band, now, *now + 2 * BAR);
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, *now));
    owner(band, *now);
    echoy_control_view_t playing = view(band, ECHOY_TILE_A);
    CHECK(playing.state.phase == ECHOY_PLAYING);
    advance(band, now, playing.state.start_us);
    return band;
}

/* A retransmitted ABORT belongs to the old network offer, not to a later
 * explicit local edit. It cannot invalidate that edit's synthesis job. */
static void duplicate_abort_preserves_new_local_job(void)
{
    int64_t now;
    echoy_band_t *band = playing_example(&now);
    echoy_link_update_t message = follower_message(ECHOY_LINK_OFFER, 11);
    message.valid = true;
    message.plan = upstream(0x0041);
    CHECK(echoy_band_link_update(band, &message, now));
    owner(band, now);
    CHECK(link_view(band).offer_ready);
    message.command = ECHOY_LINK_ABORT;
    CHECK(echoy_band_link_update(band, &message, now));
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_DENSITY, .8, now));
    echoy_band_job_t *job = echoy_band_take_job(band);
    CHECK(job);
    CHECK(echoy_band_prepare_job(job));
    CHECK(echoy_band_link_update(band, &message, now));
    CHECK(echoy_band_finish_job(band, job, now));
    advance(band, &now, now + 2 * BAR);
    echoy_control_view_t edited = view(band, ECHOY_TILE_A);
    CHECK(edited.state.phase == ECHOY_PLAYING);
    CHECK(edited.plan.density == .8);
    CHECK(root_seed(&edited.plan) == 5);
    echoy_band_delete(band);
}

static void commit_offer(echoy_band_t *band, echoy_link_update_t *message, int64_t *now)
{
    owner(band, *now);
    echoy_link_view_t ready = link_view(band);
    CHECK(ready.offer_ready && ready.offered_transaction == message->transaction);
    message->revision = ready.offered_revision;
    message->commit_at_us = 200000 + ((*now + 1000000 - 200000 + BAR - 1) / BAR) * BAR;
    message->command = ECHOY_LINK_ARM;
    CHECK(echoy_band_link_update(band, message, *now));
    message->command = ECHOY_LINK_COMMIT;
    CHECK(echoy_band_link_update(band, message, *now));
    advance(band, now, message->commit_at_us);
    CHECK(link_view(band).committed_transaction == message->transaction);
}

/* Finishing or explicitly stopping a deferred operation only admits the
 * receiver: the offered music still needs owner READY and ARM/COMMIT. */
static void deferred_offer_requires_commit(bool cancel)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
    echoy_link_update_t message = follower_message(ECHOY_LINK_OFFER, 11);
    message.valid = true;
    message.plan = upstream(0x0041);
    CHECK(echoy_band_link_update(band, &message, now));
    CHECK(!link_view(band).offer_ready);
    if (cancel) {
        CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_CANCEL_RECORD, 0, now));
        no_reply_after_explicit_stop(band, ECHOY_TILE_A, &now);
    } else {
        tap(band, ECHOY_TILE_A, &now, capture_at + 1250000);
        advance(band, &now, capture_at + BAR);
        int64_t start = await_reply(band, ECHOY_TILE_A, &now);
        advance(band, &now, start + BAR + 2 * BAR);
    }
    echoy_control_view_t waiting = view(band, ECHOY_TILE_A);
    CHECK(waiting.state.phase == ECHOY_IDLE && !waiting.state.continuous);
    CHECK(root_seed(&waiting.plan) == (cancel ? 0 : OWN_SEED));
    CHECK(link_view(band).committed_transaction == 0);
    CHECK(link_view(band).offer_ready);
    commit_offer(band, &message, &now);
    echoy_control_view_t joined = view(band, ECHOY_TILE_A);
    CHECK(root_seed(&joined.plan) == 0x0041);
    CHECK(joined.state.phase == ECHOY_IDLE && !joined.state.continuous);
    CHECK(energy(band, now, 12000) == 0);
    now += 500000;
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, now));
    owner(band, now);
    CHECK(view(band, ECHOY_TILE_A).state.phase == ECHOY_PLAYING);
    echoy_band_delete(band);
}

static void completed_deferred_offer_requires_commit(void)
{ deferred_offer_requires_commit(false); }
static void cancelled_deferred_offer_requires_commit(void)
{ deferred_offer_requires_commit(true); }

static void link_rejoin_rejects_old_session(void)
{
    int64_t now;
    echoy_band_t *band = fresh(&now);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
    echoy_link_update_t old = follower_message(ECHOY_LINK_OFFER, 11);
    old.valid = true;
    old.plan = upstream(0x0041);
    CHECK(echoy_band_link_update(band, &old, now));
    echoy_link_update_t message = old;
    message.attached = false;
    message.command = ECHOY_LINK_NONE;
    message.connection_generation = 2;
    CHECK(echoy_band_link_update(band, &message, now));
    echoy_control_view_t detached = view(band, ECHOY_TILE_A);
    CHECK(detached.cooperation == ECHOY_COOP_SOLO);
    CHECK(detached.state.phase == ECHOY_RECORDING && detached.state.pending_seed == 0x0004);
    message = follower_message(ECHOY_LINK_NONE, 0);
    message.connection_generation = 3;
    message.session = 29;
    CHECK(echoy_band_link_update(band, &message, now));
    deferred_unchanged(band, ECHOY_TILE_A, detached);
    CHECK(!echoy_band_link_update(band, &old, now));
    old.command = ECHOY_LINK_ABORT;
    CHECK(!echoy_band_link_update(band, &old, now));
    tap(band, ECHOY_TILE_A, &now, capture_at + 1250000);
    advance(band, &now, capture_at + BAR);
    int64_t start = await_reply(band, ECHOY_TILE_A, &now);
    advance(band, &now, start + BAR);
    follow_silently(band, ECHOY_TILE_A, &now, OWN_SEED, true);
    CHECK(link_view(band).offered_transaction == 0);
    echoy_band_delete(band);
}

static void failed_replacement_keeps_old_pcm(void)
{
    int64_t now;
    echoy_band_t *band = playing_example(&now);
    echoy_control_view_t original = view(band, ECHOY_TILE_A);
    echoy_audio_loops_t *expected = NULL;
    CHECK(echoy_audio_prepare(&original.plan, true, &expected) == ECHOY_AUDIO_OK);
    float baseline[6000];
    echoy_band_render(band, now, baseline, 6000);
    for (unsigned i = 0; i < 6000; ++i) {
        int64_t at = now + (int64_t)i * 1000000 / ECHOY_AUDIO_SAMPLE_RATE;
        uint64_t sample = ((uint64_t)(at - original.state.start_us) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
        CHECK(fabs(baseline[i] - expected->samples[ECHOY_AUDIO_FULL][sample % expected->frames] * .65 * .45) < 1e-6);
    }
    now += 250000;
    echoy_link_update_t message = follower_message(ECHOY_LINK_OFFER, 11);
    message.valid = true;
    message.plan = upstream(0x0041);
    CHECK(echoy_band_link_update(band, &message, now));
    owner(band, now);
    echoy_link_view_t ready = link_view(band);
    CHECK(ready.offer_ready);
    CHECK(echoy_band_action(band, ECHOY_TILE_A, ECHOY_ACT_DENSITY, .8, now));
    CHECK(!link_view(band).offer_ready);
    message.command = ECHOY_LINK_ARM;
    message.revision = ready.offered_revision;
    message.commit_at_us = now + BAR;
    CHECK(!echoy_band_link_update(band, &message, now));
    echoy_band_job_t *replacement = echoy_band_take_job(band);
    CHECK(replacement);
    CHECK(!echoy_band_finish_job(band, replacement, now)); /* Worker reports failure. */
    CHECK(link_view(band).offer_failed);
    message.command = ECHOY_LINK_ABORT;
    message.failed = true;
    CHECK(echoy_band_link_update(band, &message, now));
    advance(band, &now, now + BAR);
    echoy_control_view_t still_playing = view(band, ECHOY_TILE_A);
    CHECK(still_playing.state.phase == ECHOY_PLAYING && still_playing.state.continuous);
    CHECK(still_playing.state.generation == original.state.generation);
    CHECK(root_seed(&still_playing.plan) == 5 && still_playing.plan.density == .5);
    float pcm[6000];
    echoy_band_render(band, now, pcm, 6000);
    double power = 0;
    for (unsigned i = 0; i < 6000; ++i) {
        int64_t at = now + (int64_t)i * 1000000 / ECHOY_AUDIO_SAMPLE_RATE;
        uint64_t sample = ((uint64_t)(at - original.state.start_us) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
        double want = expected->samples[ECHOY_AUDIO_FULL][sample % expected->frames] * .65 * .45;
        if (fabs(pcm[i] - want) >= 1e-6)
            fprintf(stderr, "PCM mismatch sample=%u at=%lld start=%lld current_start=%lld got=%.9f want=%.9f\n",
                    i, (long long)now, (long long)original.state.start_us,
                    (long long)still_playing.state.start_us, pcm[i], want);
        CHECK(fabs(pcm[i] - want) < 1e-6);
        power += (double)pcm[i] * pcm[i];
    }
    CHECK(power > 1e-6);
    echoy_audio_free_loops(expected);
    echoy_band_delete(band);
}

/* A failed shared preparation cannot reply to a new recording using the
 * previously prepared bank. Existing PLAYING audio has a separate contract. */
static void leader_abort_cannot_reply_with_old_bank(void)
{
    int64_t now;
    echoy_band_t *band = playing_example(&now);
    int64_t capture_at = record(band, ECHOY_TILE_A, now);
    echoy_link_update_t message = follower_message(ECHOY_LINK_NONE, 0);
    message.following = false;
    message.hold_changes = true;
    CHECK(echoy_band_link_update(band, &message, now));
    tap(band, ECHOY_TILE_A, &now, capture_at + 312500);
    tap(band, ECHOY_TILE_A, &now, capture_at + 1250000);
    advance(band, &now, capture_at + BAR);
    CHECK(view(band, ECHOY_TILE_A).state.phase == ECHOY_PREPARING);
    echoy_link_view_t ready = link_view(band);
    CHECK(ready.proposal_ready && root_seed(&ready.proposal) == OWN_SEED);
    CHECK(root_seed(&ready.current) == 5);
    message.command = ECHOY_LINK_BEGIN;
    message.transaction = 11;
    message.revision = ready.proposal_revision;
    CHECK(echoy_band_link_update(band, &message, now));
    message.command = ECHOY_LINK_ABORT;
    message.failed = true;
    message.hold_changes = false;
    CHECK(echoy_band_link_update(band, &message, now));
    owner(band, now);
    echoy_control_view_t failed = view(band, ECHOY_TILE_A);
    double unexpected_power = energy(band, now + 60000, 12000);
    if (failed.state.phase != ECHOY_IDLE || unexpected_power != 0)
        fprintf(stderr, "Failed recording phase=%d old-bank PCM energy=%.9f\n",
                failed.state.phase, unexpected_power);
    CHECK(failed.state.phase == ECHOY_IDLE);
    CHECK(!failed.state.continuous && failed.preparation_failed);
    CHECK(unexpected_power == 0);
    CHECK(root_seed(&failed.plan) == 5);
    echoy_band_delete(band);
}

int main(int argc, char **argv)
{
    const char *names[] = {"physical-count-in", "physical-recording", "physical-preparing", "physical-reply"};
    unsigned ran = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (argc > 1 && strcmp(argv[1], names[i])) continue;
        current_test = names[i];
        physical_join_during_cycle(i);
        printf("PASS %s\n", current_test);
        ++ran;
    }
    const char *cancel_names[] = {"cancel-count-in", "cancel-recording", "pause-preparing", "pause-reply"};
    for (unsigned i = 0; i < 4; ++i) {
        if (argc > 1 && strcmp(argv[1], cancel_names[i])) continue;
        current_test = cancel_names[i];
        cancel_or_pause_deferred(i);
        printf("PASS %s\n", current_test);
        ++ran;
    }
    const char *detach_names[] = {"detach-deferred", "rejoin-empty-deferred", "rejoin-latest-deferred"};
    for (unsigned i = 0; i < 3; ++i) {
        if (argc > 1 && strcmp(argv[1], detach_names[i])) continue;
        current_test = detach_names[i];
        detach_or_rejoin_deferred(i);
        printf("PASS %s\n", current_test);
        ++ran;
    }
    const struct { const char *name; void (*run)(void); } cases[] = {
        {"empty-capture", empty_capture_joins_without_reply},
        {"both-empty", both_empty_wait_for_phrase},
        {"stale-disconnected-job", reject_disconnected_preparation},
        {"detach-keeps-cooperative", detach_keeps_cooperative_phrase},
        {"pc-chain", pc_chain_defers_only_capturing_receiver},
        {"link-abort-deferred", aborted_deferred_offer_cannot_join},
        {"link-abort-duplicate-deferred", deferred_abort_is_idempotent},
        {"link-abort-duplicate-local-job", duplicate_abort_preserves_new_local_job},
        {"link-completed-deferred-commit", completed_deferred_offer_requires_commit},
        {"link-cancelled-deferred-commit", cancelled_deferred_offer_requires_commit},
        {"link-rejoin-old-session", link_rejoin_rejects_old_session},
        {"link-failed-ready-keeps-pcm", failed_replacement_keeps_old_pcm},
        {"link-leader-abort-old-bank", leader_abort_cannot_reply_with_old_bank},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (argc > 1 && strcmp(argv[1], cases[i].name)) continue;
        current_test = cases[i].name;
        cases[i].run();
        printf("PASS %s\n", current_test);
        ++ran;
    }
    CHECK(ran != 0);
    printf("PASS %u deferred boundary cases, %u assertions\n", ran, checks);
    return 0;
}
