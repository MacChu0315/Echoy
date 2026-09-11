// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_PLATFORM_H
#define ECHOY_PLATFORM_H
#include "echoy_protocol.h"
#include "mosaico_network.h"
#include "mosaico_mag_classifier.h"
#include "freertos/queue.h"
#define ECHOY_RADIO_QUEUE_DEPTH 32
/* BSP callbacks only enqueue copies. The control task polls and owns all state. */
typedef enum { ECHOY_RADIO_PACKET, ECHOY_RADIO_NEIGHBOR, ECHOY_RADIO_CONFLICT } echoy_radio_event_kind_t;
typedef struct {
    echoy_radio_event_kind_t kind; int64_t received_us;
    union { mosaico_peer_message_t packet; struct {bool attached;mosaico_network_neighbor_event_t value;} neighbor; } data;
} echoy_radio_event_t;
typedef struct {
    mosaico_network_handle_t network; QueueHandle_t queue; bool started;
    uint64_t uid; uint32_t boot; unsigned dropped;
} echoy_radio_t;
esp_err_t echoy_radio_start(echoy_radio_t *radio);
bool echoy_radio_poll(echoy_radio_t *radio,echoy_radio_event_t *event);
bool echoy_radio_send(void *ctx,uint64_t target,const uint8_t *data,size_t size);
/* Call on the owner every 20 ms. No wireless identity is guessed from field strength. */
typedef struct {
    mosaico_mag_classifier_t classifier; mosaico_mag_presence_tracker_t presence;
    echoy_radio_t *radio; bool initialized,uncertain,ambiguous,unsupported;
    uint8_t physical_mask,reported_mask; uint64_t left_uid,right_uid;
    int64_t uncertain_since_us; unsigned errors;
} echoy_contact_adapter_t;
esp_err_t echoy_contact_start(echoy_contact_adapter_t *contact,echoy_radio_t *radio);
esp_err_t echoy_contact_sample(echoy_contact_adapter_t *contact,int64_t now_us);
/* Resolves an actual BSP neighbor; rejects up/down, rotation and ambiguous edges. */
echoy_result_t echoy_contact_neighbor(echoy_contact_adapter_t *contact,
    const mosaico_network_neighbor_event_t *neighbor,bool attached);
/* Emits the local portion of a full left->right report, resolving against real UIDs.
 * The control owner combines it with other confirmed members' directed edges. */
echoy_result_t echoy_contact_report(const echoy_contact_adapter_t *contact,
    const echoy_core_t *core,int local,int64_t now_us,echoy_input_t *out);
#endif
