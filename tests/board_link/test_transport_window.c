#include "fake_hardware.h"

/* Both endpoints expose application receive and ACK events on 50 ms ticks.
 * The ACK is observed on the following dispatch tick. This deliberately makes
 * the queueing cost visible, rather than replacing it with immediate ACKs. */
#define DISPATCH_US INT64_C(50000)
#define PERF_START INT64_C(1000000)
typedef struct {
    mosaico_network_message_t frame;
    int64_t receive_at, ack_at;
    bool active, received;
} flight_t;
static bool idle_read(void *ctx, echoy_link_view_t *out) { (void)ctx; (void)out; return false; }
static bool accept(void *ctx, const board_link_update_t *u) { (void)ctx; (void)u; return true; }
static board_link_t *transport_node(bool follower)
{
    board_link_t *h = calloc(1, sizeof(*h));
    assert(h);
    h->read = idle_read; h->receive = accept; h->network = (void *)h;
    h->attached = h->sensors_ready = true; h->following = follower;
    h->peer = follower ? 1 : 2; h->session = 987;
    return h;
}
static void fixture_plan(echoy_plan_t *p)
{
    *p = (echoy_plan_t){.role = ECHOY_PULSE, .phrase = 1, .density = .5, .duration = 2.5,
                        .source_count = 3, .count = 10};
    for (unsigned i = 0; i < 3; ++i)
        p->source[i] = (echoy_seed_note_t){.step = i * 4, .velocity = .74, .kind = ECHOY_KICK};
    for (unsigned i = 0; i < 10; ++i)
        p->events[i] = (echoy_note_t){.step = i * 1.5, .velocity = .5, .duration = .1,
            .max_duration = .2, .expression = .8, .note = 60, .kind = ECHOY_KICK,
            .layer = ECHOY_ANCHOR};
}
static board_link_t *source(void)
{
    board_link_t *h = transport_node(false);
    h->have_view = h->peer_seen = true;
    h->view.available = h->view.clock_started = h->view.proposal_pending = true;
    h->view.clock_origin_us = 700000;
    h->view.requested_revision = h->view.proposal_revision = 11;
    fixture_plan(&h->view.proposal);
    assert(begin_transaction(h, PERF_START));
    assert(h->tx.size == 693);
    return h;
}
static void acknowledgement(board_link_t *h, const mosaico_network_message_t *m, bool ok, int64_t now)
{
    link_event_t e = {.received_us = now, .event = {
        .type = ok ? MOSAICO_NETWORK_EVENT_MESSAGE_DELIVERED : MOSAICO_NETWORK_EVENT_MESSAGE_FAILED,
        .data.delivery = {.destination_id = m->destination_id, .service_id = m->service_id,
                          .error = ok ? ESP_OK : ESP_FAIL}}};
    process_event(h, &e);
}
static unsigned active_flights(const flight_t *f, unsigned *chunks)
{
    unsigned count = 0; *chunks = 0;
    for (unsigned i = 0; i < 3; ++i) if (f[i].active) {
        ++count; *chunks += f[i].frame.payload[1] == WIRE_CHUNK;
    }
    return count;
}
/* Compatibility oracle: the original v2 receiver required serial offsets. */
static void legacy_chunk(board_link_t *h, const mosaico_network_message_t *m, int64_t now)
{
    const uint8_t *p = m->payload;
    uint16_t offset = get16(p + 18);
    size_t bytes = m->payload_len - 20;
    if (!h->rx.metadata || h->rx.complete || get32(p + 8) != h->rx.revision ||
        get32(p + 12) != h->rx.crc || get16(p + 16) != h->rx.size ||
        !bytes || offset + bytes > h->rx.size) return;
    if (!offset) h->rx.offset = 0;
    if (offset != h->rx.offset) return;
    memcpy(h->rx.bytes + offset, p + 20, bytes);
    h->rx.offset += (uint16_t)bytes;
    h->rx.next_want = now + LINK_RX_TIMEOUT_US;
    if (h->rx.offset != h->rx.size) return;
    if (echoy_link_crc32(h->rx.bytes, h->rx.size) != h->rx.crc ||
        !echoy_link_plan_decode(h->rx.bytes, h->rx.size, &h->received_plan)) {
        h->rx.offset = 0; h->rx.next_want = now + LINK_RETRY_US; return;
    }
    h->rx.complete = true;
}
static int64_t measure_transfer(bool serialize, bool reorder, bool lose_one, bool legacy)
{
    board_link_t *a = source(), *b = transport_node(true);
    flight_t flights[3] = {0};
    unsigned peak = 0, peak_chunks = 0;
    bool dropped = false;
    int64_t done = 0;
    for (int64_t now = PERF_START; now < PERF_START + 15000000; now += 5000) {
        /* Reverse same-tick receive/ACK order to exercise offset reassembly. */
        for (unsigned j = 0; j < 3; ++j) {
            unsigned i = reorder ? 2 - j : j;
            flight_t *f = &flights[i];
            if (!f->active) continue;
            if (!f->received && now >= f->receive_at) {
                bool omit = lose_one && !dropped && f->frame.payload[1] == WIRE_CHUNK &&
                            get16(f->frame.payload + 18) == LINK_CHUNK_BYTES;
                if (omit) dropped = true;
                else {
                    if (legacy && f->frame.payload[1] == WIRE_CHUNK) legacy_chunk(b, &f->frame, now);
                    else receive_message(b, &f->frame, now);
                    if (reorder && !legacy) receive_message(b, &f->frame, now + 1); /* exact duplicate */
                }
                f->received = true;
            }
            if (now >= f->ack_at) {
                acknowledgement(a, &f->frame, true, now);
                f->active = false;
            }
        }
        unsigned chunks;
        unsigned active = active_flights(flights, &chunks);
        assert(active <= 3 && chunks <= 2);
        if (active > peak) peak = active;
        if (chunks > peak_chunks) peak_chunks = chunks;
        if (!a->tx.active && b->rx.complete && !active) { done = now; break; }
        if (b->rx.metadata && !b->rx.complete && !a->tx.active && now >= b->rx.next_want) {
            mosaico_network_message_t want = {.source_id = a->peer, .service_id = LINK_SERVICE_BASE,
                                             .payload_len = 16};
            want.payload[0] = LINK_WIRE_VERSION; want.payload[1] = WIRE_WANT;
            put32(want.payload + 4, a->session); put32(want.payload + 8, b->rx.revision);
            put32(want.payload + 12, b->rx.crc);
            receive_message(a, &want, now);
            b->rx.next_want = now + LINK_RX_TIMEOUT_US;
        }
        if (!serialize || !active) {
            fake_now = now; send_next(a, now);
            if (have_packet) {
                unsigned slot = 0;
                while (slot < 3 && flights[slot].active) ++slot;
                assert(slot < 3 && packet_sender == a);
                int64_t received = ((now + 1000 + DISPATCH_US - 1) / DISPATCH_US) * DISPATCH_US;
                flights[slot] = (flight_t){.frame = packet, .receive_at = received,
                                          .ack_at = received + DISPATCH_US, .active = true};
                have_packet = false;
            }
        }
    }
    assert(done && b->rx.complete && a->tx.offset == a->tx.size);
    uint8_t encoded[ECHOY_LINK_PLAN_MAX_BYTES];
    assert(echoy_link_plan_encode(&b->received_plan, encoded, sizeof(encoded)) == 693);
    assert(!memcmp(encoded, a->tx.bytes, 693));
    if (lose_one) assert(dropped);
    printf("transport-window: serial=%u reorder=%u lost=%u legacy=%u bytes=693 elapsed_us=%lld in_flight=%u chunks=%u\n",
           serialize, reorder, lose_one, legacy, (long long)(done - PERF_START), peak, peak_chunks);
    fflush(stdout);
    free(a); free(b);
    return done - PERF_START;
}

