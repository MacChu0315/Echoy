#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "../../../projects/echoy/main/board_link.c"

/* Test the real adapter's private wire/queue logic against the pinned public
 * BSP headers. Only the radio, OS, sensor hardware and timer are replaced. */
static int64_t fake_now;
static esp_err_t send_error, stop_error, sensor_error;
static unsigned sends, stop_calls, callback_count;
static bool have_packet, accept_update = true;
static mosaico_network_message_t packet;
static board_link_t *packet_sender;
static echoy_control_view_t local_view;
static board_link_update_t last_update;

struct fake_queue { unsigned count, size, used, at; unsigned char data[]; };
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{
    struct fake_queue *q = calloc(1, sizeof(*q) + count * size);
    assert(q);
    q->count = count;
    q->size = size;
    return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t timeout)
{
    (void)timeout;
    if (q->used == q->count) return pdFALSE;
    memcpy(q->data + ((q->at + q->used) % q->count) * q->size, item, q->size);
    q->used++;
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *out, TickType_t timeout)
{
    (void)timeout;
    if (!q->used) return pdFALSE;
    memcpy(out, q->data + q->at * q->size, q->size);
    q->at = (q->at + 1) % q->count;
    q->used--;
    return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t q) { q->used = q->at = 0; return pdTRUE; }
void vQueueDelete(QueueHandle_t q) { free(q); }
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, unsigned stack, void *ctx,
                       unsigned priority, TaskHandle_t *out)
{
    (void)fn; (void)name; (void)stack; (void)ctx; (void)priority; (void)out;
    return pdPASS;
}
void vTaskDelay(TickType_t delay) { fake_now += (int64_t)delay * 10000; }
void vTaskDelete(TaskHandle_t task) { (void)task; }
int64_t esp_timer_get_time(void) { return fake_now; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test"; }
void fake_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }

const mosaico_mag_calibration_t MOSAICO_MAG_CALIBRATION_S31_V1 = {0};
esp_err_t bsp_magnetometer_init_all(void) { return sensor_error; }
esp_err_t bsp_magnetometer_deinit(bsp_magnetometer_t id) { (void)id; return ESP_OK; }
esp_err_t bsp_magnetometer_read(bsp_magnetometer_t id, struct bmm150_mag_data *data)
{ (void)id; (void)data; return sensor_error; }
esp_err_t mosaico_mag_classifier_init(mosaico_mag_classifier_t *c, const mosaico_mag_calibration_t *p)
{ (void)c; (void)p; return ESP_OK; }
esp_err_t mosaico_mag_classifier_process(mosaico_mag_classifier_t *c,
    const mosaico_mag_sample_t *s, mosaico_mag_classification_t *o)
{ (void)c; (void)s; (void)o; return ESP_OK; }
esp_err_t mosaico_mag_presence_tracker_init(mosaico_mag_presence_tracker_t *t,
    const mosaico_mag_presence_config_t *c, mosaico_mag_presence_event_cb_t cb, void *ctx)
{ (void)c; t->event_cb = cb; t->user_ctx = ctx; return ESP_OK; }
void mosaico_mag_presence_tracker_reset(mosaico_mag_presence_tracker_t *t) { t->attached_mask = 0; }
esp_err_t mosaico_mag_presence_tracker_process(mosaico_mag_presence_tracker_t *t,
    const mosaico_mag_classification_t *c) { (void)t; (void)c; return ESP_OK; }
mosaico_edge_mask_t mosaico_edge_to_mask(mosaico_edge_t edge)
{ return edge >= MOSAICO_EDGE_TOP && edge <= MOSAICO_EDGE_LEFT ? 1u << (edge - 1) : 0; }
const char *mosaico_edge_to_string(mosaico_edge_t edge) { (void)edge; return "edge"; }
esp_err_t mosaico_network_start(const mosaico_network_config_t *c, mosaico_network_handle_t *out)
{ assert(c->network_id == LINK_NETWORK_ID && c->manage_wifi); *out = (void *)1; return ESP_OK; }
esp_err_t mosaico_network_stop(mosaico_network_handle_t h)
{ (void)h; stop_calls++; return stop_error; }
esp_err_t mosaico_network_set_contact(mosaico_network_handle_t h, mosaico_edge_t e,
                                     bool present, uint32_t ms)
{ (void)h; (void)e; (void)present; (void)ms; return ESP_OK; }
esp_err_t mosaico_network_send(mosaico_network_handle_t handle, uint64_t destination,
                               uint16_t service, const void *p, size_t length)
{
    sends++;
    if (send_error != ESP_OK) return send_error;
    assert(!have_packet && length <= sizeof(packet.payload));
    board_link_t *h = (void *)handle;
    packet_sender = h;
    packet.source_id = h->following ? 2 : 1;
    packet.destination_id = destination;
    packet.service_id = service;
    packet.payload_len = (uint8_t)length;
    memcpy(packet.payload, p, length);
    have_packet = true;
    return ESP_OK;
}

