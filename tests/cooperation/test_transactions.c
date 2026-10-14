/* Real board_link protocol + two independent band/arrange/model/synthesis/PCM
 * instances. Only external radio, OS, clock and physical sensors are fakes. */
#include "../board_link/fake_hardware.h"
#include "echoy_band.h"
#include <math.h>

#define BLOCK_FRAMES 240u
#define BLOCK_US INT64_C(10000)
#define REMOTE_OFFSET INT64_C(200000)
static const char *scenario;
static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", scenario, __LINE__, #x); exit(1); \
} } while (0)

typedef struct {
    echoy_band_t *band;
    echoy_link_update_t desired;
    bool queued, consume, prepare, fail_next;
    int64_t now, offset;
    unsigned rejected, prepared, failed;
    echoy_control_view_t state;
    echoy_link_view_t link;
    struct {
        bool enabled, silent;
        echoy_audio_loops_t *old, *next;
        int64_t old_start, at;
        double energy;
        unsigned before, after;
    } pcm;
} real_owner_t;
typedef struct { board_link_t *left, *right; int64_t now; uint32_t dropped; } pair_t;

static bool real_read(void *ctx, echoy_link_view_t *out)
{ return echoy_band_link_read(((real_owner_t *)ctx)->band, out); }
static bool real_receive(void *ctx, const board_link_update_t *update)
{
    real_owner_t *o = ctx;
    o->desired = *update;
    o->queued = true;
    return true;
}
static void refresh(real_owner_t *o)
{
    CHECK(echoy_band_read(o->band, ECHOY_TILE_A, &o->state));
    CHECK(echoy_band_link_read(o->band, &o->link));
}
static void service_owner(real_owner_t *o, int64_t now)
{
    o->now = now;
    if (o->queued && o->consume) {
        o->queued = false;
        if (!echoy_band_link_update(o->band, &o->desired, now)) ++o->rejected;
    }
    echoy_band_tick(o->band, now);
    if (o->prepare) {
        for (unsigned i = 0; i < 8; ++i) {
            echoy_band_job_t *job = echoy_band_take_job(o->band);
            if (!job) break;
            if (o->fail_next) {
                o->fail_next = false;
                CHECK(!echoy_band_finish_job(o->band, job, now));
                ++o->failed;
            } else {
                CHECK(echoy_band_prepare_job(job));
                CHECK(echoy_band_finish_job(o->band, job, now));
                ++o->prepared;
            }
            CHECK(i != 7);
        }
    }
    refresh(o);
}
static void render_owner(real_owner_t *o, int64_t now)
{
    float pcm[BLOCK_FRAMES];
    echoy_band_render(o->band, now, pcm, BLOCK_FRAMES);
    for (size_t i = 0; i < BLOCK_FRAMES; ++i) {
        CHECK(isfinite(pcm[i]));
        if (!o->pcm.enabled) continue;
        int64_t at = now + (int64_t)(i * 1000000 / ECHOY_AUDIO_SAMPLE_RATE);
        if (o->pcm.silent) { CHECK(pcm[i] == 0); continue; }
        echoy_audio_loops_t *loops = o->pcm.old;
        int64_t start = o->pcm.old_start;
        if (o->pcm.at && at >= o->pcm.at) {
            if (at < o->pcm.at + 8000) continue; /* Documented bank crossfade. */
            CHECK(o->pcm.next);
            loops = o->pcm.next;
            start = o->pcm.at;
            ++o->pcm.after;
        } else ++o->pcm.before;
        uint64_t position = ((uint64_t)(at - start) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
        double expected = loops->samples[ECHOY_AUDIO_FULL][position % loops->frames] * .65 * .45;
        CHECK(fabs(pcm[i] - expected) < 1e-6);
        o->pcm.energy += (double)pcm[i] * pcm[i];
    }
    refresh(o);
}
static void packet_delivery(board_link_t *h, uint16_t service, int64_t now)
{
    link_event_t e = {.received_us = now, .event = {
        .type = MOSAICO_NETWORK_EVENT_MESSAGE_DELIVERED,
        .data.delivery = {.service_id = service, .destination_id = h->peer}}};
    process_event(h, &e);
}
static void transport_step(pair_t *p, board_link_t *from, board_link_t *to, int64_t physical)
{
    real_owner_t *owner = from->ctx;
    fake_now = physical + owner->offset;
    read_local(from, fake_now);
    deliver_update(from);
    service_owner(owner, fake_now);
    send_next(from, fake_now);
    if (have_packet) {
        CHECK(packet_sender == from);
        if (!(p->dropped & (1u << packet.payload[1]))) {
            int64_t recipient_time = physical + 1000 + ((real_owner_t *)to->ctx)->offset;
            receive_message(to, &packet, recipient_time);
        }
        packet_delivery(from, packet.service_id, fake_now + 2000);
        have_packet = false;
    }
}
static void advance_pair(pair_t *p, int64_t until)
{
    CHECK(until >= p->now);
    for (; p->now < until; p->now += BLOCK_US) {
        transport_step(p, p->right, p->left, p->now);
        transport_step(p, p->left, p->right, p->now + 2000);
        render_owner(p->left->ctx, p->now + 2000);
        render_owner(p->right->ctx, p->now + REMOTE_OFFSET);
    }
}
static real_owner_t *fresh_owner(bool right)
{
    real_owner_t *o = calloc(1, sizeof(*o));
    CHECK(o);
    o->band = echoy_band_create();
    CHECK(o->band);
    o->consume = o->prepare = true;
    o->offset = right ? REMOTE_OFFSET : 0;
    if (right) CHECK(echoy_band_action(o->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_HARMONY, o->offset));
    CHECK(echoy_band_action(o->band, ECHOY_TILE_A, ECHOY_ACT_EXAMPLE, right ? 1 : 0, o->offset));
    service_owner(o, o->offset);
    service_owner(o, o->offset + 50000);
    service_owner(o, o->offset + 3000000);
    CHECK(o->state.state.phase == ECHOY_IDLE && o->state.plan.count);
    return o;
}
static board_link_t *real_node(real_owner_t *owner, bool following)
{
    board_link_t *h = calloc(1, sizeof(*h));
    CHECK(h);
    h->read = real_read; h->receive = real_receive; h->ctx = owner; h->network = (void *)h;
    h->sensors_ready = true;
    h->contacts = following ? MOSAICO_EDGE_MASK_LEFT : MOSAICO_EDGE_MASK_RIGHT;
    mosaico_network_neighbor_event_t n = {.peer_id = following ? 1 : 2, .session_id = 777,
        .local_edge = following ? MOSAICO_EDGE_LEFT : MOSAICO_EDGE_RIGHT,
        .peer_edge = following ? MOSAICO_EDGE_RIGHT : MOSAICO_EDGE_LEFT};
    fake_now = 4000000 + owner->offset;
    attach(h, &n);
    CHECK(h->attached);
    return h;
}
static pair_t *fresh_pair(bool right_playing)
{
    pair_t *p = calloc(1, sizeof(*p));
    CHECK(p);
    real_owner_t *left = fresh_owner(false), *right = fresh_owner(true);
    CHECK(echoy_band_action(left->band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 3100000));
    service_owner(left, 3200000);
    int64_t original_start = left->state.state.start_us;
    p->left = real_node(left, false); p->right = real_node(right, true); p->now = 4000000;
    advance_pair(p, 9000000);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    CHECK(left->state.state.start_us == original_start); /* Baseline join cannot restart the source. */
    CHECK(right->state.state.phase == ECHOY_IDLE && !right->state.state.continuous);
    CHECK(right->state.state.following);
    if (right_playing)
        CHECK(echoy_band_action(right->band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, p->now + right->offset));
    advance_pair(p, 12000000);
    return p;
}
static void expect_pcm(real_owner_t *o, bool silent)
{
    refresh(o);
    bool original = o->state.source == ECHOY_TILE_NONE && o->state.plan.role == ECHOY_PULSE &&
                    o->state.plan.phrase == 1;
    CHECK(echoy_audio_prepare(&o->state.plan, original, &o->pcm.old) == ECHOY_AUDIO_OK);
    o->pcm.old_start = o->state.state.continuous ? o->link.clock_origin_us : o->state.state.start_us;
    o->pcm.enabled = true; o->pcm.silent = silent;
}
static void expect_next(real_owner_t *o, const echoy_plan_t *plan, int64_t at)
{
    CHECK(echoy_audio_prepare(plan, false, &o->pcm.next) == ECHOY_AUDIO_OK);
    o->pcm.at = at;
}
static void equal_plan(const echoy_plan_t *a, const echoy_plan_t *b)
{
    uint8_t left[ECHOY_LINK_PLAN_MAX_BYTES], right[ECHOY_LINK_PLAN_MAX_BYTES];
    size_t count = echoy_link_plan_encode(a, left, sizeof(left));
    CHECK(count && count == echoy_link_plan_encode(b, right, sizeof(right)));
    CHECK(!memcmp(left, right, count));
}
static void destroy_pair(pair_t *p)
{
    board_link_t *nodes[] = {p->left, p->right};
    for (unsigned i = 0; i < 2; ++i) {
        real_owner_t *o = nodes[i]->ctx;
        echoy_audio_free_loops(o->pcm.old); echoy_audio_free_loops(o->pcm.next);
        echoy_band_delete(o->band); free(o); free(nodes[i]);
    }
    free(p);
}

static void common_bar_preserves_pcm(bool right_playing)
{
    pair_t *p = fresh_pair(right_playing);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    echoy_plan_t old_a = a->state.plan, old_b = b->state.plan, expected_a, local_b, expected_b;
    expect_pcm(a, false); expect_pcm(b, !right_playing);
    b->prepare = false;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    CHECK(echoy_compose(old_a.source, old_a.source_count, ECHOY_VOICE, 1, .5, &expected_a));
    CHECK(echoy_compose(old_a.source, old_a.source_count, old_b.role, old_b.phrase, old_b.density, &local_b));
    CHECK(echoy_arrange_linked(&local_b, &expected_a, &expected_b));
    advance_pair(p, 15000000);
    equal_plan(&a->state.plan, &old_a); equal_plan(&b->state.plan, &old_b);
    CHECK(!a->link.armed_transaction && !b->link.armed_transaction);
    CHECK(a->pcm.before && a->pcm.energy > 1e-6);
    b->prepare = true;
    while (p->now < 18000000 && p->left->transaction.phase < TX_REMOTE_ARM)
        advance_pair(p, p->now + BLOCK_US);
    CHECK(p->left->transaction.phase >= TX_REMOTE_ARM && p->left->transaction.phase < TX_DONE);
    int64_t at = p->left->transaction.at;
    CHECK(at > p->now + 800000);
    expect_next(a, &expected_a, at);
    expect_next(b, &expected_b, at + p->right->clock.offset);
    advance_pair(p, at - 10000);
    equal_plan(&a->state.plan, &old_a); equal_plan(&b->state.plan, &old_b);
    advance_pair(p, at + 600000);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    CHECK(a->link.armed_at_us == at && llabs(b->link.armed_at_us - at - REMOTE_OFFSET) <= 1000);
    equal_plan(&a->state.plan, &expected_a); equal_plan(&b->state.plan, &expected_b);
    CHECK(a->pcm.after && a->pcm.energy > 1e-6);
    CHECK(b->state.state.continuous == right_playing);
    if (right_playing) CHECK(b->pcm.after && b->pcm.energy > 1e-6);
    destroy_pair(p);
}

static void failed_preparation_or_lost_commit(bool fail)
{
    pair_t *p = fresh_pair(true);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    echoy_plan_t old_a = a->state.plan, old_b = b->state.plan;
    expect_pcm(a, false); expect_pcm(b, false);
    if (fail) b->fail_next = true;
    else p->dropped = 1u << WIRE_COMMIT;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 3, p->now));
    advance_pair(p, 20000000);
    CHECK(p->left->transaction.phase == TX_ABORTED && p->right->transaction.phase == TX_ABORTED);
    CHECK(a->state.cooperation == ECHOY_COOP_RECOVERING && b->state.cooperation == ECHOY_COOP_RECOVERING);
    equal_plan(&a->state.plan, &old_a); equal_plan(&b->state.plan, &old_b);
    CHECK(a->pcm.energy > 1e-6 && b->pcm.energy > 1e-6);
    if (fail) CHECK(b->failed == 1);
    destroy_pair(p);
}


