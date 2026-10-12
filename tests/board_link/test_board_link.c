#include "fake_hardware.h"

static unsigned callback_count;
static bool accept_update = true;
static board_link_update_t last_update;

typedef struct {
    echoy_link_view_t view;
    board_link_update_t desired;
    echoy_plan_t offered;
    bool queued, consume, prepare, fail, reject_read;
    unsigned begin_count, arm_count, commit_count, aborted_count;
    int64_t applied_at;
} owner_t;

static bool read_view(void *ctx, echoy_link_view_t *out)
{
    owner_t *o = ctx;
    if (o->reject_read) return false;
    *out = o->view;
    return true;
}
static bool receive_update(void *ctx, const board_link_update_t *update)
{
    owner_t *o = ctx;
    callback_count++;
    if (!accept_update) return false;
    last_update = *update;
    if (o) { o->desired = *update; o->queued = true; }
    return true;
}

/* The test owner models asynchronous snapshot admission, with independently
 * controlled preparation. Actual compilation/PCM coverage lives in band tests. */
static void owner_tick(owner_t *o, int64_t now)
{
    if (o->consume && o->queued) {
        const board_link_update_t *u = &o->desired;
        o->queued = false;
        if (!u->attached) {
            o->view.offered_transaction = o->view.armed_transaction = o->view.authorized_transaction = 0;
        } else switch (u->command) {
        case ECHOY_LINK_BEGIN:
        case ECHOY_LINK_OFFER:
            if (o->view.offered_transaction != u->transaction) {
                o->begin_count++;
                o->view.offered_transaction = u->transaction;
                o->view.offered_revision = u->command == ECHOY_LINK_BEGIN ? u->revision : 71;
                o->view.armed_transaction = o->view.authorized_transaction = 0;
                o->view.offer_ready = false;
                o->offered = u->command == ECHOY_LINK_BEGIN ?
                    (u->revision ? o->view.proposal : o->view.current) : u->plan;
            }
            break;
        case ECHOY_LINK_ARM:
            if (o->view.offered_transaction == u->transaction && o->view.offer_ready &&
                o->view.offered_revision == u->revision && now < u->commit_at_us) {
                if (o->view.armed_transaction != u->transaction) o->arm_count++;
                o->view.armed_transaction = u->transaction;
                o->view.armed_revision = u->revision;
                o->view.armed_at_us = u->commit_at_us;
            }
            break;
        case ECHOY_LINK_COMMIT:
            if (o->view.armed_transaction == u->transaction && o->view.armed_revision == u->revision &&
                o->view.armed_at_us == u->commit_at_us && now < u->commit_at_us) {
                if (o->view.authorized_transaction != u->transaction) o->commit_count++;
                o->view.authorized_transaction = u->transaction;
            }
            break;
        case ECHOY_LINK_ABORT:
            o->aborted_count++;
            if (u->failed) o->view.failed_transaction = u->transaction;
            if (o->view.committed_transaction != u->transaction) {
                o->view.armed_transaction = o->view.authorized_transaction = 0;
                o->view.offer_ready = false;
                o->view.offered_transaction = 0;
            }
            break;
        default: break;
        }
    }
    if (o->view.offered_transaction && !o->view.armed_transaction) {
        o->view.offer_ready = o->prepare && !o->view.deferred && !o->fail;
        o->view.offer_failed = o->fail;
    }
    if (o->view.authorized_transaction && now >= o->view.armed_at_us &&
        o->view.committed_transaction != o->view.authorized_transaction) {
        o->view.committed_transaction = o->view.authorized_transaction;
        o->view.current = o->offered;
        o->view.proposal_pending = false;
        o->applied_at = o->view.armed_at_us;
    }
}

static owner_t *new_owner(bool proposal)
{
    owner_t *o = calloc(1, sizeof(*o));
    assert(o);
    o->consume = o->prepare = true;
    o->view.available = o->view.clock_started = o->view.looping = true;
    o->view.clock_origin_us = 700000;
    o->view.requested_revision = 11;
    assert(echoy_compose_mask(0x1111, ECHOY_HARMONY, 2, 0.7, &o->view.current));
    assert(echoy_compose_mask(0x5555, ECHOY_VOICE, 3, 0.3, &o->view.proposal));
    o->view.proposal_pending = proposal;
    o->view.proposal_ready = proposal;
    o->view.proposal_revision = proposal ? 11 : 0;
    return o;
}

