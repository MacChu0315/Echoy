#include "board_link.h"
#include "echoy_group.h"
#include "echoy_horizontal_profile.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/magnetometer.h"
#include "esp_log.h"
#include "esp_random.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mosaico_mag_classifier.h"
#include "mosaico_network.h"
#define LINK_NETWORK_ID UINT32_C(0x45434859)
#define LINK_SERVICE_MASK UINT16_C(0xf000)
#define LINK_SERVICE_BASE UINT16_C(0xe000)
typedef struct { mosaico_network_event_t event; int64_t received_us; } link_event_t;
typedef struct {
    board_link_read_fn read;
    board_link_receive_fn receive;
    void *ctx;
    mosaico_network_handle_t network;
    echoy_group_t *group;
    uint64_t self;
    QueueHandle_t events;
    atomic_bool overflow,stopping,worker_exited;
    mosaico_mag_classifier_t classifier;
    mosaico_mag_presence_tracker_t presence;
    bool sensors_ready,sensors_initialized,samples_ready;
    mosaico_edge_mask_t contacts,reported_contacts;
    int64_t next_sensor;
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    int64_t next_diagnostic;
    unsigned diagnostic_records;
#endif
    struct { uint64_t peer; uint32_t session; } seams[2];
} board_link_t;
static const char *TAG="echoy-link";
static atomic_bool owned;
static board_link_t *instance;
static esp_err_t init_horizontal_classifier(mosaico_mag_classifier_t *classifier)
{
    /* The shared read-only profile is the common Flash-backed threshold source.
     * Its LEFT/RIGHT predicates use absolute axes, independently of the Y/Z
     * reference used for vertical edges. Supply a fixed zero vertical reference
     * to disable startup baseline capture, then discard every vertical result.
     * This is horizontal-only operation, not a measured per-device calibration. */
    esp_err_t error=mosaico_mag_classifier_init(classifier,&echoy_horizontal_profile);
    if (error!=ESP_OK) return error;
    classifier->baseline_right_y=0;
    classifier->baseline_right_z=0;
    classifier->baseline_ready=true;
    classifier->calibration_state=MOSAICO_MAG_CALIBRATION_READY;
    return ESP_OK;
}
static bool owner_read(void *ctx,echoy_link_view_t *out)
{ board_link_t *h=ctx; return h->read(h->ctx,out); }
static bool owner_receive(void *ctx,const echoy_link_update_t *u)
{ board_link_t *h=ctx; return h->receive(h->ctx,u); }
static bool network_send(void *ctx,uint64_t to,uint16_t service,const uint8_t *p,size_t size)
{ board_link_t *h=ctx; return mosaico_network_send(h->network,to,service,p,size)==ESP_OK; }
static void release_seam(board_link_t *h,unsigned side,int64_t now)
{
    h->seams[side].peer=0; h->seams[side].session=0;
    echoy_group_edge(h->group,side==0,0,0,now);
}
static void release_all(board_link_t *h,int64_t now)
{ release_seam(h,0,now); release_seam(h,1,now); }
static void network_event(const mosaico_network_event_t *event,void *ctx)
{
    board_link_t *h=ctx;
    if (event->type==MOSAICO_NETWORK_EVENT_TOPOLOGY_CHANGED) return;
    link_event_t copy={.event=*event,.received_us=esp_timer_get_time()};
    if (xQueueSend(h->events,&copy,0)!=pdTRUE) atomic_store_explicit(&h->overflow,true,memory_order_release);
}
static void presence_event(const mosaico_mag_presence_event_t *event,void *ctx)
{
    board_link_t *h=ctx;
    h->contacts=event->attached_mask & (MOSAICO_EDGE_MASK_LEFT | MOSAICO_EDGE_MASK_RIGHT);
    int64_t now=esp_timer_get_time();
    if (!(h->contacts & MOSAICO_EDGE_MASK_LEFT)) release_seam(h,0,now);
    if (!(h->contacts & MOSAICO_EDGE_MASK_RIGHT)) release_seam(h,1,now);
}
static void report_contacts(board_link_t *h,int64_t now)
{
    const mosaico_edge_t edges[]={MOSAICO_EDGE_LEFT,MOSAICO_EDGE_RIGHT};
    for (unsigned i=0;i<2;++i) {
        mosaico_edge_mask_t bit=mosaico_edge_to_mask(edges[i]);
        if ((h->contacts ^ h->reported_contacts) & bit) {
            bool present=(h->contacts & bit)!=0;
            if (mosaico_network_set_contact(h->network,edges[i],present,(uint32_t)(now/1000))==ESP_OK) {
                h->reported_contacts^=bit;
                ESP_LOGI(TAG,"magnetic %s edge=%s",present ? "contact" : "release",mosaico_edge_to_string(edges[i]));
            }
        }
    }
}
static void disable_sensors(board_link_t *h,const char *reason)
{
    h->sensors_ready=false; h->contacts=0;
    release_all(h,esp_timer_get_time());
    ESP_LOGW(TAG,"link unavailable: %s; sensor restart required",reason);
}
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
static unsigned sentinel_axes(const struct bmm150_mag_data *data)
{
    /* The pinned fixed-point BMM150 API uses INT16_MIN as its overflow output.
     * Equality is evidence of the sentinel, not a diagnosis of its cause. */
    return (data->x == INT16_MIN ? 1u : 0u) |
           (data->y == INT16_MIN ? 2u : 0u) |
           (data->z == INT16_MIN ? 4u : 0u);
}
static int32_t diagnostic_average(const int16_t *values,unsigned count)
{
    int32_t sum=0;
    for (unsigned i=0;i<count;++i) sum+=values[i];
    return count ? sum/(int32_t)count : 0;
}
static void diagnostic_sample(board_link_t *h,const struct bmm150_mag_data *left,
                              const struct bmm150_mag_data *right,
                              const mosaico_mag_classification_t *result,int64_t now)
{
    if (!result->valid || !h->sensors_ready || h->diagnostic_records>=90 || now<h->next_diagnostic) return;
    h->next_diagnostic=now+1000000;
    echoy_group_info_t group={0};
    echoy_group_info(h->group,&group);
    const mosaico_mag_classifier_t *c=&h->classifier;
    unsigned n=c->sample_count;
    ESP_LOGI(TAG,"ECHOY_MAG_LIVE n=%u raw_L=%ld,%ld,%ld filtered_L=%ld,%ld,%ld "
             "raw_R=%ld,%ld,%ld filtered_R=%ld,%ld,%ld edges=%02x stable=%02x reported=%02x "
             "peers=%012" PRIx64 "/%012" PRIx64 " group=%u/%u/%u mapped=%u",
             ++h->diagnostic_records,(long)left->x,(long)left->y,(long)left->z,
             (long)diagnostic_average(c->left_x,n),(long)diagnostic_average(c->left_y,n),
             (long)diagnostic_average(c->left_z,n),(long)right->x,(long)right->y,(long)right->z,
             (long)diagnostic_average(c->right_x,n),(long)diagnostic_average(c->right_y,n),
             (long)diagnostic_average(c->right_z,n),(unsigned)result->edge_mask,
             (unsigned)h->contacts,(unsigned)h->reported_contacts,h->seams[0].peer,h->seams[1].peer,
             group.count,group.rank,group.phase,group.clock_mapped ? 1u : 0u);
}
#endif

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
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    unsigned rejections = h->classifier.calibration_rejections;
#endif
    esp_err_t classified = mosaico_mag_classifier_process(&h->classifier, &sample, &result);
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    if (h->classifier.calibration_rejections != rejections) {
        /* Link worker only; at most the three rejected startup windows.
         * These are the successful BSP read outputs for the final sample in
         * that window, not raw ADC registers or the classifier's average. */
        ESP_LOGW(TAG, "ECHOY_MAG_BOOT attempt=%u left_x=%ld left_y=%ld left_z=%ld "
                 "left_sentinel_bits=%u right_x=%ld right_y=%ld right_z=%ld right_sentinel_bits=%u",
                 (unsigned)h->classifier.calibration_rejections,
                 (long)left.x, (long)left.y, (long)left.z, sentinel_axes(&left),
                 (long)right.x, (long)right.y, (long)right.z, sentinel_axes(&right));
    }