static void superseded_jobs_and_concurrent_edits(void)
{
    pair_t *p = fresh_pair(true);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    echoy_plan_t old_a = a->state.plan, old_b = b->state.plan;
    expect_pcm(a, false); expect_pcm(b, false);
    a->prepare = false;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    echoy_band_job_t *stale = echoy_band_take_job(a->band);
    CHECK(stale && echoy_band_prepare_job(stale));
    advance_pair(p, 13000000);
    uint32_t old_transaction = p->left->transaction.id;
    CHECK(p->left->transaction.phase == TX_PREPARING && !a->link.offer_ready);
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_DUST, p->now));
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 3, p->now));
    CHECK(echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now + b->offset));
    CHECK(!echoy_band_finish_job(a->band, stale, p->now));
    echoy_band_job_t *latest = echoy_band_take_job(a->band);
    CHECK(latest);
    advance_pair(p, 14000000);
    CHECK(p->left->transaction.id != old_transaction);
    equal_plan(&a->state.plan, &old_a); equal_plan(&b->state.plan, &old_b);
    CHECK(echoy_band_prepare_job(latest));
    CHECK(echoy_band_finish_job(a->band, latest, p->now));
    a->prepare = true;
    while (p->now < 17000000 && p->left->transaction.phase < TX_REMOTE_ARM)
        advance_pair(p, p->now + BLOCK_US);
    CHECK(p->left->transaction.phase >= TX_REMOTE_ARM && p->left->transaction.phase < TX_DONE);
    int64_t at = p->left->transaction.at;
    echoy_plan_t expected_a, local_b, expected_b;
    CHECK(echoy_compose(old_a.source, old_a.source_count, ECHOY_DUST, 3, .5, &expected_a));
    CHECK(echoy_compose(old_a.source, old_a.source_count, ECHOY_VOICE, 1, .5, &local_b));
    CHECK(echoy_arrange_linked(&local_b, &expected_a, &expected_b));
    expect_next(a, &expected_a, at); expect_next(b, &expected_b, at + p->right->clock.offset);
    advance_pair(p, at + 600000);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    equal_plan(&a->state.plan, &expected_a); equal_plan(&b->state.plan, &expected_b);
    CHECK(a->pcm.after && b->pcm.after);
    destroy_pair(p);
}