static board_link_t *node(bool following, owner_t *owner)
{
    board_link_t *h = calloc(1, sizeof(*h));
    assert(h);
    h->ctx = owner;
    h->read = read_view;
    h->receive = receive_update;
    h->network = (void *)h;
    h->sensors_ready = true;
    h->contacts = following ? MOSAICO_EDGE_MASK_LEFT : MOSAICO_EDGE_MASK_RIGHT;
    mosaico_network_neighbor_event_t n = {
        .peer_id = following ? 1 : 2, .session_id = 123,
        .local_edge = following ? MOSAICO_EDGE_LEFT : MOSAICO_EDGE_RIGHT,
        .peer_edge = following ? MOSAICO_EDGE_RIGHT : MOSAICO_EDGE_LEFT,
    };
    attach(h, &n);
    assert(h->attached && h->following == following && !h->update.valid);
    return h;
}

static void delivered(board_link_t *h, uint16_t service, bool success, int64_t now)
{
    link_event_t e = {
        .received_us = now,
        .event = {.type = success ? MOSAICO_NETWORK_EVENT_MESSAGE_DELIVERED : MOSAICO_NETWORK_EVENT_MESSAGE_FAILED,
                  .data.delivery = {.service_id = service, .destination_id = h->peer,
                                    .error = success ? ESP_OK : ESP_FAIL}},
    };
    process_event(h, &e);
}

static uint32_t drop_kinds;
static unsigned duplicate_packets;
static void step(board_link_t *sender, board_link_t *recipient, int64_t physical)
{
    fake_now = physical + (sender->following ? 200000 : 0);
    read_local(sender, fake_now);
    deliver_update(sender);
    owner_tick(sender->ctx, fake_now);
    send_next(sender, fake_now);
    if (!have_packet) return;
    assert(packet_sender == sender);
    if (!(drop_kinds & (1u << packet.payload[1]))) {
        receive_message(recipient, &packet, physical + 1000 + (recipient->following ? 200000 : 0));
        if (duplicate_packets) {
            receive_message(recipient, &packet, physical + 2000 + (recipient->following ? 200000 : 0));
        }
    }
    delivered(sender, packet.service_id, true, fake_now + 2000);
    have_packet = false;
}
static void run_pair(board_link_t *a, board_link_t *b, int64_t start, int64_t end)
{
    for (int64_t now = start; now < end; now += 10000) {
        step(b, a, now); step(a, b, now + 2000);
    }
}
static void free_pair(board_link_t *a, board_link_t *b)
{
    free(a->ctx); free(b->ctx); free(a); free(b);
    drop_kinds = duplicate_packets = 0;
}
static void same_plan(const echoy_plan_t *a, const echoy_plan_t *b)
{
    uint8_t left[ECHOY_LINK_PLAN_MAX_BYTES], right[ECHOY_LINK_PLAN_MAX_BYTES];
    size_t size = echoy_link_plan_encode(a, left, sizeof(left));
    assert(size && size == echoy_link_plan_encode(b, right, sizeof(right)));
    assert(!memcmp(left, right, size));
}