static mosaico_network_message_t send_frame(board_link_t *h, int64_t now, unsigned kind)
{
    fake_now = now; send_next(h, now);
    assert(have_packet && packet_sender == h && packet.payload[1] == kind);
    mosaico_network_message_t result = packet;
    have_packet = false;
    return result;
}
static void no_frame(board_link_t *h, int64_t now)
{
    fake_now = now; send_next(h, now); assert(!have_packet);
}
static mosaico_network_message_t meta_acked(board_link_t *h)
{
    mosaico_network_message_t meta = send_frame(h, 1000000, WIRE_META);
    acknowledgement(h, &meta, true, 1100000);
    return meta;
}
static void test_control_lane_and_out_of_order_ack(void)
{
    board_link_t *a = source(), *b = transport_node(true);
    mosaico_network_message_t meta = meta_acked(a);
    receive_message(b, &meta, 1100000);
    mosaico_network_message_t first = send_frame(a, 1105000, WIRE_CHUNK);
    mosaico_network_message_t second = send_frame(a, 1110000, WIRE_CHUNK);
    assert(get16(first.payload + 18) == 0 && get16(second.payload + 18) == 44);
    a->clock.pong_pending = true; a->clock.pong_t1 = a->clock.pong_t2 = 1000000;
    mosaico_network_message_t pong = send_frame(a, 1115000, WIRE_PONG);
    no_frame(a, 1120000); /* Two chunks plus one control, never a fourth frame. */
    receive_message(b, &second, 1121000);
    receive_message(b, &first, 1122000);
    receive_message(b, &first, 1123000);
    assert(b->rx.offset == 88 && !b->rx.complete);
    acknowledgement(a, &second, true, 1124000);
    mosaico_network_message_t third = send_frame(a, 1125000, WIRE_CHUNK);
    assert(get16(third.payload + 18) == 88); /* Control ACK cannot block data either. */
    acknowledgement(a, &second, true, 1126000);
    assert(a->tx.offset == 44);
    acknowledgement(a, &first, true, 1127000);
    acknowledgement(a, &pong, true, 1128000);
    acknowledgement(a, &third, true, 1129000);
    assert(a->tx.offset == 132 && a->tx.queued == 132);
    free(a); free(b);
}
static void test_retry_and_late_ack_keep_old_slots_reserved(void)
{
    board_link_t *a = source();
    meta_acked(a);
    mosaico_network_message_t first = send_frame(a, 1105000, WIRE_CHUNK);
    mosaico_network_message_t second = send_frame(a, 1110000, WIRE_CHUNK);
    uint32_t transfer = a->tx.transfer;
    acknowledgement(a, &first, false, 1120000);
    assert(a->tx.transfer != transfer && !a->tx.offset && !a->tx.queued && a->chunks[1].active);
    no_frame(a, 1200000);
    mosaico_network_message_t meta = send_frame(a, 1880000, WIRE_META);
    acknowledgement(a, &meta, true, 1890000);
    mosaico_network_message_t retried = send_frame(a, 1895000, WIRE_CHUNK);
    assert(get16(retried.payload + 18) == 0);
    no_frame(a, 1900000); /* The other old-generation chunk still owns its slot. */
    acknowledgement(a, &second, true, 1905000);
    assert(!a->tx.offset);
    mosaico_network_message_t retried_second = send_frame(a, 1910000, WIRE_CHUNK);
    acknowledgement(a, &first, true, 1915000); /* Old service cannot free its reused slot. */
    assert(!a->tx.offset && a->chunks[0].active && a->chunks[1].active);
    acknowledgement(a, &retried_second, true, 1920000);
    acknowledgement(a, &retried, true, 1925000);
    assert(a->tx.offset == 88);
    free(a);
}
static void test_want_serial_fallback_waits_for_old_slot(void)
{
    board_link_t *a = source();
    meta_acked(a);
    mosaico_network_message_t first = send_frame(a, 1105000, WIRE_CHUNK);
    mosaico_network_message_t second = send_frame(a, 1110000, WIRE_CHUNK);
    acknowledgement(a, &first, true, 1120000);
    mosaico_network_message_t want = {.source_id = a->peer, .service_id = LINK_SERVICE_BASE,
                                     .payload_len = 16};
    want.payload[0] = LINK_WIRE_VERSION; want.payload[1] = WIRE_WANT;
    put32(want.payload + 4, a->session); put32(want.payload + 8, a->tx.revision);
    put32(want.payload + 12, a->tx.crc);
    int64_t began = a->transaction.began;
    receive_message(a, &want, 1130000);
    assert(a->tx.serial_retry && a->chunks[1].active && a->transaction.began == began);
    mosaico_network_message_t meta = send_frame(a, 1135000, WIRE_META);
    acknowledgement(a, &meta, true, 1145000);
    no_frame(a, 1150000); /* Serial fallback counts old slot 1, not just slot 0. */
    acknowledgement(a, &second, true, 1155000);
    assert(!a->tx.offset);
    mosaico_network_message_t retry = send_frame(a, 1160000, WIRE_CHUNK);
    assert(get16(retry.payload + 18) == 0);
    no_frame(a, 1165000);
    assert(begin_transaction(a, 1300000));
    assert(!a->tx.serial_retry); /* A new T restores the two-chunk window. */
    free(a);
}
static void test_detach_rejoin_does_not_reuse_live_slots(void)
{
    board_link_t *a = source();
    meta_acked(a);
    mosaico_network_message_t first = send_frame(a, 1105000, WIRE_CHUNK);
    mosaico_network_message_t second = send_frame(a, 1110000, WIRE_CHUNK);
    detach(a);
    assert(a->chunks[0].active && a->chunks[1].active);
    a->contacts = MOSAICO_EDGE_MASK_RIGHT;
    mosaico_network_neighbor_event_t n = {.peer_id = 3, .session_id = 988,
        .local_edge = MOSAICO_EDGE_RIGHT, .peer_edge = MOSAICO_EDGE_LEFT};
    fake_now = 1200000; attach(a, &n);
    assert(a->attached && a->peer == 3 && a->session == 988);
    a->have_view = a->peer_seen = true;
    assert(begin_transaction(a, 1200000));
    mosaico_network_message_t meta = send_frame(a, 1200000, WIRE_META);
    acknowledgement(a, &meta, true, 1300000);
    no_frame(a, 1305000);
    acknowledgement(a, &second, true, 1310000); /* Old peer ACK only retires its old slot. */
    mosaico_network_message_t current = send_frame(a, 1315000, WIRE_CHUNK);
    assert(current.destination_id == 3 && get16(current.payload + 18) == 0);
    acknowledgement(a, &first, true, 1320000);
    acknowledgement(a, &second, true, 1325000);
    assert(!a->tx.offset);
    acknowledgement(a, &current, true, 1330000);
    assert(a->tx.offset == 44);
    free(a);
}
static void test_missing_callback_then_late_ack_is_bounded(void)
{
    board_link_t *a = source();
    meta_acked(a);
    mosaico_network_message_t lost = send_frame(a, 1105000, WIRE_CHUNK);
    mosaico_network_message_t second = send_frame(a, 1110000, WIRE_CHUNK);
    acknowledgement(a, &second, true, 1200000);
    uint32_t generation = a->tx.transfer;
    no_frame(a, 3610000); /* Original 2.5-second lost-callback ceiling. */
    assert(a->tx.transfer != generation && !a->tx.offset && !a->tx.queued);
    mosaico_network_message_t meta = send_frame(a, 4370000, WIRE_META);
    acknowledgement(a, &meta, true, 4380000);
    mosaico_network_message_t retry = send_frame(a, 4390000, WIRE_CHUNK);
    acknowledgement(a, &lost, true, 4400000);
    assert(a->chunks[0].active && !a->tx.offset);
    acknowledgement(a, &retry, true, 4410000);
    assert(a->tx.offset == 44);
    free(a);
}
static mosaico_network_message_t fragment(board_link_t *a, unsigned offset, unsigned count)
{
    assert(count && count <= 44 && offset + count <= a->tx.size);
    mosaico_network_message_t m = {.source_id = 1, .service_id = LINK_SERVICE_BASE,
                                  .payload_len = (uint8_t)(20 + count)};
    m.payload[0] = LINK_WIRE_VERSION; m.payload[1] = WIRE_CHUNK;
    put32(m.payload + 4, a->session); put32(m.payload + 8, a->tx.revision);
    put32(m.payload + 12, a->tx.crc); put16(m.payload + 16, a->tx.size);
    put16(m.payload + 18, (uint16_t)offset); memcpy(m.payload + 20, a->tx.bytes + offset, count);
    return m;
}
static void test_arbitrary_offsets_conflicts_and_duplicates(void)
{
    board_link_t *a = source(), *b = transport_node(true);
    mosaico_network_message_t meta = meta_acked(a);
    receive_message(b, &meta, 1100000);
    for (unsigned end = a->tx.size; end;) {
        unsigned count = end > 31 ? 31 : end;
        end -= count;
        mosaico_network_message_t part = fragment(a, end, count);
        receive_message(b, &part, 1110000 + end);
        uint16_t coverage = b->rx.offset;
        receive_message(b, &part, 1120000 + end);
        assert(b->rx.offset == coverage);
    }
    assert(b->rx.complete && b->rx.offset == 693 && !memcmp(b->rx.bytes, a->tx.bytes, 693));
    free(b); b = transport_node(true); receive_message(b, &meta, 1100000);
    mosaico_network_message_t first = fragment(a, 0, 44);
    receive_message(b, &first, 1110000);
    mosaico_network_message_t conflict = fragment(a, 30, 40);
    conflict.payload[20] ^= 0x80;
    receive_message(b, &conflict, 1120000);
    assert(!b->rx.complete && !b->rx.offset && !b->update.valid);
    for (unsigned offset = 0; offset < a->tx.size; offset += 44) {
        unsigned count = a->tx.size - offset;
        if (count > 44) count = 44;
        mosaico_network_message_t part = fragment(a, offset, count);
        receive_message(b, &part, 1200000 + offset);
    }
    assert(b->rx.complete && !memcmp(b->rx.bytes, a->tx.bytes, 693));
    free(a); free(b);
}

int main(void)
{
    int64_t serial = measure_transfer(true, false, false, false);
    int64_t window = measure_transfer(false, true, false, false);
    /* Observable throughput, independently counted by the delayed transport. */
    assert(window * 4 <= serial * 3);
    measure_transfer(false, true, true, false);
    measure_transfer(false, true, true, true);
    test_control_lane_and_out_of_order_ack();
    test_retry_and_late_ack_keep_old_slots_reserved();
    test_want_serial_fallback_waits_for_old_slot();
    test_detach_rejoin_does_not_reuse_live_slots();
    test_missing_callback_then_late_ack_is_bounded();
    test_arbitrary_offsets_conflicts_and_duplicates();
    puts("PASS control priority, bounded slots, stale ACKs across retries/rejoin/timeouts and offset coverage");
    puts("PASS two-chunk window improves delayed-dispatch throughput with reordering, duplicate and missing-fragment repair");
    return 0;
}
