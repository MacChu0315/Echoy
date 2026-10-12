#include "board_link.h"
#include "echoy_link_codec.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/magnetometer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mosaico_mag_classifier.h"
#include "mosaico_network.h"

/* Private application wire format, little endian. The network's own reliable
 * ACK acknowledges receipt, not decoding: WANT also repairs a lost application
 * callback or an incomplete/rejected plan. All application sends are serialized.
 * Each send has a different service ID because the public send API does not
 * return its message ID. A late delivery from an old session cannot advance the
 * current transfer. Reuse is more than 20 seconds apart even at maximum pacing. */
#define LINK_NETWORK_ID UINT32_C(0x45434859) /* ECHY: separate from demos. */
#define LINK_SERVICE_BASE UINT16_C(0xe000)
#define LINK_SERVICE_MASK UINT16_C(0xf000)
#define LINK_WIRE_VERSION 1
#define LINK_CHUNK_BYTES 44
#define LINK_PERIOD_US INT64_C(2500000)
#define LINK_RETRY_US INT64_C(750000)
#define LINK_META_US INT64_C(500000)
#define LINK_RX_TIMEOUT_US INT64_C(3000000)
#define LINK_CLOCK_MAX_RTT_US INT64_C(80000)

enum { WIRE_PING = 1, WIRE_PONG, WIRE_META, WIRE_CHUNK, WIRE_WANT };
enum { META_CLOCK = 1, META_LOOP = 2 };

typedef struct {
    mosaico_network_event_t event;
    int64_t received_us;
} link_event_t;

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
    mosaico_edge_mask_t contacts, reported_contacts;
    mosaico_edge_t edge;
    uint64_t peer;
    uint32_t session;
    int64_t next_sensor, next_read, next_send;
    /* Large views/plans/buffers never reside on the worker's 7 KiB stack. */
    echoy_control_view_t view;
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
        uint16_t size, offset;
        bool metadata, complete, clock_started, looping;
        int64_t origin, last_progress, next_want;
    } rx;
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
/* Lifecycle calls are serialized by the owner, never by a network callback. */
static board_link_t *instance;

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16;
}