static void armed_edits_queue_and_pause_stays_immediate(void)
{
    pair_t *p = fresh_pair(true);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    echoy_plan_t old_a = a->state.plan;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    uint32_t baseline = a->link.armed_transaction;
    while (p->now < 15000000 && (!a->link.armed_transaction || a->link.armed_transaction == baseline))
        advance_pair(p, p->now + BLOCK_US);
    uint32_t transaction = a->link.armed_transaction;
    int64_t at = a->link.armed_at_us;
    CHECK(transaction && transaction != baseline && at > p->now + 200000);
    CHECK(b->link.armed_transaction == transaction);
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_DENSITY, .8, p->now));
    CHECK(echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 3, p->now + b->offset));
    CHECK(echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, p->now + b->offset));
    advance_pair(p, p->now + 20000);
    CHECK(!b->state.state.continuous && b->state.state.phase == ECHOY_IDLE);
    expect_pcm(b, true);
    advance_pair(p, at + 30000);
    CHECK(a->link.committed_transaction == transaction && b->link.committed_transaction == transaction);
    CHECK(a->state.plan.role == ECHOY_VOICE && a->state.plan.density == .5);
    CHECK(b->state.plan.phrase == 1); /* Queued edit cannot alter the frozen first transaction. */
    advance_pair(p, at + 7000000);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    CHECK(a->link.committed_transaction != transaction);
    CHECK(a->state.plan.role == ECHOY_VOICE && a->state.plan.density == .8);
    CHECK(b->state.plan.phrase == 3 && !b->state.state.continuous);
    CHECK(a->state.plan.source_count == old_a.source_count);
    destroy_pair(p);
}


static void lost_authorization_ack_aborts_before_bar(void)
{
    pair_t *p = fresh_pair(true);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    echoy_plan_t old_a = a->state.plan, old_b = b->state.plan;
    expect_pcm(a, false); expect_pcm(b, false);
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    while (p->now < 15000000 && p->left->transaction.phase != TX_REMOTE_COMMIT)
        advance_pair(p, p->now + BLOCK_US);
    CHECK(p->left->transaction.phase == TX_REMOTE_COMMIT);
    uint32_t transaction = p->left->transaction.id;
    int64_t at = p->left->transaction.at;
    p->dropped = 1u << WIRE_STATE;
    advance_pair(p, p->now + 200000);
    CHECK(b->link.authorized_transaction == transaction && a->link.authorized_transaction != transaction);
    advance_pair(p, at + 1000000);
    CHECK(p->left->transaction.phase == TX_ABORTED && p->right->transaction.phase == TX_ABORTED);
    CHECK(a->link.committed_transaction != transaction && b->link.committed_transaction != transaction);
    equal_plan(&a->state.plan, &old_a); equal_plan(&b->state.plan, &old_b);
    CHECK(a->pcm.energy > 1e-6 && b->pcm.energy > 1e-6);
    destroy_pair(p);
}

