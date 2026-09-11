// SPDX-License-Identifier: Apache-2.0
#include "echoy_platform.h"
#include "bsp/magnetometer.h"
#include <string.h>
static void presence(const mosaico_mag_presence_event_t *event,void *ctx) {
    echoy_contact_adapter_t *c=ctx;
    c->physical_mask=event->attached_mask;
}
esp_err_t echoy_contact_start(echoy_contact_adapter_t *c,echoy_radio_t *r) {
    if(!c||!r||!r->started)return ESP_ERR_INVALID_ARG;
    memset(c,0,sizeof *c);c->radio=r;c->uncertain=true;
    esp_err_t err=mosaico_mag_classifier_init(&c->classifier,&MOSAICO_MAG_CALIBRATION_S31_V1);
    if(err!=ESP_OK)return err;
    err=mosaico_mag_presence_tracker_init(&c->presence,NULL,presence,c);if(err!=ESP_OK)return err;
    err=bsp_magnetometer_init_all();if(err==ESP_OK)c->initialized=true;return err;
}
esp_err_t echoy_contact_sample(echoy_contact_adapter_t *c,int64_t now) {
    if(!c||!c->initialized||now<0)return ESP_ERR_INVALID_STATE;
    struct bmm150_mag_data right={0},left={0};
    esp_err_t er=bsp_magnetometer_read(BSP_MAGNETOMETER_0,&right),el=bsp_magnetometer_read(BSP_MAGNETOMETER_1,&left);
    mosaico_mag_sample_t sample={.right={er==ESP_OK,right.x,right.y,right.z},
        .left={el==ESP_OK,left.x,left.y,left.z},.timestamp_ms=(uint32_t)(now/1000)};
    mosaico_mag_classification_t classification={0};
    esp_err_t err=mosaico_mag_classifier_process(&c->classifier,&sample,&classification);
    bool uncertain=err!=ESP_OK||er!=ESP_OK||el!=ESP_OK||!classification.valid||classification.saturated||
        c->classifier.calibration_state!=MOSAICO_MAG_CALIBRATION_READY;
    if(uncertain&&!c->uncertain)c->uncertain_since_us=now;
    c->uncertain=uncertain;
    if(uncertain){c->errors++;return err!=ESP_OK?err:ESP_ERR_INVALID_STATE;}
    err=mosaico_mag_presence_tracker_process(&c->presence,&classification);if(err!=ESP_OK)return err;
    bool previous_unresolved=c->unsupported||c->ambiguous;
    c->unsupported=(c->physical_mask&(MOSAICO_EDGE_MASK_TOP|MOSAICO_EDGE_MASK_BOTTOM))!=0;
    bool new_left=(c->physical_mask&MOSAICO_EDGE_MASK_LEFT)&&!c->left_uid;
    bool new_right=(c->physical_mask&MOSAICO_EDGE_MASK_RIGHT)&&!c->right_uid;
    c->ambiguous=new_left&&new_right;
    if((c->ambiguous||c->unsupported)&&!previous_unresolved)c->uncertain_since_us=now;
    /* Never initiate two unidentified claims at once. Even if the BSP could
     * pick one, simultaneous initial left+right contact has no unique UID proof. */
    uint8_t desired=c->unsupported||c->ambiguous?0:c->physical_mask;
    const mosaico_edge_t edges[]={MOSAICO_EDGE_LEFT,MOSAICO_EDGE_RIGHT};
    for(unsigned i=0;i<2;i++) {
        uint8_t mask=mosaico_edge_to_mask(edges[i]);
        if((desired&mask)==(c->reported_mask&mask))continue;
        err=mosaico_network_set_contact(c->radio->network,edges[i],(desired&mask)!=0,(uint32_t)(now/1000));
        if(err!=ESP_OK){c->errors++;return err;}
        c->reported_mask=(c->reported_mask&~mask)|(desired&mask);
    }
    return ESP_OK;
}
echoy_result_t echoy_contact_neighbor(echoy_contact_adapter_t *c,
    const mosaico_network_neighbor_event_t *n,bool attached) {
    if(!c||!n||!n->peer_id||n->peer_id==c->radio->uid)return ECHOY_INVALID;
    uint64_t *slot=n->local_edge==MOSAICO_EDGE_LEFT?&c->left_uid:n->local_edge==MOSAICO_EDGE_RIGHT?&c->right_uid:NULL;
    if(!slot)return ECHOY_TOPOLOGY_REJECTED;
    if(!attached){if(*slot==n->peer_id)*slot=0;return ECHOY_OK;}
    if(c->uncertain||c->ambiguous||c->unsupported)return ECHOY_NOT_READY;
    if(n->relative_rotation||
       (n->local_edge==MOSAICO_EDGE_LEFT&&n->peer_edge!=MOSAICO_EDGE_RIGHT)||
       (n->local_edge==MOSAICO_EDGE_RIGHT&&n->peer_edge!=MOSAICO_EDGE_LEFT)||
       !(c->reported_mask&mosaico_edge_to_mask(n->local_edge))||
       (*slot&&*slot!=n->peer_id)||
       (slot==&c->left_uid?c->right_uid:c->left_uid)==n->peer_id)return ECHOY_TOPOLOGY_REJECTED;
    *slot=n->peer_id;return ECHOY_OK;
}
echoy_result_t echoy_contact_report(const echoy_contact_adapter_t *c,const echoy_core_t *core,
    int local,int64_t now,echoy_input_t *out) {
    if(!c||!core||!out||local<0||local>=3||core->devices[local].uid!=c->radio->uid)return ECHOY_INVALID;
    *out=(echoy_input_t){.type=ECHOY_CONTACTS,.device=local,.at_us=now,.boot_id=core->devices[local].boot_id};
    if((c->uncertain||c->ambiguous||c->unsupported)&&now-c->uncertain_since_us>=ECHOY_HOLD_US)return ECHOY_OK;
    uint64_t uids[]={c->left_uid,c->right_uid};
    for(unsigned i=0;i<2;i++)if(uids[i]) {
        int slot=-1;for(int j=0;j<3;j++)if(core->devices[j].uid==uids[i])slot=j;
        if(slot<0)return ECHOY_TOPOLOGY_REJECTED;
        out->edges[out->edge_count++]=(echoy_edge_t){i?local:slot,i?slot:local,c->uncertain||c->ambiguous||c->unsupported};
    }
    return ECHOY_OK;
}
