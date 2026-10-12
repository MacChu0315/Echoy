#include "board_link.h"
#include "echoy_link_codec.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/magnetometer.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mosaico_mag_classifier.h"
#include "mosaico_network.h"

/* Private, little-endian v2 protocol. Transport delivery is never musical
 * readiness: owner snapshots acknowledge preparation, arm and authorization.
 * A service ID identifies exactly one outstanding transport operation. */
#define LINK_NETWORK_ID UINT32_C(0x45434859)
#define LINK_SERVICE_BASE UINT16_C(0xe000)
#define LINK_SERVICE_MASK UINT16_C(0xf000)
#define LINK_WIRE_VERSION 2
#define LINK_CHUNK_BYTES 44
#define LINK_PERIOD_US INT64_C(2500000)
#define LINK_RETRY_US INT64_C(750000)
#define LINK_META_US INT64_C(500000)
#define LINK_STATE_US INT64_C(100000)
#define LINK_RX_TIMEOUT_US INT64_C(3000000)
#define LINK_PREPARE_US INT64_C(30000000)
#define LINK_LEAD_US INT64_C(1000000)
#define LINK_GUARD_US INT64_C(200000)
#define LINK_CLOCK_MAX_RTT_US INT64_C(80000)

enum { WIRE_PING = 1, WIRE_PONG, WIRE_META, WIRE_CHUNK, WIRE_WANT,
       WIRE_STATE, WIRE_ARM, WIRE_COMMIT, WIRE_ABORT };
enum { META_CLOCK = 1, META_LOOP = 2 };
enum { STATE_DEFERRED = 1, STATE_READY = 2, STATE_FAILED = 4,
       STATE_ARMED = 8, STATE_AUTHORIZED = 16, STATE_COMMITTED = 32 };
typedef enum { TX_IDLE, TX_PREPARING, TX_REMOTE_ARM, TX_LOCAL_ARM,
               TX_REMOTE_COMMIT, TX_LOCAL_COMMIT, TX_COMMITTING,
               TX_DONE, TX_ABORTED } transaction_phase_t;

typedef struct { mosaico_network_event_t event; int64_t received_us; } link_event_t;
typedef struct {
    board_link_read_fn read;
    board_link_receive_fn receive;
    void *ctx;
    mosaico_network_handle_t network;
    QueueHandle_t events;
    atomic_bool overflow, stopping, worker_exited;
    mosaico_mag_classifier_t classifier;
    mosaico_mag_presence_tracker_t presence;
    bool sensors_ready, sensors_initialized, attached, following, publish_pending;
    bool recovering, have_view, peer_seen, peer_deferred, recovery_requested;
    mosaico_edge_mask_t contacts, reported_contacts;
    mosaico_edge_t edge;
    uint64_t peer;
    uint32_t session, serial, connection_generation;
    int64_t next_sensor, next_read, next_send, next_state, peer_seen_at, attached_at;
    /* All complete plans and byte arrays are heap-owned, never worker locals. */
    echoy_link_view_t view;
    board_link_update_t update;
    echoy_plan_t received_plan;
    uint8_t scratch[ECHOY_LINK_PLAN_MAX_BYTES];
    struct {
        uint8_t bytes[ECHOY_LINK_PLAN_MAX_BYTES];
        uint32_t revision, crc, transfer;
        uint16_t size, offset;
        bool active, clock_started, looping;
        int64_t origin, next_meta;
    } tx;
    struct {
        uint8_t bytes[ECHOY_LINK_PLAN_MAX_BYTES];
        uint32_t revision, crc, complete_revision;
        uint64_t bound_revision;
        uint16_t size, offset;
        bool metadata, complete, clock_started, looping;
        int64_t origin, last_progress, next_want;
    } rx;
    struct {
        transaction_phase_t phase;
        uint32_t id;
        uint64_t revision, requested_revision, peer_revision;
        int64_t began, at, peer_at, next_control;
        uint8_t peer_flags;
        bool failed, commit_received;
    } transaction;
    struct {
        bool valid, waiting, pong_pending;
        uint32_t sequence, pending_sequence, pong_sequence;
        unsigned samples;
        int64_t t1, next_ping, best_rtt, best_offset, offset, window_start;
        int64_t pong_t1, pong_t2, last_log;
    } clock;
    struct {
        bool active;
        uint16_t service, sequence, bytes;
        uint8_t kind;
        uint32_t revision, transfer;
        int64_t since;
    } pending;
} board_link_t;

static const char *TAG = "echoy-link";
static atomic_bool owned;
static board_link_t *instance;