static void wireless_deferred_recorder_does_not_hold_source(void)
{
    pair_t *p = calloc(1, sizeof(*p));
    CHECK(p);
    real_owner_t *a = fresh_owner(false), *b = fresh_owner(true);
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 3100000));
    service_owner(a, 3200000);
    CHECK(echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, 4000000 + b->offset));
    refresh(b);
    int64_t capture_at = b->state.state.capture_at_us;
    p->left = real_node(a, false); p->right = real_node(b, true); p->now = 4000000;
    advance_pair(p, 5000000);
    CHECK(b->state.cooperation == ECHOY_COOP_DEFERRED && b->state.state.phase == ECHOY_COUNT_IN);
    CHECK(b->state.state.capture_at_us == capture_at && !p->left->update.hold_changes);
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    advance_pair(p, 6000000);
    CHECK(a->state.plan.role == ECHOY_VOICE && b->state.state.phase == ECHOY_COUNT_IN);
    CHECK(!a->link.armed_transaction && !b->link.armed_transaction);
    CHECK(!p->left->update.hold_changes); /* Recorder cannot stall the independent commit. */
    CHECK(echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_CANCEL_RECORD, 0, p->now + b->offset));
    advance_pair(p, 11000000);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    CHECK(b->state.state.following && b->state.state.phase == ECHOY_IDLE && !b->state.state.continuous);
    CHECK(p->left->transaction.revision == 0); /* Join imports the newly committed source. */
    equal_plan(&p->right->received_plan, &a->state.plan);
    expect_pcm(b, true);
    advance_pair(p, 11500000);
    destroy_pair(p);
}


/* Both owners begin without a seed. The cold variant attaches before their
 * initial revision-zero preparation, matching a possible startup ordering. */
static void wireless_empty_pair_gets_first_phrase(bool recording, bool cold)
{
    pair_t *p = calloc(1, sizeof(*p));
    CHECK(p);
    real_owner_t *owners[2] = {calloc(1, sizeof(real_owner_t)), calloc(1, sizeof(real_owner_t))};
    for (unsigned i = 0; i < 2; ++i) {
        real_owner_t *o = owners[i];
        CHECK(o);
        o->band = echoy_band_create();
        CHECK(o->band);
        o->offset = i ? REMOTE_OFFSET : 0;
        o->consume = o->prepare = true;
        o->pcm.enabled = o->pcm.silent = true;
        if (!cold) {
            service_owner(o, o->offset);
            service_owner(o, o->offset + 50000);
        }
        refresh(o);
        CHECK(o->link.requested_revision == 0 && !o->state.plan.count && !o->state.plan.source_count);
        CHECK(o->state.state.phase == ECHOY_IDLE && !o->state.state.continuous);
    }
    real_owner_t *a = owners[0], *b = owners[1];
    p->left = real_node(a, false); p->right = real_node(b, true); p->now = 4000000;
    advance_pair(p, 4500000);
    CHECK(b->state.cooperation == ECHOY_COOP_WAITING && b->state.state.following);
    CHECK(b->state.state.phase == ECHOY_IDLE && b->state.state.notice == ECHOY_NOTICE_NONE);
    CHECK(!echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, p->now + b->offset));
    CHECK(!echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, p->now + b->offset));
    advance_pair(p, 9000000);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    CHECK(p->left->transaction.revision == 0 && a->link.committed_transaction);
    CHECK(b->state.cooperation == ECHOY_COOP_WAITING && b->state.state.phase == ECHOY_IDLE);
    CHECK(!a->state.plan.count && !b->state.plan.count && b->state.state.notice == ECHOY_NOTICE_NONE);
    uint32_t empty_transaction = a->link.committed_transaction;
    a->pcm.enabled = false; /* A real capture has guide/tap/reply audio. B remains silent. */
    if (recording) {
        CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, p->now));
        refresh(a);
        CHECK(a->state.state.phase == ECHOY_COUNT_IN);
        int64_t capture = a->state.state.capture_at_us;
        advance_pair(p, capture + 320000);
        CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_TAP, 0, p->now));
        advance_pair(p, capture + 1260000);
        CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_TAP, 0, p->now));
        advance_pair(p, capture + 2490000);
        CHECK(b->state.cooperation == ECHOY_COOP_WAITING && !b->state.plan.count);
        CHECK(b->state.state.phase == ECHOY_IDLE && !b->state.state.continuous);
        advance_pair(p, capture + 2510000);
    } else {
        CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_EXAMPLE, 0, p->now));
    }
    int64_t limit = p->now + 5000000;
    while (p->now < limit && (p->left->transaction.id == empty_transaction ||
                            p->left->transaction.phase < TX_REMOTE_ARM))
        advance_pair(p, p->now + BLOCK_US);
    CHECK(p->left->transaction.id != empty_transaction);
    CHECK(p->left->transaction.phase >= TX_REMOTE_ARM && p->left->transaction.phase < TX_DONE);
    int64_t at = p->left->transaction.at;
    CHECK(at > p->now && !a->state.plan.count && !b->state.plan.count);
    advance_pair(p, at - 10000);
    CHECK(!a->state.plan.count && !b->state.plan.count);
    advance_pair(p, at + 3000000);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    CHECK(a->link.committed_transaction == p->left->transaction.id &&
          b->link.committed_transaction == p->left->transaction.id);
    CHECK(a->state.plan.count && b->state.plan.count);
    CHECK(a->link.armed_at_us == at && llabs(b->link.armed_at_us - at - REMOTE_OFFSET) <= 1000);
    CHECK(b->state.cooperation == ECHOY_COOP_FOLLOWING && b->state.state.phase == ECHOY_IDLE);
    CHECK(!b->state.state.continuous && !a->state.state.continuous);
    CHECK(a->state.state.phase == ECHOY_IDLE && b->state.state.notice == ECHOY_NOTICE_NONE);
    CHECK(a->state.plan.source_count == 2u);
    CHECK(a->state.plan.source[0].step == (recording ? 2 : 0));
    CHECK(a->state.plan.source[1].step == (recording ? 8 : 2));
    CHECK(b->state.plan.source_count == a->state.plan.source_count);
    CHECK(!memcmp(a->state.plan.source, b->state.plan.source,
                  a->state.plan.source_count * sizeof(a->state.plan.source[0])));
    CHECK(!echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, p->now + b->offset));
    CHECK(echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, p->now + b->offset));
    service_owner(b, p->now + b->offset);
    CHECK(b->state.state.phase == ECHOY_PLAYING);
    b->pcm.enabled = false;
    advance_pair(p, b->state.state.start_us - b->offset + 10000);
    expect_pcm(b, false);
    advance_pair(p, p->now + 1500000);
    CHECK(b->state.state.continuous && b->state.state.phase == ECHOY_PLAYING && b->pcm.energy > 1e-6);
    CHECK(!a->state.state.continuous);
    destroy_pair(p);
}



