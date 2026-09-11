// SPDX-License-Identifier: Apache-2.0
#include "echoy_platform.h"
#include "bsp/magnetometer.h"
#include <assert.h>
static bool invalid;static struct bmm150_mag_data readings[2];static unsigned claims;
esp_err_t bsp_magnetometer_init_all(void){return ESP_OK;}
esp_err_t bsp_magnetometer_read(bsp_magnetometer_t id,struct bmm150_mag_data *out) {
    *out=readings[id];return invalid?ESP_ERR_INVALID_STATE:ESP_OK;
}
esp_err_t mosaico_network_set_contact(mosaico_network_handle_t h,mosaico_edge_t edge,bool present,uint32_t at) {
    (void)h;(void)edge;(void)at;if(present)claims++;return ESP_OK;
}
const char *mosaico_edge_to_string(mosaico_edge_t edge){(void)edge;return "edge";}
int main(void) {
    echoy_radio_t radio={.uid=11,.boot=1,.started=true};echoy_contact_adapter_t c;
    assert(echoy_contact_start(&c,&radio)==ESP_OK);
    readings[0].y=-41;
    for(int i=0;i<20;i++)(void)echoy_contact_sample(&c,20000*i);
    assert(!c.uncertain&&c.classifier.calibration_state==MOSAICO_MAG_CALIBRATION_READY);
    readings[1].x=-32768;
    for(int i=20;i<40;i++)(void)echoy_contact_sample(&c,20000*i);
    assert(c.uncertain&&claims==0); /* Saturated classifier hints cannot become claims. */
    invalid=true;assert(echoy_contact_sample(&c,900000)!=ESP_OK);assert(c.uncertain);
    c.uncertain=false;c.reported_mask=MOSAICO_EDGE_MASK_RIGHT;
    mosaico_network_neighbor_event_t n={.peer_id=22,.local_edge=MOSAICO_EDGE_RIGHT,.peer_edge=MOSAICO_EDGE_LEFT};
    assert(echoy_contact_neighbor(&c,&n,true)==ECHOY_OK);assert(c.right_uid==22);
    n.peer_edge=MOSAICO_EDGE_TOP;assert(echoy_contact_neighbor(&c,&n,true)==ECHOY_TOPOLOGY_REJECTED);
    n.peer_edge=MOSAICO_EDGE_LEFT;n.relative_rotation=180;
    assert(echoy_contact_neighbor(&c,&n,true)==ECHOY_TOPOLOGY_REJECTED);
    n.relative_rotation=0;c.ambiguous=true;assert(echoy_contact_neighbor(&c,&n,true)==ECHOY_NOT_READY);
    c.ambiguous=false;c.uncertain=true;c.uncertain_since_us=1000000;
    echoy_core_t core;uint64_t uids[]={11,22,33};echoy_core_init(&core,uids);echoy_input_t e;
    assert(echoy_contact_report(&c,&core,0,1000001,&e)==ECHOY_OK);assert(e.edge_count==1&&e.edges[0].uncertain);
    assert(echoy_contact_report(&c,&core,0,2800000,&e)==ECHOY_OK);assert(e.edge_count==0);
    assert(echoy_contact_neighbor(&c,&n,false)==ECHOY_OK);assert(!c.right_uid);
    return 0;
}