static bool read_view(void *ctx, echoy_control_view_t *out)
{ (void)ctx; *out = local_view; return true; }
static bool receive_update(void *ctx, const board_link_update_t *update)
{ (void)ctx; callback_count++; if (accept_update) last_update = *update; return accept_update; }

static board_link_t *node(bool following)
{
    board_link_t *h = calloc(1, sizeof(*h));
    assert(h);
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
        .event = {.type = success ? MOSAICO_NETWORK_EVENT_MESSAGE_DELIVERED :
                                   MOSAICO_NETWORK_EVENT_MESSAGE_FAILED,
                  .data.delivery = {.service_id = service, .destination_id = h->peer,
                                    .error = success ? ESP_OK : ESP_FAIL}},
    };
    process_event(h, &e);
}

static void step(board_link_t *sender, board_link_t *recipient, int64_t physical)
{
    fake_now = physical + (sender->following ? 200000 : 0);
    send_next(sender, fake_now);
    if (!have_packet) return;
    assert(packet_sender == sender);
    receive_message(recipient, &packet, physical + 1000 + (recipient->following ? 200000 : 0));
    delivered(sender, packet.service_id, true, fake_now + 2000);
    have_packet = false;
}

static void fixture(void)
{
    memset(&local_view, 0, sizeof(local_view));
    assert(echoy_compose_mask(0x1111, ECHOY_HARMONY, 2, 0.7, &local_view.plan));
    local_view.state.clock_started = true;
    local_view.state.clock_origin_us = 700000;
    local_view.state.phase = ECHOY_PLAYING;
    local_view.state.continuous = true;
}

static void test_complete_roundtrip_and_metadata(void)
{
    fixture();
    board_link_t *a = node(false), *b = node(true);
    read_local(a, 1000000);
    assert(a->tx.size > LINK_CHUNK_BYTES);
    for (int64_t now = 1000000; now < 6000000; now += 10000) {
        step(b, a, now);
        step(a, b, now + 2000);
    }
    assert(b->clock.valid && b->rx.complete && b->update.valid);
    assert(b->clock.best_rtt <= 3000);
    assert(llabs(b->clock.offset - 200000) <= 1000);
    assert(llabs(b->update.clock_origin_us - 900000) <= 1000);
    assert(b->update.looping && b->update.plan.count == local_view.plan.count);
    uint8_t original[ECHOY_LINK_PLAN_MAX_BYTES], received[ECHOY_LINK_PLAN_MAX_BYTES];
    size_t n = echoy_link_plan_encode(&local_view.plan, original, sizeof(original));
    assert(n == echoy_link_plan_encode(&b->update.plan, received, sizeof(received)));
    assert(!memcmp(original, received, n));
    uint32_t revision = a->tx.revision;
    local_view.state.phase = ECHOY_IDLE;
    local_view.state.continuous = false;
    read_local(a, 6000000);
    assert(a->tx.revision == revision);
    step(a, b, 6010000);
    assert(!b->update.looping && b->update.valid);
    /* Disconnection retains the owned phrase; valid=false asks the control
     * owner to stop following without replacing it. */
    detach(b);
    assert(!b->update.attached && !b->update.valid);
    assert(b->update.plan.count == local_view.plan.count);
    free(a); free(b);
}