static void baseline_timeout_does_not_invent_a_recovery_event(void)
{
    pair_t *p = calloc(1, sizeof(*p));
    CHECK(p);
    real_owner_t *a = fresh_owner(false), *b = fresh_owner(true);
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_TRANSPORT, 0, 3100000));
    service_owner(a, 3200000);
    b->prepare = false;
    p->left = real_node(a, false); p->right = real_node(b, true); p->now = 4000000;
    expect_pcm(a, false); expect_pcm(b, true);
    advance_pair(p, 7000000);
    uint32_t failed = p->left->transaction.id;
    CHECK(failed && p->left->transaction.revision == 0 && p->left->transaction.phase == TX_PREPARING);
    advance_pair(p, 41000000);
    CHECK(p->left->transaction.phase == TX_ABORTED && p->left->transaction.id == failed);
    CHECK(!p->left->recovery_requested && !p->right->recovery_requested);
    advance_pair(p, 44000000);
    CHECK(p->left->transaction.id == failed && p->left->transaction.phase == TX_ABORTED);
    destroy_pair(p);
}

static void incomplete_offer_abort_is_acknowledged_once(void)
{
    pair_t *p = fresh_pair(true);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    echoy_plan_t old_a = a->state.plan, old_b = b->state.plan;
    expect_pcm(a, false); expect_pcm(b, false);
    p->dropped = 1u << WIRE_CHUNK;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    advance_pair(p, p->now + 32000000);
    uint32_t failed = p->left->transaction.id;
    CHECK(p->left->transaction.phase == TX_ABORTED && p->right->transaction.phase == TX_ABORTED);
    CHECK(b->link.offered_transaction != failed); /* No complete OFFER reached this owner. */
    CHECK(b->link.failed_transaction == failed);
    CHECK(p->left->update.command == ECHOY_LINK_NONE && p->right->update.command == ECHOY_LINK_NONE);
    unsigned rejected = b->rejected;
    advance_pair(p, p->now + 1000000);
    CHECK(b->rejected == rejected);
    equal_plan(&a->state.plan, &old_a); equal_plan(&b->state.plan, &old_b);
    CHECK(a->pcm.energy > 1e-6 && b->pcm.energy > 1e-6);
    destroy_pair(p);
}

static void fresh_transport_recovers_without_replug_and_queues_edits(void)
{
    pair_t *p = fresh_pair(false);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    echoy_plan_t old_a = a->state.plan, old_b = b->state.plan, next_a, local_b, next_b;
    expect_pcm(a, false); expect_pcm(b, true);
    p->dropped = UINT32_MAX;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    advance_pair(p, p->now + 32000000);
    uint32_t failed = p->left->transaction.id;
    CHECK(p->left->transaction.phase == TX_ABORTED && p->right->transaction.phase == TX_ABORTED);
    equal_plan(&a->state.plan, &old_a); equal_plan(&b->state.plan, &old_b);
    p->dropped = 0;
    int64_t limit = p->now + 8000000;
    while (p->now < limit && (p->left->transaction.id == failed ||
                              p->left->transaction.phase < TX_REMOTE_ARM ||
                              p->left->transaction.phase >= TX_DONE))
        advance_pair(p, p->now + BLOCK_US);
    uint32_t recovering = p->left->transaction.id;
    CHECK(recovering != failed && p->left->transaction.phase >= TX_REMOTE_ARM &&
          p->left->transaction.phase < TX_DONE);
    int64_t at = p->left->transaction.at;
    CHECK(echoy_compose(old_a.source, old_a.source_count, ECHOY_VOICE, 1, .5, &next_a));
    CHECK(echoy_compose(old_a.source, old_a.source_count, old_b.role, old_b.phrase, old_b.density, &local_b));
    CHECK(echoy_arrange_linked(&local_b, &next_a, &next_b));
    expect_next(a, &next_a, at);
    while (p->now < at - 200000 && a->link.armed_transaction != recovering)
        advance_pair(p, p->now + BLOCK_US);
    CHECK(a->link.armed_transaction == recovering && b->link.armed_transaction == recovering);
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_DENSITY, .8, p->now));
    CHECK(echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_PHRASE, 3, p->now + b->offset));
    CHECK(echoy_band_action(b->band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, p->now + b->offset));
    advance_pair(p, at + 30000);
    CHECK(a->link.committed_transaction == recovering && b->link.committed_transaction == recovering);
    equal_plan(&a->state.plan, &next_a); equal_plan(&b->state.plan, &next_b);
    CHECK(a->pcm.after && !b->state.state.continuous);
    a->pcm.enabled = false; /* The separately queued next revision is checked below. */
    advance_pair(p, at + 7000000);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    CHECK(a->state.plan.density == .8 && b->state.plan.phrase == 3);
    CHECK(!b->state.state.continuous && b->state.state.phase == ECHOY_IDLE);
    destroy_pair(p);
}

static void available_owner_recovers_without_replug(bool source_fault)
{
    pair_t *p = fresh_pair(false);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    b->prepare = false;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    advance_pair(p, p->now + 1000000);
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_PAUSE, 0, p->now));
    real_owner_t *faulted = source_fault ? a : b;
    CHECK(echoy_band_action(faulted->band, ECHOY_TILE_A, ECHOY_ACT_CONDITION,
                           ECHOY_AUDIO_FAULT, p->now + faulted->offset));
    advance_pair(p, p->now + 31000000);
    uint32_t failed = p->left->transaction.id;
    CHECK(p->left->transaction.phase == TX_ABORTED && p->right->transaction.phase == TX_ABORTED);
    a->pcm.enabled = a->pcm.silent = b->pcm.enabled = b->pcm.silent = true;
    b->prepare = true;
    CHECK(echoy_band_action(faulted->band, ECHOY_TILE_A, ECHOY_ACT_CONDITION,
                           ECHOY_NORMAL, p->now + faulted->offset));
    advance_pair(p, p->now + 8000000);
    CHECK(p->left->transaction.id != failed);
    CHECK(p->left->transaction.phase == TX_DONE && p->right->transaction.phase == TX_DONE);
    CHECK(a->state.plan.role == ECHOY_VOICE && b->state.plan.count);
    CHECK(!a->state.state.continuous && !b->state.state.continuous);
    CHECK(a->state.state.phase == ECHOY_IDLE && b->state.state.phase == ECHOY_IDLE);
    destroy_pair(p);
}


