// SPDX-License-Identifier: Apache-2.0
#include "echoy_platform.h"
#include "esp_timer.h"
#include <string.h>
static echoy_radio_t *owner;
static void enqueue(echoy_radio_t *r,const echoy_radio_event_t *e) {
    if(xQueueSend(r->queue,e,0)!=pdTRUE)__atomic_fetch_add(&r->dropped,1,__ATOMIC_RELAXED);
}
static void raw(const mosaico_peer_message_t *message,int8_t rssi,void *ctx) {
    (void)rssi;echoy_radio_t *r=ctx;
    if(!message||message->type!=MOSAICO_PEER_MSG_GAME_EVENT||message->payload_len>100)return;
    echoy_radio_event_t e={.kind=ECHOY_RADIO_PACKET,.received_us=esp_timer_get_time()};
    e.data.packet=*message;enqueue(r,&e);
}
static void network_event(const mosaico_network_event_t *event,void *ctx) {
    echoy_radio_t *r=ctx;echoy_radio_event_t e={.received_us=esp_timer_get_time()};
    if(event->type==MOSAICO_NETWORK_EVENT_NEIGHBOR_ATTACHED||event->type==MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED) {
        e.kind=ECHOY_RADIO_NEIGHBOR;e.data.neighbor.attached=event->type==MOSAICO_NETWORK_EVENT_NEIGHBOR_ATTACHED;
        e.data.neighbor.value=event->data.neighbor;
    } else if(event->type==MOSAICO_NETWORK_EVENT_TOPOLOGY_CONFLICT)e.kind=ECHOY_RADIO_CONFLICT;
    else return;
    enqueue(r,&e);
}
esp_err_t echoy_radio_start(echoy_radio_t *r) {
    if(!r)return ESP_ERR_INVALID_ARG;
    if(owner)return owner==r&&r->started?ESP_OK:ESP_ERR_INVALID_STATE;
    memset(r,0,sizeof *r);r->queue=xQueueCreate(ECHOY_RADIO_QUEUE_DEPTH,sizeof(echoy_radio_event_t));
    if(!r->queue)return ESP_ERR_NO_MEM;
    mosaico_network_config_t config=MOSAICO_NETWORK_CONFIG_DEFAULT();
    config.event_cb=network_event;config.raw_message_cb=raw;config.user_ctx=r;
    /* mosaico_network is the sole owner of Wi-Fi / ESP-NOW initialization. */
    esp_err_t err=mosaico_network_start(&config,&r->network);
    if(err!=ESP_OK){vQueueDelete(r->queue);r->queue=NULL;return err;}
    r->uid=mosaico_peer_link_get_device_id();r->boot=mosaico_peer_link_get_boot_id();r->started=true;owner=r;return ESP_OK;
}
bool echoy_radio_poll(echoy_radio_t *r,echoy_radio_event_t *event) {
    if(!r||!r->started||!event)return false;
    while(xQueueReceive(r->queue,event,0)==pdTRUE) {
        if(event->kind==ECHOY_RADIO_PACKET) {
            const mosaico_peer_message_t *m=&event->data.packet;
            if((m->target_id&&m->target_id!=r->uid)||m->source_id==r->uid)continue;
        }
        return true;
    }return false;
}
bool echoy_radio_send(void *ctx,uint64_t target,const uint8_t *data,size_t size) {
    echoy_radio_t *r=ctx;if(!r||!r->started||!data||size<40||size>100)return false;
    return mosaico_peer_link_send_broadcast_to(MOSAICO_PEER_MSG_GAME_EVENT,target,0,
        MOSAICO_EDGE_NONE,MOSAICO_EDGE_NONE,0,data,size)==ESP_OK;
}