static void test_stop_and_wait_and_retry(void)
{
    fixture();
    board_link_t *a = node(false);
    read_local(a, 1000000);
    send_next(a, 1000000);
    assert(have_packet && packet.payload[1] == WIRE_META);
    unsigned before = sends;
    send_next(a, 1100000);
    assert(sends == before);
    uint16_t previous = packet.service_id;
    have_packet = false;
    delivered(a, previous, true, 1005000);
    send_next(a, 1010000);
    assert(packet.payload[1] == WIRE_CHUNK && a->tx.offset == 0);
    uint16_t current = packet.service_id;
    have_packet = false;
    delivered(a, previous, true, 1015000);
    assert(a->pending.active && a->tx.offset == 0);
    delivered(a, current, true, 1016000);
    assert(a->tx.offset == LINK_CHUNK_BYTES);
    send_next(a, 1020000);
    current = packet.service_id;
    have_packet = false;
    delivered(a, current, false, 1025000);
    assert(a->tx.offset == 0 && a->tx.active && !a->pending.active);
    before = sends;
    send_next(a, 1100000);
    assert(sends == before);
    send_next(a, 1800000);
    assert(have_packet && packet.payload[1] == WIRE_META);
    have_packet = false;
    delivered(a, packet.service_id, true, 1805000);
    send_next(a, 1810000);
    assert(packet.payload[1] == WIRE_CHUNK);
    have_packet = false;
    /* A full-transfer request cannot let an ACK for the old transfer skip its
     * first bytes, even though its plan revision is unchanged. */
    uint32_t generation = a->tx.transfer;
    mosaico_network_message_t want = {.source_id = a->peer, .service_id = LINK_SERVICE_BASE,
                                     .payload_len = 16};
    want.payload[0] = LINK_WIRE_VERSION;
    want.payload[1] = WIRE_WANT;
    put32(want.payload + 4, a->session);
    receive_message(a, &want, 1820000);
    assert(a->tx.transfer != generation && a->tx.offset == 0);
    delivered(a, packet.service_id, true, 1821000);
    assert(a->tx.offset == 0);
    free(a);
}

static void test_reject_partial_mixed_and_bad_clock(void)
{
    fixture();
    board_link_t *a = node(false), *b = node(true);
    read_local(a, 1000000);
    send_next(a, 1000000);
    mosaico_network_message_t meta = packet;
    receive_message(b, &meta, 1201000);
    have_packet = false;
    delivered(a, meta.service_id, true, 1002000);
    send_next(a, 1010000);
    mosaico_network_message_t chunk = packet;
    have_packet = false;
    uint32_t revision = b->rx.revision;
    put32(chunk.payload + 8, revision + 1);
    receive_message(b, &chunk, 1210000);
    assert(b->rx.offset == 0 && !b->update.valid);
    put32(chunk.payload + 8, revision);
    put32(chunk.payload + 4, b->session + 1);
    receive_message(b, &chunk, 1211000);
    assert(b->rx.offset == 0);
    put32(chunk.payload + 4, b->session);
    receive_message(b, &chunk, 1212000);
    assert(b->rx.offset == LINK_CHUNK_BYTES && !b->update.valid);
    put32(meta.payload + 8, revision + 1);
    receive_message(b, &meta, 1213000);
    assert(b->rx.offset == 0 && b->rx.revision == revision + 1);
    receive_message(b, &chunk, 1214000);
    assert(b->rx.offset == 0);
    put32(meta.payload + 8, revision);
    receive_message(b, &meta, 1215000);
    assert(b->rx.revision == revision + 1);
    /* A queued/retransmitted ping with high RTT cannot establish sync. */
    b->clock.waiting = true;
    b->clock.pending_sequence = 10;
    b->clock.t1 = 1000000;
    uint8_t pong[36] = {0};
    put32(pong + 8, 10); put_time(pong + 12, 1000000);
    put_time(pong + 20, 810000); put_time(pong + 28, 820000);
    receive_pong(b, pong, 1300000);
    assert(!b->clock.valid && b->clock.samples == 0);
    /* A verified complete plan still cannot publish before clock handshake. */
    b->rx.complete = b->rx.clock_started = true;
    b->rx.complete_revision = b->rx.revision;
    b->received_plan = local_view.plan;
    publish_received(b);
    assert(!b->update.valid);
    b->clock.valid = true;
    b->clock.offset = -2000000;
    b->rx.origin = 100000;
    publish_received(b);
    assert(b->update.valid && b->update.clock_origin_us == 600000);
    free(a); free(b);
}