static void unoffered_abort_preserves_authorized_commit(void)
{
    pair_t *p = fresh_pair(true);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    uint32_t old = a->link.committed_transaction;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    int64_t limit = p->now + 5000000;
    while (p->now < limit && (!a->link.authorized_transaction ||
           a->link.authorized_transaction == old ||
           b->link.authorized_transaction != a->link.authorized_transaction))
        advance_pair(p, p->now + BLOCK_US);
    uint32_t authorized = a->link.authorized_transaction;
    int64_t at = a->link.armed_at_us;
    CHECK(authorized != old && at > p->now);
    echoy_link_update_t *abort = calloc(1, sizeof(*abort));
    CHECK(abort);
    *abort = b->desired;
    abort->command = ECHOY_LINK_ABORT;
    abort->transaction = authorized + 1;
    abort->failed = true;
    CHECK(echoy_band_link_update(b->band, abort, p->now + b->offset));
    refresh(b);
    CHECK(b->link.failed_transaction == abort->transaction);
    CHECK(b->link.authorized_transaction == authorized && b->link.armed_transaction == authorized);
    CHECK(echoy_band_link_update(b->band, abort, p->now + b->offset));
    free(abort);
    advance_pair(p, at + 500000);
    CHECK(a->link.committed_transaction == authorized && b->link.committed_transaction == authorized);
    CHECK(a->state.plan.role == ECHOY_VOICE && b->state.plan.count);
    destroy_pair(p);
}

static void unoffered_abort_preserves_deferred_recording_job(void)
{
    real_owner_t *o = fresh_owner(false);
    CHECK(echoy_band_action(o->band, ECHOY_TILE_A, ECHOY_ACT_RECORD, 0, 4000000));
    refresh(o);
    int64_t capture = o->state.state.capture_at_us;
    uint32_t generation = o->state.state.generation;
    echoy_link_update_t *abort = calloc(1, sizeof(*abort));
    CHECK(abort);
    *abort = (echoy_link_update_t){.attached = true, .following = true,
        .connection_generation = 1, .session = 17};
    CHECK(echoy_band_link_update(o->band, abort, 4001000));
    service_owner(o, capture + 312500);
    CHECK(echoy_band_action(o->band, ECHOY_TILE_A, ECHOY_ACT_TAP, 0, capture + 312500));
    service_owner(o, capture + 1250000);
    CHECK(echoy_band_action(o->band, ECHOY_TILE_A, ECHOY_ACT_TAP, 0, capture + 1250000));
    echoy_band_tick(o->band, capture + 2500000);
    echoy_band_job_t *job = echoy_band_take_job(o->band);
    CHECK(job);
    abort->command = ECHOY_LINK_ABORT; abort->transaction = 123;
    CHECK(echoy_band_link_update(o->band, abort, capture + 2500000));
    abort->failed = true; /* Later link loss may upgrade a deferred cancellation. */
    CHECK(echoy_band_link_update(o->band, abort, capture + 2500000));
    CHECK(echoy_band_link_update(o->band, abort, capture + 2500000));
    CHECK(echoy_band_prepare_job(job));
    CHECK(echoy_band_finish_job(o->band, job, capture + 2510000));
    service_owner(o, capture + 2570000);
    CHECK(o->link.failed_transaction == 123 && o->link.deferred);
    CHECK(o->state.state.generation == generation && o->state.state.phase == ECHOY_PREVIEW);
    CHECK(o->state.plan.source_count == 2 && o->state.plan.source[0].step == 2 &&
          o->state.plan.source[1].step == 8);
    free(abort); echoy_band_delete(o->band); free(o);
}


static void failed_abort_supersedes_unconsumed_source_begin(void)
{
    pair_t *p = fresh_pair(true);
    real_owner_t *a = p->left->ctx, *b = p->right->ctx;
    echoy_plan_t old_a = a->state.plan, old_b = b->state.plan;
    expect_pcm(a, false); expect_pcm(b, false);
    a->consume = false;
    b->prepare = false;
    CHECK(echoy_band_action(a->band, ECHOY_TILE_A, ECHOY_ACT_ROLE, ECHOY_VOICE, p->now));
    advance_pair(p, p->now + 1000000);
    uint32_t failed = p->left->transaction.id;
    CHECK(a->desired.command == ECHOY_LINK_BEGIN && a->link.offered_transaction != failed);
    CHECK(a->link.proposal_pending && a->link.proposal_ready);
    advance_pair(p, p->now + 31000000);
    CHECK(a->desired.command == ECHOY_LINK_ABORT && a->desired.failed);
    CHECK(p->left->transaction.phase == TX_ABORTED && a->link.offered_transaction != failed);
    a->consume = true;
    advance_pair(p, p->now + 5000000);
    CHECK(a->link.failed_transaction == failed);
    equal_plan(&a->state.plan, &old_a); equal_plan(&b->state.plan, &old_b);
    CHECK(p->left->update.command == ECHOY_LINK_NONE);
    destroy_pair(p);
}

