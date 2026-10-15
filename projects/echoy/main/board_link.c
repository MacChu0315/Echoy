#include "board_link.h"
#include "echoy_group.h"
#include "echoy_radio.h"
#include "echoy_horizontal_profile.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/magnetometer.h"
#include "esp_log.h"
#include "esp_now.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mosaico_mag_classifier.h"
#include "mosaico_peer_link.h"
#define LINK_NETWORK_ID UINT32_C(0x45434859)
typedef struct { mosaico_peer_message_t message; int64_t received_us; } link_event_t;
typedef struct {
    board_link_read_fn read;
    board_link_receive_fn receive;
    void *ctx;
    echoy_radio_t *radio;
    bool radio_started,send_callback_registered;
    int64_t next_radio_stats;
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
/* The pinned peer-link component owns RX but does not register a TX callback.
 * This singleton callback touches only static atomics, never audio or a freed
 * board context. A successful submission is not application delivery. */
static atomic_bool radio_inflight;
static atomic_uint radio_completed,radio_mac_failed;
static void radio_sent(const esp_now_send_info_t *info,esp_now_send_status_t status)
{
    (void)info;
    atomic_fetch_add_explicit(&radio_completed,1,memory_order_relaxed);
    if(status!=ESP_NOW_SEND_SUCCESS)atomic_fetch_add_explicit(&radio_mac_failed,1,memory_order_relaxed);
    atomic_store_explicit(&radio_inflight,false,memory_order_release);
}
static bool radio_available(void *ctx)
{ (void)ctx;return !atomic_load_explicit(&radio_inflight,memory_order_acquire); }
static esp_err_t stop_radio(board_link_t *h)
{
    if(h->send_callback_registered) {
        esp_err_t error=esp_now_unregister_send_cb();
        if(error!=ESP_OK)return error;
        h->send_callback_registered=false;
    }
    return mosaico_peer_link_deinit();
}
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
{ board_link_t *h=ctx; return echoy_radio_send(h->radio,to,service,p,size,esp_timer_get_time()); }
static bool radio_send(void *ctx,uint64_t to,const uint8_t *p,size_t size)
{
    (void)ctx;
    bool expected=false;
    if(!atomic_compare_exchange_strong_explicit(&radio_inflight,&expected,true,
        memory_order_acq_rel,memory_order_acquire))return false;
    esp_err_t error=mosaico_peer_link_send_broadcast_to(MOSAICO_PEER_MSG_GAME_EVENT,to,0,
        MOSAICO_EDGE_NONE,MOSAICO_EDGE_NONE,0,p,size);
    if(error!=ESP_OK)atomic_store_explicit(&radio_inflight,false,memory_order_release);
    return error==ESP_OK;
}
static void radio_edge(void *ctx,bool left,uint64_t peer,uint32_t session,int64_t now)
{
    board_link_t *h=ctx;unsigned side=left?0:1;
    mosaico_edge_mask_t bit=left?MOSAICO_EDGE_MASK_LEFT:MOSAICO_EDGE_MASK_RIGHT;
    if(peer && (!h->sensors_ready || !h->samples_ready || !(h->contacts&bit)))return;
    h->seams[side].peer=peer;h->seams[side].session=session;
    echoy_group_edge(h->group,left,peer,session,now);
    ESP_LOGI(TAG,"horizontal peer=%012" PRIx64 " edge=%s session=%" PRIu32,
        peer,left?"LEFT":"RIGHT",session);
}
static void radio_lost(void *ctx,bool left,int64_t now)
{ board_link_t *h=ctx;echoy_group_radio_lost(h->group,left,now); }
static void radio_receive(void *ctx,uint64_t from,const uint8_t *p,size_t size,int64_t now)
{ board_link_t *h=ctx;echoy_group_receive(h->group,from,p,size,now); }
static void radio_stamp(void *ctx,uint8_t *p,size_t size,int64_t now)
{ board_link_t *h=ctx;echoy_group_stamp_send(h->group,p,size,now); }
static void radio_delivery(void *ctx,uint64_t to,uint16_t service,bool success,int64_t now)
{ board_link_t *h=ctx;echoy_group_delivery(h->group,to,service,success,now); }
static void release_seam(board_link_t *h,unsigned side,int64_t now)
{
    h->seams[side].peer=0; h->seams[side].session=0;
    echoy_group_edge(h->group,side==0,0,0,now);
}
static void release_all(board_link_t *h,int64_t now)
{ release_seam(h,0,now); release_seam(h,1,now); }
static void radio_event(const uint8_t mac[6],int8_t rssi,const mosaico_peer_message_t *message,void *ctx)
{
    (void)mac;(void)rssi;board_link_t *h=ctx;
    if(message->target_id && message->target_id!=h->self)return;
    link_event_t copy={.message=*message,.received_us=esp_timer_get_time()};
    if(xQueueSend(h->events,&copy,0)!=pdTRUE)atomic_store_explicit(&h->overflow,true,memory_order_release);
}
static void report_contacts(board_link_t *h,int64_t now);
static void presence_event(const mosaico_mag_presence_event_t *event,void *ctx)
{
    board_link_t *h=ctx;
    h->contacts=event->attached_mask & (MOSAICO_EDGE_MASK_LEFT | MOSAICO_EDGE_MASK_RIGHT);
    int64_t now=esp_timer_get_time();
    if (!(h->contacts & MOSAICO_EDGE_MASK_LEFT)) release_seam(h,0,now);
    if (!(h->contacts & MOSAICO_EDGE_MASK_RIGHT)) release_seam(h,1,now);
    report_contacts(h,now);
}
static void report_contacts(board_link_t *h,int64_t now)
{
    uint8_t mask=((h->contacts&MOSAICO_EDGE_MASK_LEFT)?ECHOY_CONTACT_LEFT:0) |
        ((h->contacts&MOSAICO_EDGE_MASK_RIGHT)?ECHOY_CONTACT_RIGHT:0);
    echoy_radio_contacts(h->radio,h->sensors_ready && h->samples_ready,mask,now);
    const mosaico_edge_t edges[]={MOSAICO_EDGE_LEFT,MOSAICO_EDGE_RIGHT};
    for(unsigned i=0;i<2;i++) {
        mosaico_edge_mask_t bit=mosaico_edge_to_mask(edges[i]);
        if((h->contacts^h->reported_contacts)&bit)
            ESP_LOGI(TAG,"magnetic %s edge=%s",(h->contacts&bit)?"contact":"release",mosaico_edge_to_string(edges[i]));
    }
    h->reported_contacts=h->contacts;
}

static void disable_sensors(board_link_t *h,const char *reason)
{
    h->sensors_ready=false; h->contacts=0;
    release_all(h,esp_timer_get_time());
    report_contacts(h,esp_timer_get_time());
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
    /* Iris retains bounded lines: keep group details separate from raw axes. */
    ESP_LOGI(TAG,"ECHOY_GROUP_LIVE n=%u identity=%08" PRIx32 " attempt=%" PRIu32
             " operations=%u members=%012" PRIx64 "/%012" PRIx64 "/%012" PRIx64
             " offset=%" PRId64 " deadline=%" PRId64,
             h->diagnostic_records,group.identity,group.attempt,group.operations,
             group.members[0],group.members[1],group.members[2],
             group.root_offset_us,group.deadline_us);
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
    const mosaico_peer_message_t *m=&e->message;
    if(m->type==MOSAICO_PEER_MSG_GAME_EVENT)
        echoy_radio_receive(h->radio,m->source_id,m->source_boot_id,m->payload,m->payload_len,e->received_us);
    else
        echoy_radio_observe_unknown(h->radio,m->source_id,m->source_boot_id,e->received_us);
}
static void radio_stats(board_link_t *h,int64_t now)
{
    if(now<h->next_radio_stats)return;
    h->next_radio_stats=now+30000000;
    echoy_radio_info_t info={0};echoy_radio_info(h->radio,&info);
    ESP_LOGI(TAG,"ECHOY_RADIO tx=%" PRIu32 " fail=%" PRIu32 " rx=%" PRIu32
        " reject=%" PRIu32 " retry=%" PRIu32 " drop=%" PRIu32 " queued=%u peak=%u known=%u admitted=%u fresh=%u",
        info.transmitted,info.send_failures,info.received,info.rejected,info.retries,info.queue_drops,
        info.pending,info.queue_high_water,info.known,info.admitted?1u:0u,info.scene_fresh?1u:0u);
    ESP_LOGI(TAG,"ECHOY_RADIO_TX completed=%u failed=%u busy=%u left=%012" PRIx64 " right=%012" PRIx64 " graph=%016" PRIx64,
        atomic_load_explicit(&radio_completed,memory_order_relaxed),
        atomic_load_explicit(&radio_mac_failed,memory_order_relaxed),
        atomic_load_explicit(&radio_inflight,memory_order_acquire)?1u:0u,info.peers[0],info.peers[1],info.graph);
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
            xQueueReset(h->events);echoy_radio_recover(h->radio,now);
            ESP_LOGW(TAG,"radio event queue overflow; retaining physical contacts and refreshing scene");
        }
        link_event_t e;
        for (unsigned i=0;i<16 && xQueueReceive(h->events,&e,0)==pdTRUE;++i) process_event(h,&e);
        sample_sensors(h,now); report_contacts(h,now);
        echoy_group_tick(h->group,esp_timer_get_time());
        echoy_radio_tick(h->radio,esp_timer_get_time());radio_stats(h,now);
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
    h->radio=heap_caps_calloc(1,echoy_radio_size(),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    h->group=calloc(1,echoy_group_size());
    h->radio=calloc(1,echoy_radio_size());
#endif
    h->read=read; h->receive=receive; h->ctx=ctx;
    atomic_init(&h->overflow,false); atomic_init(&h->stopping,false); atomic_init(&h->worker_exited,false);
    h->events=xQueueCreate(32,sizeof(link_event_t));
    esp_err_t error=h->events && h->group && h->radio ? ESP_OK : ESP_ERR_NO_MEM;
    if (error==ESP_OK) error=init_horizontal_classifier(&h->classifier);
    if (error==ESP_OK) error=mosaico_mag_presence_tracker_init(&h->presence,NULL,presence_event,h);
    if (error==ESP_OK) {
        mosaico_peer_link_config_t c=MOSAICO_PEER_LINK_CONFIG_DEFAULT();
        /* Keep the scene network ID to observe and reject legacy participants;
         * the ECHN/version envelope is a separate admission capability. */
        c.network_id=LINK_NETWORK_ID;
        error=mosaico_peer_link_init(&c,radio_event,h);h->radio_started=error==ESP_OK;
        if(error==ESP_OK) {
            atomic_store(&radio_inflight,false);atomic_store(&radio_completed,0);atomic_store(&radio_mac_failed,0);
            error=esp_now_register_send_cb(radio_sent);h->send_callback_registered=error==ESP_OK;
        }
    }
    if (error==ESP_OK) {
        h->self=mosaico_peer_link_get_device_id();
        uint32_t boot=mosaico_peer_link_get_boot_id();
        echoy_group_callbacks_t cb={owner_read,owner_receive,network_send,h};
        echoy_radio_callbacks_t rcb={radio_send,radio_edge,radio_lost,radio_receive,radio_delivery,h,radio_stamp,radio_available};
        if(!echoy_group_init(h->group,echoy_group_size(),h->self,boot,&cb) ||
           !echoy_radio_init(h->radio,echoy_radio_size(),h->self,boot,&rcb))error=ESP_ERR_INVALID_STATE;
        if (error==ESP_OK && xTaskCreate(worker,"echoy_link",7168,h,tskIDLE_PRIORITY+1,NULL)==pdPASS) {
            instance=h; return ESP_OK;
        }
        if (error==ESP_OK) error=ESP_ERR_NO_MEM;
    }
    atomic_store(&h->worker_exited,true);
    if (h->radio_started) {
        esp_err_t stopped=stop_radio(h);
        if (stopped!=ESP_OK) { instance=h; return stopped; }
    }
    if (h->events) vQueueDelete(h->events);
    free(h->group); free(h->radio); free(h); atomic_store(&owned,false); return error;
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
    esp_err_t error=stop_radio(h); if (error!=ESP_OK) return error;
    vQueueDelete(h->events); free(h->group); free(h->radio); free(h); instance=NULL;
    atomic_store_explicit(&owned,false,memory_order_release); return ESP_OK;
}