static void test_contact_queue_and_lifecycle(void)
{
    board_link_t *h = node(false);
    detach(h);
    h->contacts = 0;
    mosaico_network_neighbor_event_t neighbor = {
        .peer_id = 2, .session_id = 123, .local_edge = MOSAICO_EDGE_RIGHT,
        .peer_edge = MOSAICO_EDGE_LEFT,
    };
    attach(h, &neighbor);
    assert(!h->attached); /* Radio proximity alone is not attachment. */
    h->contacts = MOSAICO_EDGE_MASK_RIGHT;
    neighbor.relative_rotation = 180;
    attach(h, &neighbor);
    assert(!h->attached);
    neighbor.relative_rotation = 0;
    attach(h, &neighbor);
    assert(h->attached);
    sensor_error = ESP_FAIL;
    sample_sensors(h, 1000000);
    assert(!h->attached && !h->sensors_ready && !h->contacts);
    sensor_error = ESP_OK;
    h->events = xQueueCreate(1, sizeof(link_event_t));
    mosaico_network_event_t e = {.type = MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED};
    network_event(&e, h);
    network_event(&e, h);
    assert(atomic_load(&h->overflow));
    vQueueDelete(h->events);
    free(h);
    assert(board_link_start(NULL, receive_update, NULL) == ESP_ERR_INVALID_ARG);
    assert(board_link_start(read_view, receive_update, NULL) == ESP_OK);
    h = instance;
    assert(h && h->publish_pending && !h->update.attached);
    assert(board_link_start(read_view, receive_update, NULL) == ESP_ERR_INVALID_STATE);
    unsigned stopped = stop_calls;
    assert(board_link_stop(0) == ESP_ERR_TIMEOUT && instance == h);
    assert(stop_calls == stopped);
    atomic_store(&h->worker_exited, true);
    stop_error = ESP_FAIL;
    assert(board_link_stop(100) == ESP_FAIL && instance == h);
    stop_error = ESP_OK;
    assert(board_link_stop(100) == ESP_OK && !instance && !atomic_load(&owned));
    assert(board_link_stop(0) == ESP_OK);
}

static void test_crc_empty_and_callback_retry(void)
{
    fixture();
    board_link_t *a = node(false), *b = node(true);
    read_local(a, 1000000);
    send_next(a, 1000000);
    receive_message(b, &packet, 1201000);
    delivered(a, packet.service_id, true, 1002000);
    have_packet = false;
    for (int64_t now = 1010000; a->tx.active && now < 4000000; now += 10000) {
        send_next(a, now);
        if (!have_packet) continue;
        if (packet.payload[1] == WIRE_CHUNK && get16(packet.payload + 18) == 0) {
            packet.payload[20] ^= 0x80;
        }
        receive_message(b, &packet, now + 201000);
        delivered(a, packet.service_id, true, now + 2000);
        have_packet = false;
    }
    assert(!a->tx.active && !b->rx.complete && !b->update.valid && b->rx.offset == 0);
    /* An explicit empty source is a complete, valid plan after handshake. */
    memset(&local_view.plan, 0, sizeof(local_view.plan));
    local_view.plan.phrase = 1;
    local_view.plan.duration = 2.5;
    read_local(a, 4000000);
    for (int64_t now = 4000000; now < 8000000; now += 10000) {
        step(b, a, now);
        step(a, b, now + 2000);
    }
    assert(b->rx.complete && b->update.valid && b->clock.valid);
    assert(!b->update.plan.count && !b->update.plan.source_count);
    assert(b->publish_pending);
    accept_update = false;
    unsigned before = callback_count;
    deliver_update(b);
    deliver_update(b);
    assert(b->publish_pending && callback_count == before + 2);
    accept_update = true;
    deliver_update(b);
    assert(!b->publish_pending && last_update.valid && !last_update.plan.count);
    deliver_update(b);
    assert(callback_count == before + 3);
    free(a); free(b);
}

int main(void)
{
    test_complete_roundtrip_and_metadata();
    test_stop_and_wait_and_retry();
    test_reject_partial_mixed_and_bad_clock();
    test_contact_queue_and_lifecycle();
    test_crc_empty_and_callback_retry();
    puts("board_link: plan/clock roundtrip, pacing/retry, CRC/stale/partial rejection, empty plans, callback retry, contact gating and stop pass");
    return 0;
}