/* First Play is prompt from either side; clock traffic cannot grant permission. */
static void prompt_group_start(bool follower_first, bool simultaneous)
{
    pair_t *p=fresh_pair(false);
    real_owner_t *a=p->left->ctx, *b=p->right->ctx;
    CHECK(echoy_band_action(a->band,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,p->now));
    advance_pair(p,p->now+500000);
    real_owner_t *first=follower_first ? b : a, *later=follower_first ? a : b;
    int64_t pressed=p->now, origin;
    CHECK(echoy_band_action(first->band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,pressed+first->offset));
    service_owner(first,pressed+first->offset);
    CHECK(first->state.state.start_us==pressed+first->offset+50000);
    origin=first->state.state.start_us-first->offset;
    if (simultaneous) {
        CHECK(echoy_band_action(later->band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,pressed+later->offset+20000));
        service_owner(later,pressed+later->offset+20000);
    }
    advance_pair(p,p->now+500000);
    CHECK(llabs(a->link.clock_origin_us-b->link.clock_origin_us+REMOTE_OFFSET)<=1000);
    CHECK(llabs(a->link.clock_origin_us-origin)<=1000);
    CHECK(first->state.state.continuous);
    if (!simultaneous) {
        CHECK(!later->state.state.continuous && later->state.state.phase==ECHOY_IDLE);
        CHECK(echoy_band_action(later->band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,p->now+later->offset));
        service_owner(later,p->now+later->offset);
        CHECK(later->state.state.start_us>p->now+later->offset+1000000);
        CHECK(llabs((later->state.state.start_us-later->offset-origin)%2500000)<=1000);
    }
    advance_pair(p,p->now+3000000);
    expect_pcm(a,false);expect_pcm(b,false);
    advance_pair(p,p->now+200000);
    CHECK(a->pcm.energy>1e-6 && b->pcm.energy>1e-6);
    destroy_pair(p);
}
static void immediate_release_rejects_old_session(void)
{
    pair_t *p=fresh_pair(true);
    real_owner_t *a=p->left->ctx,*b=p->right->ctx;
    echoy_plan_t retained=b->state.plan;
    echoy_link_update_t stale=b->desired;
    expect_pcm(a,false);expect_pcm(b,false);
    /* A pending proposal/queued start must not delay visible physical release. */
    CHECK(echoy_band_action(a->band,ECHOY_TILE_A,ECHOY_ACT_PHRASE,2,p->now));
    advance_pair(p,p->now+100000);
    mosaico_mag_presence_event_t e={.attached_mask=0};
    presence_event(&e,p->right);
    deliver_update(p->right);service_owner(b,p->now+b->offset);
    CHECK(b->state.cooperation==ECHOY_COOP_SOLO && !b->state.state.following);
    CHECK(b->state.clock==ECHOY_TILE_A && b->state.source==ECHOY_TILE_NONE);
    CHECK(b->state.state.continuous);equal_plan(&b->state.plan,&retained);
    CHECK(!echoy_band_link_update(b->band,&stale,p->now+b->offset));
    render_owner(b,p->now+b->offset);
    CHECK(b->pcm.energy>1e-6);
    destroy_pair(p);
}

static void radio_loss_retains_physical_membership(void)
{
    pair_t *p=fresh_pair(true);
    real_owner_t *b=p->right->ctx;
    echoy_plan_t retained=b->state.plan;
    link_event_t lost={.event={.type=MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED,
        .data.neighbor={.peer_id=p->right->peer,.session_id=p->right->session,.local_edge=p->right->edge}}};
    process_event(p->right,&lost);deliver_update(p->right);service_owner(b,p->now+b->offset);
    CHECK(p->right->attached && b->state.state.following);
    CHECK(b->state.cooperation==ECHOY_COOP_RECOVERING && b->state.state.continuous);
    equal_plan(&b->state.plan,&retained);
    mosaico_network_neighbor_event_t joined={.peer_id=p->right->peer,.session_id=778,
        .local_edge=MOSAICO_EDGE_LEFT,.peer_edge=MOSAICO_EDGE_RIGHT};
    attach(p->right,&joined);deliver_update(p->right);service_owner(b,p->now+b->offset);
    CHECK(p->right->session==778 && b->state.state.following);
    CHECK(b->state.clock==ECHOY_TILE_B && b->state.state.continuous);
    equal_plan(&b->state.plan,&retained);
    destroy_pair(p);
}

static void repeated_paused_starts_and_stale_clock(void)
{
    pair_t *p=fresh_pair(false);
    real_owner_t *a=p->left->ctx,*b=p->right->ctx;
    for (unsigned round=0;round<4;++round) {
        CHECK(echoy_band_action(a->band,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,p->now));
        CHECK(echoy_band_action(b->band,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,p->now+b->offset));
        advance_pair(p,p->now+600000);
        real_owner_t *first=round%2 ? a : b,*peer=round%2 ? b : a;
        int64_t pressed=p->now;
        CHECK(echoy_band_action(first->band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,pressed+first->offset));
        service_owner(first,pressed+first->offset);
        CHECK(first->state.state.start_us==pressed+first->offset+50000);
        if (!round) {
            p->dropped=1u<<WIRE_CLOCK;
            advance_pair(p,p->now+200000);
            mosaico_network_message_t stale={.source_id=p->right->peer,.service_id=LINK_SERVICE_BASE,.payload_len=36};
            stale.payload[0]=LINK_WIRE_VERSION;stale.payload[1]=WIRE_CLOCK;
            put32(stale.payload+4,p->right->session);
            put32(stale.payload+8,p->right->playback.receive_sequence+1);
            put32(stale.payload+20,p->right->playback.revision);
            put_time(stale.payload+24,3150000);stale.payload[32]=1;
            receive_message(p->right,&stale,p->now+b->offset);
            deliver_update(p->right);service_owner(b,p->now+b->offset);
            CHECK(b->link.clock_origin_us==pressed+b->offset+50000);
            p->dropped=0;
        }
        advance_pair(p,p->now+1000000);
        CHECK(llabs(a->link.clock_origin_us-b->link.clock_origin_us+REMOTE_OFFSET)<=1000);
        CHECK(llabs(a->link.clock_origin_us-pressed-50000)<=1000);
        CHECK(!peer->state.state.continuous && first->state.state.continuous);
    }
    destroy_pair(p);
}