static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16; }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }
static int64_t get_time(const uint8_t *p)
{ uint64_t n = get64(p); return n <= INT64_MAX / 4 ? (int64_t)n : -1; }
static void put16(uint8_t *p, uint16_t n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static void put32(uint8_t *p, uint32_t n) { put16(p, (uint16_t)n); put16(p + 2, (uint16_t)(n >> 16)); }
static void put64(uint8_t *p, uint64_t n) { put32(p, (uint32_t)n); put32(p + 4, (uint32_t)(n >> 32)); }
static void put_time(uint8_t *p, int64_t n) { put64(p, (uint64_t)n); }
static bool newer(uint32_t a, uint32_t b)
{ return a != b && (uint32_t)(a - b) < UINT32_C(0x80000000); }

static int64_t mapped_origin(board_link_t *h)
{
    int64_t origin = h->rx.origin + h->clock.offset;
    if (origin < 0) { origin %= LINK_PERIOD_US; if (origin < 0) origin += LINK_PERIOD_US; }
    return origin;
}

static void publish_attachment(board_link_t *h)
{
    /* The desired snapshot carries the command until owner read-back proves it
     * was consumed. Metadata changes cannot overwrite an unobserved command. */
    h->update.attached = h->attached;
    h->update.following = h->attached && h->following;
    h->update.recovering = h->recovering;
    h->update.hold_changes = h->attached && !h->following && !h->peer_deferred &&
                             h->transaction.phase != TX_ABORTED;
    h->update.session = h->session;
    h->update.connection_generation = h->connection_generation;
    if (!h->attached) {
        h->update.command = ECHOY_LINK_NONE;
        h->update.valid = h->update.clock_started = h->update.looping = false;
        h->update.transaction = 0;
        h->update.clock_origin_us = h->update.commit_at_us = 0;
        h->update.failed = false;
    } else if (h->following) {
        h->update.clock_started = h->clock.valid && h->rx.metadata && h->rx.clock_started;
        h->update.looping = h->rx.looping;
        if (h->update.clock_started) h->update.clock_origin_us = mapped_origin(h);
    } else if (h->have_view) {
        h->update.clock_started = h->view.clock_started;
        h->update.clock_origin_us = h->view.clock_origin_us;
        h->update.looping = h->view.looping;
    }
    h->publish_pending = true;
}

static void command(board_link_t *h, echoy_link_command_t cmd, uint64_t revision)
{
    h->update.command = cmd;
    h->update.transaction = h->transaction.id;
    h->update.revision = revision;
    h->update.commit_at_us = h->transaction.at;
    h->update.failed = h->transaction.failed;
    publish_attachment(h);
}

static void deliver_update(board_link_t *h)
{
    if (h->publish_pending && h->receive(h->ctx, &h->update)) h->publish_pending = false;
}

static void detach(board_link_t *h)
{
    if (!++h->connection_generation) ++h->connection_generation;
    if (h->attached) ESP_LOGI(TAG, "detached edge=%s peer=%012" PRIx64 " session=%" PRIu32,
                            mosaico_edge_to_string(h->edge), h->peer, h->session);
    h->attached = h->following = h->have_view = h->peer_seen = h->peer_deferred = false;
    h->recovery_requested = false;
    h->peer = h->session = 0;
    h->edge = MOSAICO_EDGE_NONE;
    h->pending.active = h->tx.active = false;
    h->tx.size = 0;
    memset(&h->transaction, 0, sizeof(h->transaction));
    memset(&h->rx, 0, sizeof(h->rx));
    memset(&h->clock, 0, sizeof(h->clock));
    publish_attachment(h);
}

static void abort_transaction(board_link_t *h, bool failed)
{
    /* Repeated wire ABORT/FAILED frames cannot recreate an owner command that
     * its copied read-back already acknowledged. */
    if (h->transaction.phase == TX_ABORTED && h->transaction.failed == failed) return;
    if (h->transaction.phase != TX_ABORTED)
        ESP_LOGW(TAG, "transaction=%" PRIu32 " aborted failed=%d at=%" PRId64,
                 h->transaction.id, failed, h->transaction.at);
    h->transaction.phase = TX_ABORTED;
    h->transaction.failed = failed;
    h->transaction.next_control = 0;
    h->tx.active = false;
    if (h->recovering != failed && !++h->connection_generation) ++h->connection_generation;
    h->recovering = failed;
    command(h, ECHOY_LINK_ABORT, h->transaction.revision);
    h->next_state = 0;
}

static void network_event(const mosaico_network_event_t *event, void *ctx)
{
    board_link_t *h = ctx;
    if (event->type == MOSAICO_NETWORK_EVENT_TOPOLOGY_CHANGED) {
        return;
    }
    /* The network task must never wait for sensor work or an application lock. */
    link_event_t copy = {.event = *event, .received_us = esp_timer_get_time()};
    if (xQueueSend(h->events, &copy, 0) != pdTRUE) {
        atomic_store_explicit(&h->overflow, true, memory_order_release);
    }
}

static void presence_event(const mosaico_mag_presence_event_t *event, void *ctx)
{
    board_link_t *h = ctx;
    h->contacts = event->attached_mask & (MOSAICO_EDGE_MASK_LEFT | MOSAICO_EDGE_MASK_RIGHT);
    if ((h->attached && !(h->contacts & mosaico_edge_to_mask(h->edge))) ||
        (!h->attached && h->recovering && !h->contacts)) {
        /* Detach immediately on physical release; do not wait for a radio ACK. */
        h->recovering = false;
        detach(h);
    }
}

static void report_contacts(board_link_t *h, int64_t now)
{
    const mosaico_edge_t edges[] = {MOSAICO_EDGE_LEFT, MOSAICO_EDGE_RIGHT};
    for (unsigned i = 0; i < 2; ++i) {
        mosaico_edge_mask_t bit = mosaico_edge_to_mask(edges[i]);
        if ((h->contacts ^ h->reported_contacts) & bit) {
            bool present = (h->contacts & bit) != 0;
            if (mosaico_network_set_contact(h->network, edges[i], present,
                                            (uint32_t)(now / 1000)) == ESP_OK) {
                h->reported_contacts ^= bit;
                ESP_LOGI(TAG, "magnetic %s edge=%s", present ? "contact" : "release",
                         mosaico_edge_to_string(edges[i]));
            }
        }
    }
}

static void disable_sensors(board_link_t *h, const char *reason)
{
    h->sensors_ready = false;
    h->contacts = 0;
    h->recovering = false;
    detach(h);
    ESP_LOGW(TAG, "link unavailable: %s; restart with tiles separated", reason);
}

static void sample_sensors(board_link_t *h, int64_t now)
{
    if (!h->sensors_ready || now < h->next_sensor) {
        return;
    }
    h->next_sensor = now + 40000;
    struct bmm150_mag_data right = {0}, left = {0};
    if (bsp_magnetometer_read(BSP_MAGNETOMETER_0, &right) != ESP_OK ||
        bsp_magnetometer_read(BSP_MAGNETOMETER_1, &left) != ESP_OK) {
        disable_sensors(h, "magnetometer read failed");
        return;
    }
    mosaico_mag_sample_t sample = {
        .right = {.valid = true, .x = right.x, .y = right.y, .z = right.z},
        .left = {.valid = true, .x = left.x, .y = left.y, .z = left.z},
        .timestamp_ms = (uint32_t)(now / 1000),
    };
    mosaico_mag_classification_t result = {0};
    mosaico_mag_calibration_state_t previous = h->classifier.calibration_state;
    if (mosaico_mag_classifier_process(&h->classifier, &sample, &result) != ESP_OK ||
        h->classifier.calibration_state == MOSAICO_MAG_CALIBRATION_FAILED) {
        disable_sensors(h, "magnetometer baseline/classification failed");
        return;
    }
    if (previous != MOSAICO_MAG_CALIBRATION_READY &&
        h->classifier.calibration_state == MOSAICO_MAG_CALIBRATION_READY) {
        ESP_LOGI(TAG, "magnetometer baseline ready; horizontal contact enabled");
    }
    if (mosaico_mag_presence_tracker_process(&h->presence, &result) != ESP_OK) {
        disable_sensors(h, "magnetic presence tracking failed");
    }
}


static void attach(board_link_t *h, const mosaico_network_neighbor_event_t *n)
{
    bool horizontal = (n->local_edge == MOSAICO_EDGE_RIGHT && n->peer_edge == MOSAICO_EDGE_LEFT) ||
                      (n->local_edge == MOSAICO_EDGE_LEFT && n->peer_edge == MOSAICO_EDGE_RIGHT);
    if (!h->sensors_ready || !horizontal || n->relative_rotation != 0 ||
        !(h->contacts & mosaico_edge_to_mask(n->local_edge)) || !n->session_id || !n->peer_id) return;
    if (h->attached) return;
    h->recovering = false;
    detach(h);
    h->attached = true;
    if (!++h->connection_generation) ++h->connection_generation;
    h->following = n->local_edge == MOSAICO_EDGE_LEFT;
    h->edge = n->local_edge;
    h->peer = n->peer_id;
    h->session = n->session_id;
    h->next_send = h->next_read = h->next_state = 0;
    h->attached_at = esp_timer_get_time();
    publish_attachment(h);
    ESP_LOGI(TAG, "attached edge=%s peer=%012" PRIx64 " session=%" PRIu32 " following=%d",
             mosaico_edge_to_string(h->edge), h->peer, h->session, h->following);
}

static void publish_received(board_link_t *h)
{
    if (!h->attached || !h->following || !h->clock.valid || !h->rx.complete ||
        !h->rx.metadata || !h->rx.clock_started || h->rx.complete_revision != h->transaction.id ||
        h->transaction.phase != TX_PREPARING) return;
    h->update.valid = true;
    h->update.plan = h->received_plan;
    command(h, ECHOY_LINK_OFFER, h->rx.bound_revision);
}

/* A fresh, validated peer response after an actual silence is a recovery
 * event. Continuous heartbeats after a failed preparation are not retries. */
static void note_peer(board_link_t *h, int64_t now)
{
    if (h->transaction.phase == TX_ABORTED &&
        (!h->peer_seen || now - h->peer_seen_at >= LINK_RX_TIMEOUT_US)) {
        h->recovery_requested = true;
        h->rx.next_want = 0;
    }
    h->peer_seen = true;
    h->peer_seen_at = now;
}

static void receive_pong(board_link_t *h, const uint8_t *p, int64_t t4)
{
    int64_t t1 = get_time(p + 12), t2 = get_time(p + 20), t3 = get_time(p + 28);
    if (!h->following || !h->clock.waiting || get32(p + 8) != h->clock.pending_sequence ||
        t1 != h->clock.t1 || t1 < 0 || t2 < 0 || t3 < t2 || t4 < t1) return;
    h->clock.waiting = false;
    note_peer(h, t4);
    int64_t rtt = (t4 - t1) - (t3 - t2);
    if (rtt < 0 || rtt > LINK_CLOCK_MAX_RTT_US || t4 - t1 > 250000) return;
    int64_t offset = ((t1 - t2) + (t4 - t3)) / 2;
    if (!h->clock.samples || rtt < h->clock.best_rtt) {
        h->clock.best_rtt = rtt;
        h->clock.best_offset = offset;
    }
    h->clock.samples++;
    if (h->clock.samples >= 3) {
        h->clock.valid = true;
        /* Freeze the mapping while a boundary is being negotiated. Changing
         * clock estimates then would make a duplicate ARM refer to a new time. */
        if (h->transaction.phase < TX_REMOTE_ARM || h->transaction.phase >= TX_DONE)
            h->clock.offset = h->clock.best_offset;
        h->clock.next_ping = t4 + 5000000;
        publish_received(h);
        publish_attachment(h);
    }
}

static void receive_meta(board_link_t *h, const uint8_t *p, int64_t now)
{
    uint32_t id = get32(p + 8), crc = get32(p + 16);
    uint16_t size = get16(p + 12);
    int64_t origin = get_time(p + 20);
    if (!h->following || !id || !size || size > ECHOY_LINK_PLAN_MAX_BYTES ||
        (p[14] & ~(META_CLOCK | META_LOOP)) || p[15] || origin < 0 ||
        (h->transaction.id && id != h->transaction.id && !newer(id, h->transaction.id))) return;
    if (id != h->transaction.id) {
        /* A newer complete proposal supersedes an uncommitted old session
         * candidate; the owner also checks transaction identity on all jobs. */
        memset(&h->transaction, 0, sizeof(h->transaction));
        h->transaction.id = id;
        h->transaction.phase = TX_PREPARING;
        h->transaction.began = now;
        h->recovery_requested = false;
        h->rx.offset = 0;
        h->rx.complete = false;
        h->rx.revision = id;
        h->rx.size = size;
        h->rx.crc = crc;
        h->rx.bound_revision = get64(p + 28);
        h->rx.last_progress = now;
        h->rx.next_want = now + LINK_RX_TIMEOUT_US;
        h->update.command = ECHOY_LINK_NONE;
        h->update.valid = false;
        if (h->recovering && !++h->connection_generation) ++h->connection_generation;
        h->recovering = false;
    } else if (h->rx.size != size || h->rx.crc != crc || h->rx.bound_revision != get64(p + 28)) return;
    h->rx.metadata = true;
    h->rx.clock_started = (p[14] & META_CLOCK) != 0;
    h->rx.looping = (p[14] & META_LOOP) != 0;
    h->rx.origin = origin;
    note_peer(h, now);
    publish_received(h);
    publish_attachment(h);
}

static void receive_chunk(board_link_t *h, const uint8_t *p, size_t length, int64_t now)
{
    uint16_t offset = get16(p + 18);
    size_t bytes = length - 20;
    if (!h->following || !h->rx.metadata || h->rx.complete || h->transaction.phase != TX_PREPARING ||
        get32(p + 8) != h->rx.revision || get32(p + 12) != h->rx.crc ||
        get16(p + 16) != h->rx.size || !bytes || offset + bytes > h->rx.size) return;
    if (!offset) h->rx.offset = 0;
    if (offset != h->rx.offset) return;
    memcpy(h->rx.bytes + offset, p + 20, bytes);
    h->rx.offset += (uint16_t)bytes;
    h->rx.last_progress = now;
    h->rx.next_want = now + LINK_RX_TIMEOUT_US;
    if (h->rx.offset != h->rx.size) return;
    if (echoy_link_crc32(h->rx.bytes, h->rx.size) != h->rx.crc ||
        !echoy_link_plan_decode(h->rx.bytes, h->rx.size, &h->received_plan)) {
        h->rx.offset = 0;
        h->rx.next_want = now + LINK_RETRY_US;
        return;
    }
    h->rx.complete = true;
    h->rx.complete_revision = h->rx.revision;
    publish_received(h);
}

static void receive_state(board_link_t *h, const uint8_t *p, int64_t now)
{
    if (h->following || (p[28] & ~(STATE_DEFERRED | STATE_READY | STATE_FAILED |
        STATE_ARMED | STATE_AUTHORIZED | STATE_COMMITTED)) || p[29] || p[30] || p[31]) return;
    note_peer(h, now);
    bool was_deferred = h->peer_deferred;
    h->peer_deferred = (p[28] & STATE_DEFERRED) != 0;
    if (h->peer_deferred) {
        if (h->transaction.phase > TX_IDLE && h->transaction.phase < TX_REMOTE_ARM)
            abort_transaction(h, false);
    } else if (was_deferred && h->transaction.phase == TX_ABORTED && !h->transaction.failed) {
        h->transaction.phase = TX_IDLE;
    }
    if (get32(p + 8) == h->transaction.id && h->transaction.id) {
        uint8_t flags = p[28];
        uint64_t revision = get64(p + 12);
        int64_t at = get_time(p + 20);
        /* READY may be withdrawn by a local edit. Never retain a previous
         * readiness bit across a newer snapshot or revision. */
        h->transaction.peer_flags = flags;
        if (h->transaction.phase == TX_PREPARING) h->transaction.peer_revision = revision;
        if (h->transaction.phase == TX_REMOTE_ARM &&
            (revision != h->transaction.peer_revision || !(flags & STATE_READY)) &&
            !(flags & (STATE_ARMED | STATE_AUTHORIZED | STATE_FAILED))) {
            /* A right-local edit replaced READY before it accepted ARM. Both
             * committed banks still play, so prepare the new revision. */
            h->transaction.phase = TX_PREPARING;
            h->transaction.peer_revision = revision;
            h->transaction.at = 0;
        } else if (h->transaction.phase >= TX_REMOTE_ARM && h->transaction.phase < TX_DONE &&
                   revision != h->transaction.peer_revision) {
            abort_transaction(h, true);
        }
        h->transaction.peer_at = at;
        if ((flags & STATE_FAILED) && h->transaction.phase != TX_DONE) abort_transaction(h, true);
    }
    publish_attachment(h);
}

static void receive_control(board_link_t *h, const uint8_t *p, int64_t now)
{
    if (!h->following || get32(p + 8) != h->transaction.id || !h->transaction.id) return;
    note_peer(h, now);
    if (p[1] == WIRE_ABORT) { abort_transaction(h, p[28] != 0); return; }
    if (h->transaction.phase == TX_ABORTED || h->transaction.phase == TX_DONE) return;
    uint64_t revision = get64(p + 12);
    int64_t remote_at = get_time(p + 20);
    if (!h->clock.valid || remote_at < 0) return;
    int64_t at = remote_at + h->clock.offset;
    if (p[1] == WIRE_ARM) {
        if (h->transaction.phase >= TX_REMOTE_ARM) {
            /* Retransmission cannot move or replace a frozen revision. */
            if (h->transaction.revision == revision && h->transaction.at == at) h->next_state = 0;
            return;
        }
        if (!h->have_view || !h->view.available || h->view.deferred || !h->view.offer_ready ||
            h->view.offered_transaction != h->transaction.id || h->view.offered_revision != revision ||
            now >= at - LINK_GUARD_US) { h->next_state = 0; return; }
        h->transaction.revision = revision;
        h->transaction.at = at;
        h->transaction.phase = TX_REMOTE_ARM;
        command(h, ECHOY_LINK_ARM, revision);
    } else if (p[1] == WIRE_COMMIT) {
        if (h->transaction.phase < TX_REMOTE_ARM || h->transaction.revision != revision ||
            h->transaction.at != at || h->view.armed_transaction != h->transaction.id ||
            h->view.armed_revision != revision || h->view.armed_at_us != at) return;
        if (!h->transaction.commit_received && now >= at - LINK_GUARD_US) {
            abort_transaction(h, true); return;
        }
        h->transaction.commit_received = true;
        h->transaction.phase = TX_COMMITTING;
        command(h, ECHOY_LINK_COMMIT, revision);
    }
}

static void receive_message(board_link_t *h, const mosaico_network_message_t *m, int64_t now)
{
    const uint8_t *p = m->payload;
    size_t n = m->payload_len;
    if (!h->attached || m->source_id != h->peer || n < 8 || n > 64 ||
        (m->service_id & LINK_SERVICE_MASK) != LINK_SERVICE_BASE ||
        p[0] != LINK_WIRE_VERSION || p[2] || p[3] || get32(p + 4) != h->session) return;
    switch (p[1]) {
    case WIRE_PING:
        if (!h->following && n == 20 && get_time(p + 12) >= 0) {
            h->clock.pong_pending = true;
            h->clock.pong_sequence = get32(p + 8);
            h->clock.pong_t1 = get_time(p + 12);
            h->clock.pong_t2 = now;
        }
        break;
    case WIRE_PONG: if (n == 36) receive_pong(h, p, now); break;
    case WIRE_META: if (n == 36) receive_meta(h, p, now); break;
    case WIRE_CHUNK: if (n > 20) receive_chunk(h, p, n, now); break;
    case WIRE_STATE: if (n == 32) receive_state(h, p, now); break;
    case WIRE_ARM: case WIRE_COMMIT: case WIRE_ABORT:
        if (n == 32 && !p[29] && !p[30] && !p[31] &&
            ((p[1] == WIRE_ABORT && p[28] <= 1) || (p[1] != WIRE_ABORT && !p[28])))
            receive_control(h, p, now);
        break;
    case WIRE_WANT:
        if (!h->following && n == 16 && h->tx.size &&
            get32(p + 8) == h->tx.revision && get32(p + 12) == h->tx.crc) {
            if (h->transaction.phase == TX_ABORTED) {
                /* The receiver requests recovery once after owner/link health
                 * returns. A new T makes duplicate old WANTs harmless. */
                h->recovery_requested = true;
            } else if (h->transaction.phase == TX_PREPARING &&
                       (!h->tx.active || now >= h->next_send)) {
                h->tx.active = true; ++h->tx.transfer; h->tx.offset = 0; h->tx.next_meta = 0;
            }
        }
        break;
    default: break;
    }
}

static void delivery_failed(board_link_t *h, int64_t now)
{
    h->pending.active = false;
    h->next_send = now + LINK_RETRY_US;
    if (!h->following && h->transaction.phase == TX_PREPARING && h->tx.size) {
        h->tx.active = true; ++h->tx.transfer; h->tx.offset = 0; h->tx.next_meta = 0;
    }
}

static void process_event(board_link_t *h, const link_event_t *e)
{
    switch (e->event.type) {
    case MOSAICO_NETWORK_EVENT_NEIGHBOR_ATTACHED:
        attach(h, &e->event.data.neighbor);
        break;
    case MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED:
        if (h->attached && e->event.data.neighbor.peer_id == h->peer &&
            e->event.data.neighbor.session_id == h->session &&
            e->event.data.neighbor.local_edge == h->edge) {
            h->recovering = (h->contacts & mosaico_edge_to_mask(h->edge)) != 0;
            detach(h);
        }
        break;
    case MOSAICO_NETWORK_EVENT_MESSAGE_RECEIVED:
        receive_message(h, &e->event.data.message, e->received_us);
        break;
    case MOSAICO_NETWORK_EVENT_MESSAGE_DELIVERED:
    case MOSAICO_NETWORK_EVENT_MESSAGE_FAILED: {
        const mosaico_network_delivery_event_t *d = &e->event.data.delivery;
        if (!h->pending.active || d->service_id != h->pending.service ||
            d->destination_id != h->peer) {
            break;
        }
        if (e->event.type == MOSAICO_NETWORK_EVENT_MESSAGE_FAILED) {
            ESP_LOGW(TAG, "delivery failed kind=%u revision=%" PRIu32 " error=%s",
                     (unsigned)h->pending.kind, h->pending.revision, esp_err_to_name(d->error));
            delivery_failed(h, e->received_us);
            break;
        }
        h->pending.active = false;
        if (h->pending.kind == WIRE_CHUNK && h->pending.revision == h->tx.revision &&
            h->pending.transfer == h->tx.transfer) {
            h->tx.offset += h->pending.bytes;
            if (h->tx.offset == h->tx.size) {
                h->tx.active = false;
                ESP_LOGI(TAG, "sent plan peer=%012" PRIx64 " revision=%" PRIu32,
                         h->peer, h->tx.revision);
            }
        }
        break;
    }
    case MOSAICO_NETWORK_EVENT_TOPOLOGY_CONFLICT:
        /* Keep musical state local until a new physical contact handshake. */
        h->contacts = 0;
        h->recovering = true;
        detach(h);
        mosaico_mag_presence_tracker_reset(&h->presence);
        break;
    default:
        break;
    }
}


static bool begin_transaction(board_link_t *h, int64_t now)
{
    if (!h->have_view || !h->view.available || !h->view.clock_started || !h->peer_seen || h->peer_deferred)
        return false;
    bool proposed = h->view.proposal_pending;
    /* Revision zero is also the real initial empty arrangement. A compiled
     * empty proposal has canonical metadata; an uncompiled zeroed snapshot
     * fails the codec below. Do not confuse its revision with missing work. */
    if (proposed && h->view.proposal_revision != h->view.requested_revision) return false;
    const echoy_plan_t *plan = proposed ? &h->view.proposal : &h->view.current;
    size_t size = echoy_link_plan_encode(plan, h->scratch, sizeof(h->scratch));
    if (!size || size > UINT16_MAX) return false;
    memset(&h->transaction, 0, sizeof(h->transaction));
    if (!++h->serial) ++h->serial;
    h->transaction.id = h->serial;
    h->transaction.phase = TX_PREPARING;
    h->transaction.revision = proposed ? h->view.proposal_revision : 0;
    h->transaction.requested_revision = h->view.requested_revision;
    h->transaction.began = now;
    h->recovery_requested = false;
    h->tx.revision = h->serial;
    memcpy(h->tx.bytes, h->scratch, size);
    h->tx.size = (uint16_t)size;
    h->tx.crc = echoy_link_crc32(h->tx.bytes, size);
    h->tx.offset = 0; h->tx.active = true; ++h->tx.transfer; h->tx.next_meta = 0;
    if (h->recovering && !++h->connection_generation) ++h->connection_generation;
    h->recovering = false;
    h->update.valid = false;
    command(h, ECHOY_LINK_BEGIN, h->transaction.revision);
    ESP_LOGI(TAG, "proposed transaction=%" PRIu32 " revision=%" PRIu64 " bytes=%u",
             h->transaction.id, h->transaction.revision, (unsigned)h->tx.size);
    return true;
}

static bool owner_ready(const board_link_t *h)
{
    return h->have_view && h->view.available && h->view.offered_transaction == h->transaction.id &&
        h->view.offered_revision == h->transaction.revision && h->view.offer_ready &&
        !h->view.offer_failed && !h->view.deferred;
}

static bool owner_armed(const board_link_t *h)
{
    return h->have_view && h->view.armed_transaction == h->transaction.id &&
        h->view.armed_revision == h->transaction.revision &&
        h->view.armed_at_us == h->transaction.at;
}

static void advance_transaction(board_link_t *h, int64_t now)
{
    if (!h->attached) return;
    transaction_phase_t phase = h->transaction.phase;
    int64_t peer_at = h->peer_seen ? h->peer_seen_at : h->attached_at;
    if (now - peer_at >= LINK_PREPARE_US && !h->recovering) {
        abort_transaction(h, true);
    }
    phase = h->transaction.phase;
    if (phase > TX_IDLE && phase < TX_DONE) {
        if (now - h->transaction.began >= LINK_PREPARE_US ||
            (h->have_view && (h->view.failed_transaction == h->transaction.id ||
             (h->view.offered_transaction == h->transaction.id && h->view.offer_failed)))) {
            abort_transaction(h, true); return;
        }
        if (h->transaction.at && now >= h->transaction.at - LINK_GUARD_US &&
            h->view.authorized_transaction != h->transaction.id) {
            abort_transaction(h, true); return;
        }
    }
    if (h->transaction.phase == TX_ABORTED && h->update.command == ECHOY_LINK_ABORT &&
        h->have_view && (h->view.failed_transaction == h->transaction.id ||
         h->view.committed_transaction == h->transaction.id ||
         (!h->transaction.failed && h->view.offered_transaction != h->transaction.id)))
        command(h, ECHOY_LINK_NONE, h->transaction.revision);
    if (h->following) {
        if (h->transaction.phase == TX_REMOTE_ARM && !owner_armed(h) && h->have_view &&
            h->view.offered_transaction == h->transaction.id &&
            (!h->view.available || !h->view.offer_ready || h->view.offered_revision != h->transaction.revision)) {
            /* The ARM was based on a cached READY and the owner invalidated
             * that candidate before freezing it. Withdraw it explicitly. */
            h->transaction.phase = TX_PREPARING;
            h->transaction.at = 0;
            command(h, ECHOY_LINK_NONE, 0);
            h->next_state = 0;
        }
        if (h->transaction.phase == TX_PREPARING) {
            if (h->view.offered_transaction != h->transaction.id) publish_received(h);
            else if (h->update.command == ECHOY_LINK_OFFER) command(h, ECHOY_LINK_NONE, 0);
        } else if (h->transaction.phase == TX_REMOTE_ARM && owner_armed(h)) {
            command(h, ECHOY_LINK_NONE, h->transaction.revision);
        } else if (h->transaction.phase == TX_COMMITTING &&
                   h->view.authorized_transaction == h->transaction.id) {
            command(h, ECHOY_LINK_NONE, h->transaction.revision);
            if (h->view.committed_transaction == h->transaction.id) {
                h->transaction.phase = TX_DONE;
                h->recovering = false;
                ESP_LOGI(TAG, "committed transaction=%" PRIu32 " local-at=%" PRId64,
                         h->transaction.id, h->transaction.at);
            }
        }
        if (h->update.command != ECHOY_LINK_NONE) h->publish_pending = true;
        return;
    }
    if (h->peer_deferred) {
        if (h->transaction.phase == TX_ABORTED && !h->transaction.failed &&
            h->view.offered_transaction != h->transaction.id) command(h, ECHOY_LINK_NONE, 0);
        return;
    }
    bool requested_changed = h->have_view &&
        h->view.requested_revision != h->transaction.requested_revision;
    bool changed = h->have_view &&
        (requested_changed ||
         (h->view.proposal_pending && h->view.proposal_revision != h->transaction.revision));
    if (h->transaction.phase == TX_IDLE ||
        (h->transaction.phase == TX_ABORTED && (h->recovery_requested || requested_changed)) ||
        ((h->transaction.phase == TX_DONE || h->transaction.phase == TX_PREPARING ||
          ((h->transaction.phase == TX_REMOTE_ARM || h->transaction.phase == TX_LOCAL_ARM) &&
           !owner_armed(h))) && changed)) {
        begin_transaction(h, now);
    }
    phase = h->transaction.phase;
    switch (phase) {
    case TX_PREPARING:
        if (h->view.offered_transaction != h->transaction.id)
            command(h, ECHOY_LINK_BEGIN, h->transaction.revision);
        else if (h->update.command == ECHOY_LINK_BEGIN) command(h, ECHOY_LINK_NONE, 0);
        if (owner_ready(h) && (h->transaction.peer_flags & STATE_READY) &&
            !(h->transaction.peer_flags & (STATE_DEFERRED | STATE_FAILED))) {
            int64_t origin = h->view.clock_origin_us;
            int64_t earliest = now + LINK_LEAD_US;
            h->transaction.at = origin;
            if (earliest > origin) h->transaction.at +=
                ((earliest - origin + LINK_PERIOD_US - 1) / LINK_PERIOD_US) * LINK_PERIOD_US;
            h->transaction.phase = TX_REMOTE_ARM;
            h->transaction.next_control = 0;
            ESP_LOGI(TAG, "prepared transaction=%" PRIu32 " common-bar=%" PRId64,
                     h->transaction.id, h->transaction.at);
        }
        break;
    case TX_REMOTE_ARM:
        if ((h->transaction.peer_flags & STATE_ARMED) && h->transaction.peer_at == h->transaction.at) {
            h->transaction.phase = TX_LOCAL_ARM;
            command(h, ECHOY_LINK_ARM, h->transaction.revision);
        }
        break;
    case TX_LOCAL_ARM:
        if (owner_armed(h)) {
            h->transaction.phase = TX_REMOTE_COMMIT;
            h->transaction.next_control = 0;
            command(h, ECHOY_LINK_NONE, h->transaction.revision);
        }
        break;
    case TX_REMOTE_COMMIT:
        if ((h->transaction.peer_flags & STATE_AUTHORIZED) && h->transaction.peer_at == h->transaction.at) {
            h->transaction.phase = TX_LOCAL_COMMIT;
            command(h, ECHOY_LINK_COMMIT, h->transaction.revision);
        }
        break;
    case TX_LOCAL_COMMIT:
        if (h->view.authorized_transaction == h->transaction.id) {
            h->transaction.phase = TX_COMMITTING;
            command(h, ECHOY_LINK_NONE, h->transaction.revision);
        }
        break;
    case TX_COMMITTING:
        if (h->view.committed_transaction == h->transaction.id &&
            (h->transaction.peer_flags & STATE_COMMITTED)) {
            h->transaction.phase = TX_DONE;
            h->recovering = false;
            ESP_LOGI(TAG, "both committed transaction=%" PRIu32 " root-at=%" PRId64,
                     h->transaction.id, h->transaction.at);
            publish_attachment(h);
        }
        break;
    default: break;
    }
    if (h->update.command != ECHOY_LINK_NONE) h->publish_pending = true;
}

static void read_local(board_link_t *h, int64_t now)
{
    if (!h->attached) return;
    if (now >= h->next_read) {
        h->next_read = now + 50000;
        bool unavailable = h->have_view && !h->view.available;
        if (h->read(h->ctx, &h->view)) {
            if (unavailable && h->view.available && h->transaction.phase == TX_ABORTED) {
                h->recovery_requested = true;
                h->rx.next_want = 0;
            }
            h->have_view = true;
            if (!h->following) {
                if (h->tx.clock_started != h->view.clock_started || h->tx.looping != h->view.looping ||
                    h->tx.origin != h->view.clock_origin_us) h->tx.next_meta = 0;
                h->tx.clock_started = h->view.clock_started;
                h->tx.looping = h->view.looping;
                h->tx.origin = h->view.clock_origin_us;
            }
            publish_attachment(h);
        }
    }
    advance_transaction(h, now);
}

static uint8_t state_flags(board_link_t *h)
{
    uint8_t flags = h->have_view && h->view.deferred ? STATE_DEFERRED : 0;
    if (h->transaction.phase == TX_ABORTED && h->transaction.failed) flags |= STATE_FAILED;
    if (!h->have_view || !h->transaction.id) return flags;
    if (h->view.offered_transaction == h->transaction.id) {
        if (h->view.available && h->view.offer_ready && !h->view.deferred) flags |= STATE_READY;
        if (h->view.offer_failed) flags |= STATE_FAILED;
    }
    if (owner_armed(h)) flags |= STATE_ARMED;
    if (h->view.authorized_transaction == h->transaction.id) flags |= STATE_AUTHORIZED;
    if (h->view.committed_transaction == h->transaction.id) flags |= STATE_COMMITTED;
    if (h->view.failed_transaction == h->transaction.id) flags |= STATE_FAILED;
    return flags;
}

static void send_next(board_link_t *h, int64_t now)
{
    if (!h->attached) return;
    if (h->pending.active) {
        if (now - h->pending.since >= 2500000) delivery_failed(h, now);
        return;
    }
    if (now < h->next_send) return;
    uint8_t p[64] = {LINK_WIRE_VERSION};
    put32(p + 4, h->session);
    size_t length = 0;
    uint16_t chunk = 0;
    transaction_phase_t phase = h->transaction.phase;
    if (h->clock.pong_pending) {
        p[1] = WIRE_PONG;
        put32(p + 8, h->clock.pong_sequence);
        put_time(p + 12, h->clock.pong_t1); put_time(p + 20, h->clock.pong_t2); put_time(p + 28, now);
        length = 36;
    } else if (h->following && now >= h->clock.next_ping &&
               (!h->clock.waiting || now - h->clock.t1 >= 500000)) {
        if (now - h->clock.window_start >= 5000000) { h->clock.window_start = now; h->clock.samples = 0; }
        p[1] = WIRE_PING; put32(p + 8, ++h->clock.sequence); put_time(p + 12, now); length = 20;
    } else if (h->following && now >= h->next_state) {
        p[1] = WIRE_STATE;
        put32(p + 8, h->transaction.id);
        uint64_t revision = h->view.offered_transaction == h->transaction.id ? h->view.offered_revision : 0;
        put64(p + 12, revision);
        int64_t at = h->transaction.at ? h->transaction.at - h->clock.offset : 0;
        put_time(p + 20, at); p[28] = state_flags(h); length = 32;
    } else if (!h->following && h->transaction.id && now >= h->transaction.next_control &&
               (phase == TX_REMOTE_ARM || phase == TX_REMOTE_COMMIT || phase == TX_ABORTED)) {
        p[1] = phase == TX_REMOTE_ARM ? WIRE_ARM : phase == TX_REMOTE_COMMIT ? WIRE_COMMIT : WIRE_ABORT;
        put32(p + 8, h->transaction.id); put64(p + 12, h->transaction.peer_revision);
        put_time(p + 20, h->transaction.at); p[28] = phase == TX_ABORTED && h->transaction.failed;
        length = 32;
    } else if (!h->following && h->tx.size && now >= h->tx.next_meta) {
        p[1] = WIRE_META;
        put32(p + 8, h->tx.revision); put16(p + 12, h->tx.size);
        p[14] = (h->tx.clock_started ? META_CLOCK : 0) | (h->tx.looping ? META_LOOP : 0);
        put32(p + 16, h->tx.crc); put_time(p + 20, h->tx.origin);
        put64(p + 28, h->transaction.revision); length = 36;
    } else if (!h->following && h->tx.active && phase == TX_PREPARING) {
        chunk = h->tx.size - h->tx.offset;
        if (chunk > LINK_CHUNK_BYTES) chunk = LINK_CHUNK_BYTES;
        p[1] = WIRE_CHUNK; put32(p + 8, h->tx.revision); put32(p + 12, h->tx.crc);
        put16(p + 16, h->tx.size); put16(p + 18, h->tx.offset);
        memcpy(p + 20, h->tx.bytes + h->tx.offset, chunk); length = 20 + chunk;
    } else if (h->following && h->rx.metadata && now >= h->rx.next_want &&
               ((!h->rx.complete && phase == TX_PREPARING) ||
                (phase == TX_ABORTED && h->recovery_requested && h->view.available))) {
        p[1] = WIRE_WANT; put32(p + 8, h->rx.revision); put32(p + 12, h->rx.crc); length = 16;
    }
    if (!length) return;
    uint16_t service = LINK_SERVICE_BASE | (++h->pending.sequence & ~LINK_SERVICE_MASK);
    if (mosaico_network_send(h->network, h->peer, service, p, length) != ESP_OK) {
        delivery_failed(h, now); return;
    }
    h->pending.active = true; h->pending.service = service; h->pending.kind = p[1];
    h->pending.revision = h->tx.revision; h->pending.transfer = h->tx.transfer;
    h->pending.bytes = chunk; h->pending.since = now; h->next_send = now + 5000;
    if (p[1] == WIRE_PONG) h->clock.pong_pending = false;
    else if (p[1] == WIRE_PING) {
        h->clock.waiting = true; h->clock.pending_sequence = h->clock.sequence;
        h->clock.t1 = now; h->clock.next_ping = now + 150000;
    } else if (p[1] == WIRE_META) h->tx.next_meta = now + LINK_META_US;
    else if (p[1] == WIRE_WANT) h->rx.next_want = now + LINK_RX_TIMEOUT_US;
    else if (p[1] == WIRE_STATE) h->next_state = now + LINK_STATE_US;
    else h->transaction.next_control = now + LINK_STATE_US;
}

static void worker(void *arg)
{
    board_link_t *h = arg;
    ESP_LOGI(TAG, "calibrating magnetometers; keep tiles separated until baseline ready");
    /* init_all can initialize the first sensor before failing on the second;
     * BSP deinit is also safe for an uninitialized sensor. */
    h->sensors_initialized = true;
    h->sensors_ready = bsp_magnetometer_init_all() == ESP_OK;
    if (!h->sensors_ready) {
        disable_sensors(h, "magnetometer initialization failed");
    }
    while (!atomic_load_explicit(&h->stopping, memory_order_acquire)) {
        int64_t now = esp_timer_get_time();
        if (atomic_exchange_explicit(&h->overflow, false, memory_order_acq_rel)) {
            /* A dropped release cannot be reconstructed from proximity. Fail
             * closed and obtain fresh debounced contact/session events. */
            xQueueReset(h->events);
            h->contacts = 0;
            h->recovering = true;
            detach(h);
            mosaico_mag_presence_tracker_reset(&h->presence);
            ESP_LOGW(TAG, "event queue overflow; restarting physical contact handshake");
        }
        link_event_t event;
        for (unsigned i = 0; i < 16 && xQueueReceive(h->events, &event, 0) == pdTRUE; ++i) {
            process_event(h, &event);
        }
        sample_sensors(h, now);
        report_contacts(h, now);
        read_local(h, now);
        deliver_update(h);
        send_next(h, esp_timer_get_time());
        vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
    }
    h->contacts = 0;
    report_contacts(h, esp_timer_get_time());
    if (h->sensors_initialized) {
        esp_err_t right = bsp_magnetometer_deinit(BSP_MAGNETOMETER_0);
        esp_err_t left = bsp_magnetometer_deinit(BSP_MAGNETOMETER_1);
        if (right != ESP_OK || left != ESP_OK) {
            ESP_LOGW(TAG, "magnetometer cleanup right=%s left=%s", esp_err_to_name(right),
                     esp_err_to_name(left));
        }
    }
    /* No further h/ctx access after publishing this release flag. */
    atomic_store_explicit(&h->worker_exited, true, memory_order_release);
    vTaskDelete(NULL);
}

esp_err_t board_link_start(board_link_read_fn read, board_link_receive_fn receive, void *ctx)
{
    if (!read || !receive) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_exchange_explicit(&owned, true, memory_order_acq_rel)) {
        return ESP_ERR_INVALID_STATE;
    }
    /* This cold context contains four full plans and three wire buffers.
     * Internal RAM must remain available for the PCM task and radio queues. */
#ifdef ESP_PLATFORM
    board_link_t *h = heap_caps_calloc(1, sizeof(*h), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    board_link_t *h = calloc(1, sizeof(*h));
#endif
    if (!h) {
        atomic_store(&owned, false);
        return ESP_ERR_NO_MEM;
    }
    h->read = read;
    h->receive = receive;
    h->ctx = ctx;
    atomic_init(&h->overflow, false);
    atomic_init(&h->stopping, false);
    atomic_init(&h->worker_exited, false);
    h->events = xQueueCreate(32, sizeof(link_event_t));
    esp_err_t error = h->events ? ESP_OK : ESP_ERR_NO_MEM;
    if (error == ESP_OK) {
        error = mosaico_mag_classifier_init(&h->classifier, &MOSAICO_MAG_CALIBRATION_S31_V1);
    }
    if (error == ESP_OK) {
        error = mosaico_mag_presence_tracker_init(&h->presence, NULL, presence_event, h);
    }
    if (error == ESP_OK) {
        mosaico_network_config_t config = MOSAICO_NETWORK_CONFIG_DEFAULT();
        config.network_id = LINK_NETWORK_ID;
        config.app_retry_interval_ms = 150;
        config.app_retry_count = 5;
        config.event_cb = network_event;
        config.user_ctx = h;
        /* RF calibration and NVS complete here, before the caller starts PCM. */
        error = mosaico_network_start(&config, &h->network);
    }
    if (error == ESP_OK) {
        publish_attachment(h);
        if (xTaskCreate(worker, "echoy_link", 7168, h, tskIDLE_PRIORITY + 1, NULL) == pdPASS) {
            instance = h;
            return ESP_OK;
        }
        atomic_store(&h->worker_exited, true);
        error = ESP_ERR_NO_MEM;
        /* No application task exists yet; callbacks stop before h is freed. */
        esp_err_t stop_error = mosaico_network_stop(h->network);
        if (stop_error != ESP_OK) {
            /* Retain callback storage if the transport cannot stop safely. */
            ESP_LOGE(TAG, "network cleanup failed: %s", esp_err_to_name(stop_error));
            instance = h;
            return stop_error;
        }
    }
    if (h->events) {
        vQueueDelete(h->events);
    }
    free(h);
    atomic_store(&owned, false);
    return error;
}

esp_err_t board_link_stop(uint32_t timeout_ms)
{
    if (timeout_ms > 60000) {
        return ESP_ERR_INVALID_ARG;
    }
    board_link_t *h = instance;
    if (!h) {
        return ESP_OK;
    }
    atomic_store_explicit(&h->stopping, true, memory_order_release);
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (!atomic_load_explicit(&h->worker_exited, memory_order_acquire)) {
        if (esp_timer_get_time() >= deadline) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    /* The BSP stop joins its protocol task and peer callbacks. Keep the queue
     * and callback storage alive until its public shutdown returns success. */
    esp_err_t error = mosaico_network_stop(h->network);
    if (error != ESP_OK) {
        return error;
    }
    vQueueDelete(h->events);
    instance = NULL;
    free(h);
    atomic_store_explicit(&owned, false, memory_order_release);
    return ESP_OK;
}