static int64_t get_time(const uint8_t *p)
{
    uint64_t n = (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
    return n <= INT64_MAX / 4 ? (int64_t)n : -1;
}

static void put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static void put32(uint8_t *p, uint32_t n)
{
    put16(p, (uint16_t)n);
    put16(p + 2, (uint16_t)(n >> 16));
}

static void put_time(uint8_t *p, int64_t n)
{
    put32(p, (uint32_t)n);
    put32(p + 4, (uint32_t)((uint64_t)n >> 32));
}

static bool newer(uint32_t a, uint32_t b)
{
    return a != b && (uint32_t)(a - b) < UINT32_C(0x80000000);
}

static void publish_attachment(board_link_t *h)
{
    h->update.attached = h->attached;
    h->update.following = h->attached && h->following;
    h->update.valid = false;
    h->update.clock_started = false;
    h->update.looping = false;
    h->update.clock_origin_us = 0;
    h->publish_pending = true;
}

static void deliver_update(board_link_t *h)
{
    if (h->publish_pending && h->receive(h->ctx, &h->update)) {
        h->publish_pending = false;
    }
}

static void detach(board_link_t *h)
{
    if (h->attached) {
        ESP_LOGI(TAG, "detached edge=%s peer=%012" PRIx64 " session=%" PRIu32,
                 mosaico_edge_to_string(h->edge), h->peer, h->session);
    }
    h->attached = false;
    h->following = false;
    h->peer = 0;
    h->session = 0;
    h->edge = MOSAICO_EDGE_NONE;
    h->pending.active = false;
    h->tx.active = false;
    memset(&h->rx, 0, sizeof(h->rx));
    memset(&h->clock, 0, sizeof(h->clock));
    publish_attachment(h);
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
    if (h->attached && !(h->contacts & mosaico_edge_to_mask(h->edge))) {
        /* Detach immediately on physical release; do not wait for a radio ACK. */
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
        !(h->contacts & mosaico_edge_to_mask(n->local_edge)) || !n->session_id || !n->peer_id) {
        ESP_LOGW(TAG, "reject attachment local=%d remote=%d rotation=%u", n->local_edge,
                 n->peer_edge, (unsigned)n->relative_rotation);
        return;
    }
    if (h->attached) {
        /* This product supports exactly one horizontal neighbor per device. */
        return;
    }
    detach(h);
    h->attached = true;
    h->following = n->local_edge == MOSAICO_EDGE_LEFT;
    h->edge = n->local_edge;
    h->peer = n->peer_id;
    h->session = n->session_id;
    h->tx.active = h->tx.size != 0;
    ++h->tx.transfer;
    h->tx.offset = 0;
    h->tx.next_meta = 0;
    h->next_send = 0;
    h->next_read = 0;
    publish_attachment(h);
    ESP_LOGI(TAG, "attached local=%s remote=%s peer=%012" PRIx64
             " session=%" PRIu32 " following=%d; waiting for plan/clock",
             mosaico_edge_to_string(n->local_edge), mosaico_edge_to_string(n->peer_edge),
             h->peer, h->session, h->following);
}

static void publish_received(board_link_t *h)
{
    if (!h->attached || !h->following || !h->clock.valid || !h->rx.complete ||
        !h->rx.metadata || !h->rx.clock_started || h->rx.complete_revision != h->rx.revision) {
        return;
    }
    int64_t origin = h->rx.origin + h->clock.offset;
    if (origin < 0) {
        origin %= LINK_PERIOD_US;
        if (origin < 0) {
            origin += LINK_PERIOD_US;
        }
    }
    bool changed = !h->update.valid || h->update.clock_origin_us != origin ||
                   h->update.looping != h->rx.looping ||
                   memcmp(&h->update.plan, &h->received_plan, sizeof(h->received_plan)) != 0;
    if (!changed) {
        return;
    }
    h->update.attached = h->update.following = h->update.valid = true;
    h->update.clock_started = true;
    h->update.looping = h->rx.looping;
    h->update.clock_origin_us = origin;
    h->update.plan = h->received_plan;
    h->publish_pending = true;
}

static void receive_pong(board_link_t *h, const uint8_t *p, int64_t t4)
{
    int64_t t1 = get_time(p + 12), t2 = get_time(p + 20), t3 = get_time(p + 28);
    if (!h->following || !h->clock.waiting || get32(p + 8) != h->clock.pending_sequence ||
        t1 != h->clock.t1 || t1 < 0 || t2 < 0 || t3 < t2 || t4 < t1) {
        return;
    }
    h->clock.waiting = false;
    int64_t rtt = (t4 - t1) - (t3 - t2);
    if (rtt < 0 || rtt > LINK_CLOCK_MAX_RTT_US || t4 - t1 > 250000) {
        return;
    }
    int64_t offset = ((t1 - t2) + (t4 - t3)) / 2;
    if (!h->clock.samples || rtt < h->clock.best_rtt) {
        h->clock.best_rtt = rtt;
        h->clock.best_offset = offset;
    }
    h->clock.samples++;
    if (h->clock.samples >= 3) {
        bool first = !h->clock.valid;
        h->clock.valid = true;
        h->clock.offset = h->clock.best_offset;
        h->clock.next_ping = t4 + 5000000;
        if (first || t4 - h->clock.last_log >= 5000000) {
            ESP_LOGI(TAG, "clock estimate peer=%012" PRIx64 " rtt=%" PRId64
                     "us local-minus-remote=%" PRId64 "us samples=%u",
                     h->peer, h->clock.best_rtt, h->clock.offset, h->clock.samples);
            h->clock.last_log = t4;
        }
        publish_received(h);
    }
}

static void receive_meta(board_link_t *h, const uint8_t *p, int64_t now)
{
    uint32_t revision = get32(p + 8), crc = get32(p + 16);
    uint16_t size = get16(p + 12);
    int64_t origin = get_time(p + 20);
    if (!h->following || !revision || !size || size > ECHOY_LINK_PLAN_MAX_BYTES ||
        (p[14] & ~(META_CLOCK | META_LOOP)) || p[15] || origin < 0 ||
        (h->rx.metadata && revision != h->rx.revision && !newer(revision, h->rx.revision))) {
        return;
    }
    if (!h->rx.metadata || revision != h->rx.revision) {
        h->rx.offset = 0;
        h->rx.complete = false;
        h->rx.revision = revision;
        h->rx.size = size;
        h->rx.crc = crc;
        h->rx.last_progress = now;
        h->rx.next_want = now + LINK_RX_TIMEOUT_US;
    } else if (h->rx.size != size || h->rx.crc != crc) {
        return;
    }
    h->rx.metadata = true;
    h->rx.clock_started = (p[14] & META_CLOCK) != 0;
    h->rx.looping = (p[14] & META_LOOP) != 0;
    h->rx.origin = origin;
    publish_received(h);
}

static void receive_chunk(board_link_t *h, const uint8_t *p, size_t length, int64_t now)
{
    uint16_t offset = get16(p + 18);
    size_t bytes = length - 20;
    if (!h->following || !h->rx.metadata || h->rx.complete ||
        get32(p + 8) != h->rx.revision || get32(p + 12) != h->rx.crc ||
        get16(p + 16) != h->rx.size || !bytes || offset + bytes > h->rx.size) {
        return;
    }
    /* A sender restarts the full snapshot after any failed delivery. */
    if (!offset) {
        h->rx.offset = 0;
    }
    if (offset != h->rx.offset) {
        return;
    }
    memcpy(h->rx.bytes + offset, p + 20, bytes);
    h->rx.offset += (uint16_t)bytes;
    h->rx.last_progress = now;
    h->rx.next_want = now + LINK_RX_TIMEOUT_US;
    if (h->rx.offset != h->rx.size) {
        return;
    }
    if (echoy_link_crc32(h->rx.bytes, h->rx.size) != h->rx.crc ||
        !echoy_link_plan_decode(h->rx.bytes, h->rx.size, &h->received_plan)) {
        h->rx.offset = 0;
        h->rx.next_want = now + LINK_RETRY_US;
        ESP_LOGW(TAG, "rejected plan revision=%" PRIu32 " bytes=%u", h->rx.revision,
                 (unsigned)h->rx.size);
        return;
    }
    h->rx.complete = true;
    h->rx.complete_revision = h->rx.revision;
    ESP_LOGI(TAG, "received plan peer=%012" PRIx64 " revision=%" PRIu32 " bytes=%u",
             h->peer, h->rx.revision, (unsigned)h->rx.size);
    publish_received(h);
}

static void receive_message(board_link_t *h, const mosaico_network_message_t *m, int64_t now)
{
    const uint8_t *p = m->payload;
    size_t n = m->payload_len;
    if (!h->attached || m->source_id != h->peer || n < 8 || n > 64 ||
        (m->service_id & LINK_SERVICE_MASK) != LINK_SERVICE_BASE ||
        p[0] != LINK_WIRE_VERSION || p[2] || p[3] || get32(p + 4) != h->session) {
        return;
    }
    switch (p[1]) {
    case WIRE_PING:
        if (!h->following && n == 20 && get_time(p + 12) >= 0) {
            h->clock.pong_pending = true;
            h->clock.pong_sequence = get32(p + 8);
            h->clock.pong_t1 = get_time(p + 12);
            h->clock.pong_t2 = now;
        }
        break;
    case WIRE_PONG:
        if (n == 36) {
            receive_pong(h, p, now);
        }
        break;
    case WIRE_META:
        if (n == 28) {
            receive_meta(h, p, now);
        }
        break;
    case WIRE_CHUNK:
        if (n > 20) {
            receive_chunk(h, p, n, now);
        }
        break;
    case WIRE_WANT:
        if (!h->following && n == 16 && h->tx.size &&
            (!h->tx.active || now >= h->next_send)) {
            h->tx.active = true;
            ++h->tx.transfer;
            h->tx.offset = 0;
            h->tx.next_meta = 0;
        }
        break;
    default:
        break;
    }
}

static void delivery_failed(board_link_t *h, int64_t now)
{
    h->pending.active = false;
    h->next_send = now + LINK_RETRY_US;
    if (!h->following && h->tx.size) {
        h->tx.active = true;
        ++h->tx.transfer;
        h->tx.offset = 0;
        h->tx.next_meta = 0;
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
        detach(h);
        mosaico_mag_presence_tracker_reset(&h->presence);
        break;
    default:
        break;
    }
}

static void read_local(board_link_t *h, int64_t now)
{
    if (!h->attached || h->following || now < h->next_read) {
        return;
    }
    h->next_read = now + 100000;
    if (!h->read(h->ctx, &h->view)) {
        return;
    }
    size_t size = echoy_link_plan_encode(&h->view.plan, h->scratch, sizeof(h->scratch));
    if (!size || size > UINT16_MAX) {
        return;
    }
    if (size != h->tx.size || memcmp(h->scratch, h->tx.bytes, size) != 0) {
        memcpy(h->tx.bytes, h->scratch, size);
        h->tx.size = (uint16_t)size;
        h->tx.crc = echoy_link_crc32(h->tx.bytes, size);
        if (++h->tx.revision == 0) {
            ++h->tx.revision;
        }
        h->tx.offset = 0;
        h->tx.active = true;
        ++h->tx.transfer;
        h->tx.next_meta = 0;
        ESP_LOGI(TAG, "local plan revision=%" PRIu32 " bytes=%u", h->tx.revision,
                 (unsigned)h->tx.size);
    }
    bool loop = h->view.state.continuous && h->view.state.phase == ECHOY_PLAYING;
    if (h->tx.clock_started != h->view.state.clock_started || h->tx.looping != loop ||
        h->tx.origin != h->view.state.clock_origin_us) {
        h->tx.next_meta = 0;
    }
    h->tx.clock_started = h->view.state.clock_started;
    h->tx.looping = loop;
    h->tx.origin = h->view.state.clock_origin_us;
}

static void send_next(board_link_t *h, int64_t now)
{
    if (!h->attached) {
        return;
    }
    if (h->pending.active) {
        if (now - h->pending.since >= 2500000) {
            ESP_LOGW(TAG, "delivery callback timeout; retrying latest snapshot");
            delivery_failed(h, now);
        }
        return;
    }
    if (now < h->next_send) {
        return;
    }
    uint8_t p[64] = {LINK_WIRE_VERSION};
    put32(p + 4, h->session);
    size_t length = 0;
    uint16_t chunk = 0;
    if (h->clock.pong_pending) {
        p[1] = WIRE_PONG;
        put32(p + 8, h->clock.pong_sequence);
        put_time(p + 12, h->clock.pong_t1);
        put_time(p + 20, h->clock.pong_t2);
        put_time(p + 28, now);
        length = 36;
    } else if (h->following && now >= h->clock.next_ping &&
               (!h->clock.waiting || now - h->clock.t1 >= 500000)) {
        if (now - h->clock.window_start >= 5000000) {
            h->clock.window_start = now;
            h->clock.samples = 0;
        }
        p[1] = WIRE_PING;
        put32(p + 8, ++h->clock.sequence);
        put_time(p + 12, now);
        length = 20;
    } else if (!h->following && h->tx.size && now >= h->tx.next_meta) {
        p[1] = WIRE_META;
        put32(p + 8, h->tx.revision);
        put16(p + 12, h->tx.size);
        p[14] = (h->tx.clock_started ? META_CLOCK : 0) | (h->tx.looping ? META_LOOP : 0);
        put32(p + 16, h->tx.crc);
        put_time(p + 20, h->tx.origin);
        length = 28;
    } else if (!h->following && h->tx.active) {
        chunk = h->tx.size - h->tx.offset;
        if (chunk > LINK_CHUNK_BYTES) {
            chunk = LINK_CHUNK_BYTES;
        }
        p[1] = WIRE_CHUNK;
        put32(p + 8, h->tx.revision);
        put32(p + 12, h->tx.crc);
        put16(p + 16, h->tx.size);
        put16(p + 18, h->tx.offset);
        memcpy(p + 20, h->tx.bytes + h->tx.offset, chunk);
        length = 20 + chunk;
    } else if (h->following && !h->rx.complete && now >= h->rx.next_want) {
        p[1] = WIRE_WANT;
        put32(p + 8, h->rx.revision);
        put32(p + 12, h->rx.crc);
        length = 16;
    }
    if (!length) {
        return;
    }
    uint16_t service = LINK_SERVICE_BASE | (++h->pending.sequence & ~LINK_SERVICE_MASK);
    esp_err_t error = mosaico_network_send(h->network, h->peer, service, p, length);
    if (error != ESP_OK) {
        delivery_failed(h, now);
        return;
    }
    h->pending.active = true;
    h->pending.service = service;
    h->pending.kind = p[1];
    h->pending.revision = h->tx.revision;
    h->pending.transfer = h->tx.transfer;
    h->pending.bytes = chunk;
    h->pending.since = now;
    h->next_send = now + 5000;
    if (p[1] == WIRE_PONG) {
        h->clock.pong_pending = false;
    } else if (p[1] == WIRE_PING) {
        h->clock.waiting = true;
        h->clock.pending_sequence = h->clock.sequence;
        h->clock.t1 = now;
        h->clock.next_ping = now + 150000;
    } else if (p[1] == WIRE_META) {
        h->tx.next_meta = now + LINK_META_US;
    } else if (p[1] == WIRE_WANT) {
        h->rx.next_want = now + LINK_RX_TIMEOUT_US;
    }
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
        deliver_update(h);
        read_local(h, now);
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
    board_link_t *h = calloc(1, sizeof(*h));
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