static void radio_session_supersedes_unconsumed_abort(void)
{
    pair_t *p=fresh_pair(true);
    real_owner_t *a=p->left->ctx,*b=p->right->ctx;
    echoy_plan_t retained=b->state.plan, old_upstream=a->state.plan;
    b->prepare=false;
    CHECK(echoy_band_action(a->band,ECHOY_TILE_A,ECHOY_ACT_ROLE,ECHOY_VOICE,p->now));
    advance_pair(p,p->now+700000);
    CHECK(b->link.offered_transaction!=b->link.committed_transaction);
    link_event_t lost={.event={.type=MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED,
        .data.neighbor={.peer_id=p->right->peer,.session_id=p->right->session,.local_edge=p->right->edge}}};
    process_event(p->right,&lost);
    mosaico_network_neighbor_event_t joined={.peer_id=p->right->peer,.session_id=778,
        .local_edge=MOSAICO_EDGE_LEFT,.peer_edge=MOSAICO_EDGE_RIGHT};
    attach(p->right,&joined);deliver_update(p->right);
    b->prepare=true;
    service_owner(b,p->now+b->offset);
    service_owner(b,p->now+b->offset+5000000);
    equal_plan(&b->state.plan,&retained);
    CHECK(b->state.state.following && b->state.state.continuous);
    CHECK(echoy_band_action(b->band,ECHOY_TILE_A,ECHOY_ACT_DENSITY,.7,p->now+b->offset+5001000));
    service_owner(b,p->now+b->offset+5001000);
    service_owner(b,p->now+b->offset+8000000);
    echoy_plan_t local,expected;
    CHECK(echoy_compose(old_upstream.source,old_upstream.source_count,retained.role,retained.phrase,.7,&local));
    CHECK(echoy_arrange_linked(&local,&old_upstream,&expected));
    equal_plan(&b->state.plan,&expected);
    destroy_pair(p);
}

static void first_start_before_recovered_clock(void)
{
 pair_t *p=fresh_pair(false);
 real_owner_t *a=p->left->ctx,*b=p->right->ctx;
 CHECK(echoy_band_action(a->band,ECHOY_TILE_A,ECHOY_ACT_PAUSE,0,p->now));
 advance_pair(p,p->now+600000);
 board_link_t *nodes[]={p->left,p->right};
 for (int i=0;i<2;++i) {
  board_link_t *h=nodes[i];
  link_event_t lost={.event={.type=MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED,
    .data.neighbor={.peer_id=h->peer,.session_id=h->session,.local_edge=h->edge}}};
  process_event(h,&lost);
  mosaico_network_neighbor_event_t joined={.peer_id=h->peer,.session_id=778,.local_edge=h->edge,
    .peer_edge=h->following ? MOSAICO_EDGE_RIGHT : MOSAICO_EDGE_LEFT};
  attach(h,&joined);deliver_update(h);service_owner(h->ctx,p->now+((real_owner_t*)h->ctx)->offset);
 }
 // Read a fresh session view first; follower then presses before offset is valid.
 advance_pair(p,p->now+10000);
 int64_t pressed=p->now;
 CHECK(!p->right->clock.valid);
 CHECK(echoy_band_action(b->band,ECHOY_TILE_A,ECHOY_ACT_TRANSPORT,0,pressed+b->offset));
 service_owner(b,pressed+b->offset);
 advance_pair(p,p->now+1000000);
 CHECK(llabs(a->link.clock_origin_us-pressed-50000)<=1000);
 CHECK(llabs(a->link.clock_origin_us-b->link.clock_origin_us+REMOTE_OFFSET)<=1000);
 destroy_pair(p);
}

int main(int argc, char **argv)
{
    const char *names[] = {"common-bar-playing", "common-bar-paused", "prepare-failed", "commit-lost",
                           "superseded-job-concurrent-edits", "armed-edits-and-pause",
                           "authorization-ack-lost", "wireless-deferred-recorder",
                           "wireless-empty-example", "wireless-empty-record", "wireless-empty-cold-start",
                           "incomplete-offer-abort", "fresh-transport-recovery",
                           "source-owner-recovery", "receiver-owner-recovery",
                           "unoffered-abort-authorized", "unoffered-abort-deferred-job",
                           "abort-supersedes-source-begin", "prompt-left-first", "prompt-right-first",
                           "prompt-simultaneous", "immediate-physical-release", "radio-retains-membership", "repeated-start-stale-clock", "radio-supersedes-abort", "early-recovery-first-start"};
    unsigned ran = 0;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (argc > 1 && strcmp(argv[1], names[i])) continue;
        scenario = names[i];
        if (i < 2) common_bar_preserves_pcm(i == 0);
        else if (i < 4) failed_preparation_or_lost_commit(i == 2);
        else if (i == 4) superseded_jobs_and_concurrent_edits();
        else if (i == 5) armed_edits_queue_and_pause_stays_immediate();
        else if (i == 6) lost_authorization_ack_aborts_before_bar();
        else if (i == 7) wireless_deferred_recorder_does_not_hold_source();
        else if (i < 11) wireless_empty_pair_gets_first_phrase(i == 9, i == 10);
        else if (i == 11) {
            incomplete_offer_abort_is_acknowledged_once();
            baseline_timeout_does_not_invent_a_recovery_event();
        }
        else if (i == 12) fresh_transport_recovers_without_replug_and_queues_edits();
        else if (i < 15) available_owner_recovers_without_replug(i == 13);
        else if (i == 15) unoffered_abort_preserves_authorized_commit();
        else if (i == 16) unoffered_abort_preserves_deferred_recording_job();
        else if (i==17) failed_abort_supersedes_unconsumed_source_begin();
        else if (i<21) prompt_group_start(i!=18,i==20);
        else if (i==21) immediate_release_rejects_old_session();
        else if (i==22) radio_loss_retains_physical_membership();
        else if (i==23) repeated_paused_starts_and_stale_clock();
        else if (i==24) radio_session_supersedes_unconsumed_abort();
        else first_start_before_recovered_clock();
        printf("PASS %s\n", scenario); ++ran;
    }
    CHECK(ran);
    printf("PASS %u real transport/band/PCM cases, %u assertions\n", ran, assertions);
    return 0;
}