static void test_prepare_bar_commit_and_no_autoplay(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    ob->prepare = false;
    ob->view.looping = false;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    run_pair(a, b, 1000000, 4000000);
    assert(b->clock.valid && b->rx.complete && b->view.offered_transaction == a->transaction.id);
    assert(llabs(b->clock.offset - 200000) <= 1000);
    assert(llabs(b->update.clock_origin_us - 900000) <= 1000);
    same_plan(&ob->offered, &oa->view.proposal);
    assert(!oa->arm_count && !ob->arm_count && !oa->commit_count && !ob->commit_count);
    assert(oa->view.current.role == ECHOY_HARMONY && ob->view.current.role == ECHOY_HARMONY);
    ob->prepare = true;
    duplicate_packets = 1;
    run_pair(a, b, 4000000, 10000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    assert(oa->arm_count == 1 && ob->arm_count == 1 && oa->commit_count == 1 && ob->commit_count == 1);
    assert(oa->applied_at == 5700000 && llabs(ob->applied_at - oa->applied_at - 200000) <= 1000);
    same_plan(&oa->view.current, &ob->view.current);
    assert(!ob->view.looping);
    free_pair(a, b);
}

static void test_deferral_latest_source_and_baseline(void)
{
    owner_t *oa = new_owner(false), *ob = new_owner(false);
    ob->view.deferred = true;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    run_pair(a, b, 1000000, 2000000);
    assert(!a->update.hold_changes && !oa->begin_count && !ob->begin_count);
    /* Independent source commits while the recorder is outside cooperation. */
    oa->view.current = oa->view.proposal;
    oa->view.requested_revision++;
    run_pair(a, b, 2000000, 3000000);
    ob->view.deferred = false;
    run_pair(a, b, 3000000, 8000000);
    assert(a->transaction.phase == TX_DONE && a->transaction.revision == 0);
    same_plan(&ob->view.current, &oa->view.current);
    assert(ob->view.current.role == ECHOY_VOICE);
    free_pair(a, b);
}

static void test_persistent_owner_commands_and_failures(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    oa->consume = false;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    run_pair(a, b, 1000000, 4000000);
    assert(a->update.command == ECHOY_LINK_BEGIN && !oa->arm_count && !ob->arm_count);
    oa->consume = true;
    ob->fail = true;
    run_pair(a, b, 4000000, 6000000);
    assert(a->transaction.phase == TX_ABORTED && b->transaction.phase == TX_ABORTED);
    assert(a->update.recovering && !a->update.hold_changes);
    assert(!oa->applied_at && !ob->applied_at);
    free_pair(a, b);
}

static void test_lost_arm_and_commit_ack_fail_closed(void)
{
    for (unsigned kind = WIRE_ARM; kind <= WIRE_COMMIT; kind++) {
        owner_t *oa = new_owner(true), *ob = new_owner(false);
        board_link_t *a = node(false, oa), *b = node(true, ob);
        drop_kinds = 1u << kind;
        run_pair(a, b, 1000000, 7000000);
        assert(a->transaction.phase == TX_ABORTED && a->update.recovering);
        assert(!oa->applied_at && !ob->applied_at && !oa->commit_count);
        free_pair(a, b);
    }
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    ob->prepare = false;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    run_pair(a, b, 1000000, 33000000);
    assert(a->transaction.phase == TX_ABORTED && b->transaction.phase == TX_ABORTED);
    assert(!oa->applied_at && !ob->applied_at);
    free_pair(a, b);
}


static void test_stale_fragments_crc_repair_and_delivery_identity(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    ob->prepare = false;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    a->peer_seen = true; a->peer_seen_at = 1000000;
    read_local(a, 1000000);
    send_next(a, 1000000);
    assert(have_packet && packet.payload[1] == WIRE_META);
    mosaico_network_message_t meta = packet;
    receive_message(b, &meta, 1201000);
    unsigned before = sends;
    send_next(a, 1100000);
    assert(sends == before);
    have_packet = false;
    delivered(a, meta.service_id, true, 1002000);
    send_next(a, 1010000);
    assert(have_packet && packet.payload[1] == WIRE_CHUNK);
    mosaico_network_message_t chunk = packet;
    uint16_t service = chunk.service_id;
    have_packet = false;
    put32(chunk.payload + 4, b->session + 1);
    receive_message(b, &chunk, 1211000);
    assert(!b->rx.offset);
    put32(chunk.payload + 4, b->session);
    put32(chunk.payload + 8, b->rx.revision + 1);
    receive_message(b, &chunk, 1212000);
    assert(!b->rx.offset);
    put32(chunk.payload + 8, b->rx.revision);
    chunk.payload[20] ^= 0x80;
    receive_message(b, &chunk, 1213000);
    assert(b->rx.offset == LINK_CHUNK_BYTES);
    delivered(a, meta.service_id, true, 1015000);
    assert(a->pending.active && !a->tx.offset);
    delivered(a, service, true, 1016000);
    assert(a->tx.offset == LINK_CHUNK_BYTES);
    for (int64_t now = 1020000; now < 3000000 && a->tx.active; now += 10000) {
        send_next(a, now);
        if (!have_packet) continue;
        receive_message(b, &packet, now + 201000);
        delivered(a, packet.service_id, true, now + 2000);
        have_packet = false;
    }
    assert(!a->tx.active && !b->rx.complete && !b->rx.offset && !b->update.valid);
    run_pair(a, b, 3000000, 7000000);
    assert(b->rx.complete && b->clock.valid);
    same_plan(&b->received_plan, &oa->view.proposal);
    /* New transaction admission invalidates partial data from older T. */
    put32(meta.payload + 8, b->rx.revision + 1);
    uint32_t id = get32(meta.payload + 8);
    receive_message(b, &meta, 7210000);
    assert(b->rx.revision == id && !b->rx.complete && !b->rx.offset);
    chunk.payload[20] ^= 0x80;
    receive_message(b, &chunk, 7211000);
    assert(!b->rx.offset);
    put32(meta.payload + 8, id - 1);
    receive_message(b, &meta, 7212000);
    assert(b->rx.revision == id);
    free_pair(a, b);
}

static void test_withdraw_ready_and_latest_root_request(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    board_link_t *a = node(false, oa), *b = node(true, ob);
    /* Delay ARM so the right owner's independent edit replaces readiness. */
    drop_kinds = 1u << WIRE_ARM;
    run_pair(a, b, 1000000, 1700000);
    assert(a->transaction.phase == TX_REMOTE_ARM);
    uint64_t old_revision = a->transaction.peer_revision;
    ob->view.offered_revision++;
    ob->prepare = false;
    run_pair(a, b, 1700000, 2000000);
    assert(a->transaction.phase == TX_PREPARING && a->transaction.peer_revision != old_revision);
    assert(!oa->arm_count && !ob->arm_count);
    ob->prepare = true;
    drop_kinds = 0;
    run_pair(a, b, 2000000, 7000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    free_pair(a, b);

    oa = new_owner(true); ob = new_owner(false); ob->prepare = false;
    a = node(false, oa); b = node(true, ob);
    run_pair(a, b, 1000000, 2500000);
    uint32_t old_id = a->transaction.id;
    assert(echoy_compose_mask(0x2222, ECHOY_DUST, 1, .5, &oa->view.proposal));
    oa->view.requested_revision = oa->view.proposal_revision = 12;
    run_pair(a, b, 2500000, 4000000);
    assert(a->transaction.id != old_id && b->transaction.id == a->transaction.id);
    same_plan(&ob->offered, &oa->view.proposal);
    ob->prepare = true;
    run_pair(a, b, 4000000, 9000000);
    assert(a->transaction.phase == TX_DONE && oa->view.current.role == ECHOY_DUST);
    assert(oa->commit_count == 1 && ob->commit_count == 1);
    free_pair(a, b);
}


static void test_owner_rejects_arm_after_cached_ready(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    board_link_t *a = node(false, oa), *b = node(true, ob);
    drop_kinds = 1u << WIRE_ARM;
    run_pair(a, b, 1000000, 1700000);
    assert(a->transaction.phase == TX_REMOTE_ARM && b->view.offer_ready);
    mosaico_network_message_t arm = {.source_id = b->peer, .service_id = LINK_SERVICE_BASE,
                                     .payload_len = 32};
    arm.payload[0] = LINK_WIRE_VERSION; arm.payload[1] = WIRE_ARM;
    put32(arm.payload + 4, b->session); put32(arm.payload + 8, b->transaction.id);
    put64(arm.payload + 12, b->view.offered_revision); put_time(arm.payload + 20, a->transaction.at);
    receive_message(b, &arm, 1900000);
    assert(b->transaction.phase == TX_REMOTE_ARM && b->update.command == ECHOY_LINK_ARM);
    ob->view.offered_revision++;
    ob->prepare = false;
    owner_tick(ob, 1901000);
    read_local(b, 1950000);
    assert(b->transaction.phase == TX_PREPARING && b->update.command != ECHOY_LINK_ARM);
    run_pair(a, b, 1800000, 2100000);
    ob->prepare = true; drop_kinds = 0;
    run_pair(a, b, 2100000, 7000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    free_pair(a, b);
}

static void test_callback_backpressure_arm_not_authorization(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    board_link_t *a = node(false, oa), *b = node(true, ob);
    drop_kinds = 1u << WIRE_COMMIT;
    run_pair(a, b, 1000000, 1900000);
    assert(oa->arm_count && ob->arm_count && !oa->commit_count && !ob->commit_count);
    assert(b->view.armed_transaction == a->transaction.id);
    assert(!oa->applied_at && !ob->applied_at);
    /* Queue admission alone cannot turn an ARM into COMMIT authorization. */
    ob->consume = false;
    drop_kinds = 0;
    run_pair(a, b, 1900000, 2200000);
    assert(b->update.command == ECHOY_LINK_COMMIT && !oa->commit_count);
    assert(!b->view.authorized_transaction && !oa->applied_at && !ob->applied_at);
    ob->consume = true;
    run_pair(a, b, 2200000, 6000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    free_pair(a, b);

    oa = new_owner(true); ob = new_owner(false);
    a = node(false, oa); b = node(true, ob);
    accept_update = false;
    run_pair(a, b, 1000000, 2000000);
    assert(a->publish_pending && b->publish_pending && !oa->begin_count && !ob->begin_count);
    accept_update = true;
    run_pair(a, b, 2000000, 8000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    free_pair(a, b);
}

static void test_final_authorization_partition_is_bounded(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    board_link_t *a = node(false, oa), *b = node(true, ob);
    /* Stop returning status only once receiver consumed authorization. */
    int64_t now;
    for (now = 1000000; now < 3000000 && !ob->view.authorized_transaction; now += 10000)
        run_pair(a, b, now, now + 10000);
    assert(ob->view.authorized_transaction && !oa->view.authorized_transaction);
    drop_kinds = UINT32_MAX;
    run_pair(a, b, now, 36000000);
    assert(a->transaction.phase == TX_ABORTED && b->transaction.phase == TX_ABORTED);
    assert(a->update.recovering && b->update.recovering);
    assert(!a->update.hold_changes && !oa->applied_at);
    /* The final authorization can have reached only one side at a partition;
     * this is explicitly recovered, never reported as an atomic success. */
    assert(ob->applied_at && oa->view.current.role == ECHOY_HARMONY);
    free_pair(a, b);
}





static void test_recovery_request_deduplicates_and_needs_fresh_event(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    ob->prepare = false;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    run_pair(a, b, 1000000, 33000000);
    assert(a->transaction.phase == TX_ABORTED && b->transaction.phase == TX_ABORTED);
    uint32_t aborted = a->transaction.id;
    run_pair(a, b, 33000000, 43000000);
    assert(a->transaction.id == aborted && !a->recovery_requested && !b->recovery_requested);
    assert(a->update.command == ECHOY_LINK_NONE && b->update.command == ECHOY_LINK_NONE);
    mosaico_network_message_t want = {.source_id = a->peer, .service_id = LINK_SERVICE_BASE,
                                     .payload_len = 16};
    want.payload[0] = LINK_WIRE_VERSION; want.payload[1] = WIRE_WANT;
    put32(want.payload + 4, a->session); put32(want.payload + 8, aborted);
    put32(want.payload + 12, a->tx.crc);
    receive_message(a, &want, 43000000);
    read_local(a, 43000000);
    uint32_t restarted = a->transaction.id;
    assert(restarted != aborted && !a->recovery_requested);
    int64_t began = a->transaction.began;
    for (unsigned i = 0; i < 8; ++i) receive_message(a, &want, 43001000 + i * 1000);
    assert(a->transaction.id == restarted && a->transaction.began == began && !a->recovery_requested);
    ob->prepare = true;
    run_pair(a, b, 43010000, 49000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    free_pair(a, b);
}


static void test_abort_traffic_is_not_a_new_outage(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    ob->prepare = false;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    run_pair(a, b, 1000000, 33000000);
    assert(b->transaction.phase == TX_ABORTED && !b->recovery_requested);
    mosaico_network_message_t aborted = {.source_id = b->peer, .service_id = LINK_SERVICE_BASE,
                                        .payload_len = 32};
    aborted.payload[0] = LINK_WIRE_VERSION; aborted.payload[1] = WIRE_ABORT;
    put32(aborted.payload + 4, b->session); put32(aborted.payload + 8, b->transaction.id);
    aborted.payload[28] = 1;
    for (int64_t now = 34000000; now <= 38000000; now += 1000000)
        receive_message(b, &aborted, now);
    b->clock.waiting = true; b->clock.pending_sequence = 17; b->clock.t1 = 39000000;
    uint8_t pong[36] = {0};
    put32(pong + 8, 17); put_time(pong + 12, 39000000);
    put_time(pong + 20, 38800000); put_time(pong + 28, 38801000);
    receive_pong(b, pong, 39002000);
    assert(!b->recovery_requested && b->update.command == ECHOY_LINK_NONE);
    free_pair(a, b);
}

static void test_first_peer_after_startup_outage_recovers(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    oa->view.requested_revision = ob->view.requested_revision = 0;
    oa->view.proposal_pending = ob->view.proposal_pending = false;
    fake_now = 0;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    drop_kinds = UINT32_MAX;
    run_pair(a, b, 1000000, 33000000);
    assert(!a->peer_seen && !b->peer_seen);
    assert(a->transaction.phase == TX_ABORTED && b->transaction.phase == TX_ABORTED);
    drop_kinds = 0;
    run_pair(a, b, 33000000, 39000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    free_pair(a, b);
}

static void test_revision_zero_requires_compiled_canonical_plan(void)
{
    owner_t *oa = new_owner(false);
    oa->view.requested_revision = 0;
    oa->view.proposal_pending = true;
    oa->view.proposal_revision = 0;
    memset(&oa->view.proposal, 0, sizeof(oa->view.proposal));
    board_link_t *a = node(false, oa);
    a->peer_seen = true;
    a->peer_seen_at = 1000000;
    read_local(a, 1000000);
    assert(!a->transaction.id && !a->tx.size && a->update.command == ECHOY_LINK_NONE);
    oa->view.proposal = (echoy_plan_t){.role = ECHOY_PULSE, .phrase = 1, .density = .5, .duration = 2.5};
    read_local(a, 1100000);
    assert(a->transaction.id && a->transaction.phase == TX_PREPARING);
    assert(a->transaction.revision == 0 && a->tx.size && a->update.command == ECHOY_LINK_BEGIN);
    free(oa); free(a);
}

static void test_unavailable_owner_cannot_claim_ready(void)
{
    owner_t *oa = new_owner(true), *ob = new_owner(false);
    ob->view.available = false;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    run_pair(a, b, 1000000, 4000000);
    assert(!oa->arm_count && !ob->arm_count && !oa->applied_at && !ob->applied_at);
    ob->view.available = true;
    run_pair(a, b, 4000000, 9000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    free_pair(a, b);
}

static void test_empty_source_bad_clock_and_send_errors(void)
{
    owner_t *oa = new_owner(false), *ob = new_owner(false);
    memset(&oa->view.current, 0, sizeof(oa->view.current));
    oa->view.current.phrase = 1; oa->view.current.duration = 2.5;
    board_link_t *a = node(false, oa), *b = node(true, ob);
    b->clock.waiting = true; b->clock.pending_sequence = 10; b->clock.t1 = 1000000;
    uint8_t pong[36] = {0};
    put32(pong + 8, 10); put_time(pong + 12, 1000000);
    put_time(pong + 20, 810000); put_time(pong + 28, 820000);
    receive_pong(b, pong, 1300000);
    assert(!b->clock.valid && !b->clock.samples);
    a->peer_seen = true; a->peer_seen_at = 1000000;
    read_local(a, 1000000);
    send_error = ESP_FAIL;
    send_next(a, 1000000);
    assert(!have_packet && !a->pending.active);
    unsigned before = sends;
    send_next(a, 1200000);
    assert(sends == before);
    send_error = ESP_OK;
    run_pair(a, b, 1800000, 8000000);
    assert(a->transaction.phase == TX_DONE && b->transaction.phase == TX_DONE);
    assert(!ob->view.current.count && !ob->view.current.source_count);
    b->rx.origin = 100000; b->clock.offset = -2000000;
    publish_attachment(b);
    assert(b->update.clock_origin_us == 600000);
    free_pair(a, b);
}

static void test_contact_queue_and_lifecycle(void)
{
    owner_t *owner = new_owner(false);
    board_link_t *h = node(false, owner);
    uint32_t generation = h->update.connection_generation;
    link_event_t lost = {.event = {.type = MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED,
        .data.neighbor = {.peer_id = h->peer, .session_id = h->session, .local_edge = h->edge}}};
    process_event(h, &lost);
    assert(!h->update.attached && h->update.recovering && h->update.connection_generation > generation);
    mosaico_mag_presence_event_t after_radio_release = {.attached_mask = 0};
    presence_event(&after_radio_release, h);
    assert(!h->update.attached && !h->update.recovering);
    detach(h);
    h->contacts = 0;
    mosaico_network_neighbor_event_t neighbor = {
        .peer_id = 2, .session_id = 123, .local_edge = MOSAICO_EDGE_RIGHT, .peer_edge = MOSAICO_EDGE_LEFT,
    };
    attach(h, &neighbor); assert(!h->attached);
    h->contacts = MOSAICO_EDGE_MASK_RIGHT;
    neighbor.relative_rotation = 180;
    attach(h, &neighbor); assert(!h->attached);
    neighbor.relative_rotation = 0;
    attach(h, &neighbor); assert(h->attached);
    mosaico_mag_presence_event_t released = {.attached_mask = 0};
    presence_event(&released, h);
    assert(!h->attached && !h->update.recovering);
    h->contacts = MOSAICO_EDGE_MASK_RIGHT;
    attach(h, &neighbor);
    sensor_error = ESP_FAIL; sample_sensors(h, 1000000);
    assert(!h->attached && !h->sensors_ready && !h->contacts);
    sensor_error = ESP_OK;
    h->events = xQueueCreate(1, sizeof(link_event_t));
    mosaico_network_event_t e = {.type = MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED};
    network_event(&e, h); network_event(&e, h);
    assert(atomic_load(&h->overflow));
    vQueueDelete(h->events); free(h); free(owner);
    assert(board_link_start(NULL, receive_update, NULL) == ESP_ERR_INVALID_ARG);
    assert(board_link_start(read_view, receive_update, NULL) == ESP_OK);
    h = instance;
    assert(h && h->publish_pending && !h->update.attached);
    assert(board_link_start(read_view, receive_update, NULL) == ESP_ERR_INVALID_STATE);
    unsigned stopped = stop_calls;
    assert(board_link_stop(0) == ESP_ERR_TIMEOUT && instance == h && stop_calls == stopped);
    atomic_store(&h->worker_exited, true);
    stop_error = ESP_FAIL;
    assert(board_link_stop(100) == ESP_FAIL && instance == h);
    stop_error = ESP_OK;
    assert(board_link_stop(100) == ESP_OK && !instance && !atomic_load(&owned));
    assert(board_link_stop(0) == ESP_OK);
}

int main(void)
{
    test_prepare_bar_commit_and_no_autoplay();
    test_deferral_latest_source_and_baseline();
    test_persistent_owner_commands_and_failures();
    test_lost_arm_and_commit_ack_fail_closed();
    test_stale_fragments_crc_repair_and_delivery_identity();
    test_withdraw_ready_and_latest_root_request();
    test_owner_rejects_arm_after_cached_ready();
    test_callback_backpressure_arm_not_authorization();
    test_final_authorization_partition_is_bounded();
    test_recovery_request_deduplicates_and_needs_fresh_event();
    test_abort_traffic_is_not_a_new_outage();
    test_first_peer_after_startup_outage_recovers();
    test_revision_zero_requires_compiled_canonical_plan();
    test_unavailable_owner_cannot_claim_ready();
    test_empty_source_bad_clock_and_send_errors();
    test_contact_queue_and_lifecycle();
    puts("board_link: v2 proposed-plan/common-bar commit; deferred and paused peers; owner acknowledgements; stale/CRC/retry; revision supersession; partitions/timeouts; physical contact and checked stop pass");
    return 0;
}
