// SPDX-License-Identifier: Apache-2.0
#include "echoy_platform.h"
#include <assert.h>
#include <string.h>
static echoy_radio_event_t items[32];static unsigned count,starts,sends;
static mosaico_network_config_t config;static int64_t timestamp=100;
QueueHandle_t xQueueCreate(unsigned n,size_t size){assert(n==32&&size==sizeof items[0]);return items;}
int xQueueSend(QueueHandle_t q,const void *item,unsigned wait){assert(q==items&&!wait);if(count==32)return 0;memcpy(&items[count++],item,sizeof items[0]);return 1;}
int xQueueReceive(QueueHandle_t q,void *item,unsigned wait){assert(q==items&&!wait);if(!count)return 0;memcpy(item,items,sizeof items[0]);memmove(items,items+1,(--count)*sizeof items[0]);return 1;}
void vQueueDelete(QueueHandle_t q){assert(q==items);}
int64_t esp_timer_get_time(void){return timestamp;}
esp_err_t mosaico_network_start(const mosaico_network_config_t *c,mosaico_network_handle_t *out){starts++;config=*c;*out=(void *)items;return ESP_OK;}
uint64_t mosaico_peer_link_get_device_id(void){return 11;}
uint32_t mosaico_peer_link_get_boot_id(void){return 123;}
esp_err_t mosaico_peer_link_send_broadcast_to(mosaico_peer_message_type_t type,uint64_t target,uint32_t session,
    mosaico_edge_t local,mosaico_edge_t peer,uint16_t rotation,const void *data,size_t size) {
    assert(type==MOSAICO_PEER_MSG_GAME_EVENT&&target==22&&!session&&local==MOSAICO_EDGE_NONE&&peer==MOSAICO_EDGE_NONE&&!rotation);
    assert(data&&size==48);sends++;return ESP_OK;
}
int main(void) {
    echoy_radio_t r,other;assert(echoy_radio_start(&r)==ESP_OK);assert(r.uid==11&&r.boot==123);
    assert(echoy_radio_start(&r)==ESP_OK);assert(starts==1);assert(echoy_radio_start(&other)==ESP_ERR_INVALID_STATE);
    mosaico_peer_message_t msg={.type=MOSAICO_PEER_MSG_GAME_EVENT,.source_id=22,.source_boot_id=9,.target_id=11,.payload_len=48};msg.payload[0]=0x43;
    config.raw_message_cb(&msg,-20,config.user_ctx);msg.payload[0]=0;timestamp=200;
    echoy_radio_event_t e;assert(echoy_radio_poll(&r,&e));assert(e.received_us==100&&e.data.packet.payload[0]==0x43);
    assert(!echoy_radio_poll(&r,&e));
    for(unsigned i=0;i<33;i++)config.raw_message_cb(&msg,0,config.user_ctx);
    assert(r.dropped==1&&count==32);while(echoy_radio_poll(&r,&e)){};
    msg.target_id=33;config.raw_message_cb(&msg,0,config.user_ctx);assert(!echoy_radio_poll(&r,&e));
    assert(echoy_radio_send(&r,22,msg.payload,48));assert(sends==1); /* No physical-neighbor gate. */
    return 0;
}