#endif
    if (classified != ESP_OK ||
        h->classifier.calibration_state == MOSAICO_MAG_CALIBRATION_FAILED) {
        disable_sensors(h, "magnetometer classification failed");
        return;
    }
    result.edge_mask &= MOSAICO_EDGE_MASK_LEFT | MOSAICO_EDGE_MASK_RIGHT;
    if (result.valid && !h->samples_ready) {
        h->samples_ready=true;
        ESP_LOGI(TAG, "horizontal sensors ready; fixed Flash thresholds active");
    }
    if (mosaico_mag_presence_tracker_process(&h->presence, &result) != ESP_OK) {
        disable_sensors(h, "magnetic presence tracking failed");
    }
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    diagnostic_sample(h,&left,&right,&result,now);
#endif
}

static void process_event(board_link_t *h,const link_event_t *e)
{
    int64_t now=e->received_us;
    switch (e->event.type) {
    case MOSAICO_NETWORK_EVENT_NEIGHBOR_ATTACHED: {
        const mosaico_network_neighbor_event_t *n=&e->event.data.neighbor;
        bool left=n->local_edge==MOSAICO_EDGE_LEFT && n->peer_edge==MOSAICO_EDGE_RIGHT;
        bool right=n->local_edge==MOSAICO_EDGE_RIGHT && n->peer_edge==MOSAICO_EDGE_LEFT;
        if (!h->sensors_ready || (!left && !right) || n->relative_rotation ||
            !(h->contacts & mosaico_edge_to_mask(n->local_edge)) || !n->peer_id || !n->session_id) break;
        unsigned side=left ? 0 : 1;
        h->seams[side].peer=n->peer_id; h->seams[side].session=n->session_id;
        echoy_group_edge(h->group,left,n->peer_id,n->session_id,now);
        ESP_LOGI(TAG,"horizontal peer=%012" PRIx64 " edge=%s session=%" PRIu32,
                 n->peer_id,mosaico_edge_to_string(n->local_edge),n->session_id);
        break;
    }
    case MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED: {
        const mosaico_network_neighbor_event_t *n=&e->event.data.neighbor;
        int side=n->local_edge==MOSAICO_EDGE_LEFT ? 0 : n->local_edge==MOSAICO_EDGE_RIGHT ? 1 : -1;
        if (side>=0 && h->seams[side].peer==n->peer_id && h->seams[side].session==n->session_id) {
            if (!(h->contacts & mosaico_edge_to_mask(n->local_edge))) release_seam(h,(unsigned)side,now);
            else echoy_group_radio_lost(h->group,side==0,now);
        }
        /* A radio-only loss never removes the retained physical seam. */
        break;
    }
    case MOSAICO_NETWORK_EVENT_MESSAGE_RECEIVED: {
        const mosaico_network_message_t *m=&e->event.data.message;
        if ((m->service_id & LINK_SERVICE_MASK)==LINK_SERVICE_BASE && m->destination_id==h->self)
            echoy_group_receive(h->group,m->source_id,m->payload,m->payload_len,now);
        break;
    }
    case MOSAICO_NETWORK_EVENT_MESSAGE_DELIVERED:
    case MOSAICO_NETWORK_EVENT_MESSAGE_FAILED:
        echoy_group_delivery(h->group,e->event.data.delivery.destination_id,
            e->event.data.delivery.service_id,e->event.type==MOSAICO_NETWORK_EVENT_MESSAGE_DELIVERED,now);
        break;
    case MOSAICO_NETWORK_EVENT_TOPOLOGY_CONFLICT:
        h->contacts=0; release_all(h,now); mosaico_mag_presence_tracker_reset(&h->presence); break;
    default: break;
    }
}
static void worker(void *arg)
{
    board_link_t *h=arg;
    ESP_LOGI(TAG,"shared Flash thresholds: profile=%s saturation=%d left_reject_z=%d; horizontal-only, no startup baseline capture",
             echoy_horizontal_profile.name,(int)echoy_horizontal_profile.saturation_limit,
             (int)echoy_horizontal_profile.left_reject_z);
    h->sensors_initialized=true;
    h->sensors_ready=bsp_magnetometer_init_all()==ESP_OK;
    if (!h->sensors_ready) disable_sensors(h,"magnetometer initialization failed");
    while (!atomic_load_explicit(&h->stopping,memory_order_acquire)) {
        int64_t now=esp_timer_get_time();
        if (atomic_exchange_explicit(&h->overflow,false,memory_order_acq_rel)) {
            xQueueReset(h->events); h->contacts=0; release_all(h,now);
            mosaico_mag_presence_tracker_reset(&h->presence);
            ESP_LOGW(TAG,"event queue overflow; restarting physical contact handshake");
        }
        link_event_t e;
        for (unsigned i=0;i<16 && xQueueReceive(h->events,&e,0)==pdTRUE;++i) process_event(h,&e);
        sample_sensors(h,now); report_contacts(h,now);
        echoy_group_tick(h->group,esp_timer_get_time());
        vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
    }
    h->contacts=0; report_contacts(h,esp_timer_get_time());
    if (h->sensors_initialized) {
        esp_err_t r=bsp_magnetometer_deinit(BSP_MAGNETOMETER_0),l=bsp_magnetometer_deinit(BSP_MAGNETOMETER_1);
        if (r!=ESP_OK || l!=ESP_OK) ESP_LOGW(TAG,"magnetometer cleanup right=%s left=%s",esp_err_to_name(r),esp_err_to_name(l));
    }
    atomic_store_explicit(&h->worker_exited,true,memory_order_release);
    vTaskDelete(NULL);
}

