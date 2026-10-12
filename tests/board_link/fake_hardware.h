#pragma once
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
static unsigned sends, stop_calls;
static bool have_packet;
static mosaico_network_message_t packet;
static board_link_t *packet_sender;


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