esp_err_t board_link_start(board_link_read_fn read,board_link_receive_fn receive,void *ctx)
{
    if (!read || !receive) return ESP_ERR_INVALID_ARG;
    if (atomic_exchange_explicit(&owned,true,memory_order_acq_rel)) return ESP_ERR_INVALID_STATE;
#ifdef ESP_PLATFORM
    board_link_t *h=heap_caps_calloc(1,sizeof(*h),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    board_link_t *h=calloc(1,sizeof(*h));
#endif
    if (!h) { atomic_store(&owned,false); return ESP_ERR_NO_MEM; }
#ifdef ESP_PLATFORM
    h->group=heap_caps_calloc(1,echoy_group_size(),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    h->group=calloc(1,echoy_group_size());
#endif
    h->read=read; h->receive=receive; h->ctx=ctx;
    atomic_init(&h->overflow,false); atomic_init(&h->stopping,false); atomic_init(&h->worker_exited,false);
    h->events=xQueueCreate(32,sizeof(link_event_t));
    esp_err_t error=h->events && h->group ? ESP_OK : ESP_ERR_NO_MEM;
    if (error==ESP_OK) error=init_horizontal_classifier(&h->classifier);
    if (error==ESP_OK) error=mosaico_mag_presence_tracker_init(&h->presence,NULL,presence_event,h);
    if (error==ESP_OK) {
        mosaico_network_config_t c=MOSAICO_NETWORK_CONFIG_DEFAULT();
        c.network_id=LINK_NETWORK_ID; c.app_retry_interval_ms=150; c.app_retry_count=5;
        c.event_cb=network_event; c.user_ctx=h;
        error=mosaico_network_start(&c,&h->network);
    }
    if (error==ESP_OK) {
        mosaico_network_snapshot_t snapshot={0};
        error=mosaico_network_get_snapshot(h->network,&snapshot); h->self=snapshot.device_id;
        uint32_t boot=esp_random(); if (!boot) boot=1;
        echoy_group_callbacks_t cb={owner_read,owner_receive,network_send,h};
        if (error==ESP_OK && !echoy_group_init(h->group,echoy_group_size(),h->self,boot,&cb)) error=ESP_ERR_INVALID_STATE;
        if (error==ESP_OK && xTaskCreate(worker,"echoy_link",7168,h,tskIDLE_PRIORITY+1,NULL)==pdPASS) {
            instance=h; return ESP_OK;
        }
        if (error==ESP_OK) error=ESP_ERR_NO_MEM;
    }
    atomic_store(&h->worker_exited,true);
    if (h->network) {
        esp_err_t stopped=mosaico_network_stop(h->network);
        if (stopped!=ESP_OK) { instance=h; return stopped; }
    }
    if (h->events) vQueueDelete(h->events);
    free(h->group); free(h); atomic_store(&owned,false); return error;
}
esp_err_t board_link_stop(uint32_t timeout_ms)
{
    if (timeout_ms>60000) return ESP_ERR_INVALID_ARG;
    board_link_t *h=instance; if (!h) return ESP_OK;
    atomic_store_explicit(&h->stopping,true,memory_order_release);
    int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;
    while (!atomic_load_explicit(&h->worker_exited,memory_order_acquire)) {
        if (esp_timer_get_time()>=deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    esp_err_t error=mosaico_network_stop(h->network); if (error!=ESP_OK) return error;
    vQueueDelete(h->events); free(h->group); free(h); instance=NULL;
    atomic_store_explicit(&owned,false,memory_order_release); return ESP_OK;
}
