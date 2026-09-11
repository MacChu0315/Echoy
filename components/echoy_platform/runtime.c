// SPDX-License-Identifier: Apache-2.0
#include "echoy_runtime.h"
#include "echoy_platform.h"
#include "echoy_control.h"
#include "echoy_diagnostics.h"
#include "echoy_speaker.h"
#include "echoy_ui.h"
#include "esp_iris.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "nvs.h"
#include "bsp/esp_mosaico.h"
#include "mosaico_module_mgr.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#define SERVICE 0xec01
#define FRAME_BYTES (480u*480u*2u)
typedef struct {uint8_t kind;echoy_ui_event_t ui;uint8_t bytes[4];} command_t;
typedef struct {
    echoy_control_t control;echoy_core_t forecast;echoy_radio_t radio;echoy_contact_adapter_t contact;
    echoy_storage_t storage;echoy_battery_t battery;echoy_diagnostics_t diag;
    QueueHandle_t commands;SemaphoreHandle_t json_lock,mirror_lock;TaskHandle_t task;
    char json[4096],json_work[4096];size_t json_size;unsigned input_dropped;
    echoy_mirror_t mirror;bool speaker_started,ready,healthy;int64_t ui_at_us,diag_at_us,battery_at_us,contact_at_us;
    uint64_t audio_clock,audio_boot,audio_session,audio_source_clock;int64_t audio_epoch;int audio_rate;
    echoy_speaker_status_t speaker;echoy_radio_event_t event;command_t command;
} runtime_t;
static runtime_t *runtime;
static const char *TAG="echoy_platform";
static int store_read(void *ctx,uint8_t *out,size_t *n){
    (void)ctx;nvs_handle_t h;esp_err_t err=nvs_open("echoy",NVS_READONLY,&h);if(err!=ESP_OK)return err;
    err=nvs_get_blob(h,"settings",out,n);nvs_close(h);return err;
}
static int store_write(void *ctx,const uint8_t *b,size_t n){
    (void)ctx;nvs_handle_t h;esp_err_t err=nvs_open("echoy",NVS_READWRITE,&h);if(err!=ESP_OK)return err;
    err=nvs_set_blob(h,"settings",b,n);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);return err;
}
static bool enqueue(runtime_t *r,const command_t *cmd){
    if(xQueueSend(r->commands,cmd,0)==pdTRUE)return true;
    __atomic_fetch_add(&r->input_dropped,1,__ATOMIC_RELAXED);return false;
}
static void ui_emit(void *ctx,echoy_ui_event_t e){runtime_t *r=ctx;command_t cmd={.ui=e};(void)enqueue(r,&cmd);}
static esp_err_t snapshot_rpc(const esp_iris_rpc_request_t *req,uint8_t *out,size_t cap,size_t *n,void *ctx){
    runtime_t *r=ctx;if(!req||req->payload_size||!out||!n)return ESP_ERR_INVALID_ARG;
    if(xSemaphoreTake(r->json_lock,pdMS_TO_TICKS(20))!=pdTRUE)return ESP_ERR_TIMEOUT;
    esp_err_t err=r->json_size&&cap>=r->json_size?ESP_OK:ESP_ERR_INVALID_SIZE;
    if(err==ESP_OK){memcpy(out,r->json,r->json_size);*n=r->json_size;}else *n=0;
    xSemaphoreGive(r->json_lock);return err;
}
#if CONFIG_ECHOY_DEV_INJECTION
static esp_err_t inject_rpc(const esp_iris_rpc_request_t *req,uint8_t *out,size_t cap,size_t *n,void *ctx){
    (void)out;(void)cap;runtime_t *r=ctx;command_t cmd={0};if(!req||!n)return ESP_ERR_INVALID_ARG;*n=0;
    if(req->method_id==2){if(!echoy_diagnostics_injection(req->payload,req->payload_size,true,&cmd.ui))return ESP_ERR_INVALID_ARG;}
    else if(req->method_id==3){if(req->payload_size!=1||req->payload[0]>1)return ESP_ERR_INVALID_ARG;cmd.kind=1;cmd.bytes[0]=req->payload[0];}
    else {if(req->payload_size!=4||req->payload[0]>2||req->payload[1]>100)return ESP_ERR_INVALID_ARG;cmd.kind=2;memcpy(cmd.bytes,req->payload,4);}
    return enqueue(r,&cmd)?ESP_OK:ESP_ERR_TIMEOUT;
}
#endif
static bool capture(void *ctx,uint8_t **pixels,size_t *size){
    (void)ctx;uint8_t *p=heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!p)return false;
    if(!bsp_display_lock(200)){heap_caps_free(p);return false;}
    lv_display_t *display=lv_display_get_default();lv_draw_buf_t draw;
    bool ok=display&&lv_display_get_horizontal_resolution(display)==480&&lv_display_get_vertical_resolution(display)==480&&
        lv_draw_buf_init(&draw,480,480,LV_COLOR_FORMAT_RGB565,960,p,FRAME_BYTES)==LV_RESULT_OK&&
        lv_snapshot_take_to_draw_buf(lv_display_get_screen_active(display),LV_COLOR_FORMAT_RGB565,&draw)==LV_RESULT_OK&&draw.header.w==480&&draw.header.h==480&&draw.header.stride==960;
    bsp_display_unlock();if(!ok){heap_caps_free(p);return false;}*pixels=p;*size=FRAME_BYTES;return true;
}
static void release(void *ctx,uint8_t *pixels){(void)ctx;heap_caps_free(pixels);}
static esp_err_t screen_begin(const esp_iris_media_desc_t *req,esp_iris_media_desc_t *actual,uint32_t *total,void *ctx){
    (void)req;runtime_t *r=ctx;if(!actual||!total)return ESP_ERR_INVALID_ARG;
    if(xSemaphoreTake(r->mirror_lock,pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    bool ok=echoy_mirror_begin(&r->mirror);xSemaphoreGive(r->mirror_lock);if(!ok)return ESP_ERR_INVALID_STATE;
    *actual=(esp_iris_media_desc_t){.width=480,.height=480,.stride=960,.format=ESP_IRIS_PIXEL_FORMAT_RGB565};*total=FRAME_BYTES;return ESP_OK;
}
static esp_err_t screen_read(uint32_t off,uint8_t *out,size_t cap,size_t *n,void *ctx){
    runtime_t *r=ctx;if(xSemaphoreTake(r->mirror_lock,pdMS_TO_TICKS(20))!=pdTRUE)return ESP_ERR_TIMEOUT;
    bool ok=echoy_mirror_read(&r->mirror,off,out,cap,n);xSemaphoreGive(r->mirror_lock);return ok?ESP_OK:ESP_ERR_INVALID_ARG;
}
static void screen_end(void *ctx){runtime_t *r=ctx;if(xSemaphoreTake(r->mirror_lock,portMAX_DELAY)==pdTRUE){echoy_mirror_end(&r->mirror);xSemaphoreGive(r->mirror_lock);}}
static bool publish(void *ctx,const echoy_snapshot_t *s,const echoy_bar_plan_t *plan){
    runtime_t *r=ctx;bool ok=true;
    if(r->speaker_started){
        /* Prepare the ordinary next bar 40 ms ahead of the configured 30 ms
         * codec lead. The forecast is a private copy, never the owner's core. */
        if(!echoy_control_forecast(&r->control,s->time_us+40000,&r->forecast)){r->diag.audio_queue_failures++;return false;}
        /* Forecast clocks are independently monotonic: aborting a future
         * COMMIT returns to the old epoch but must still cancel queued notes. */
        for(size_t i=0;i<r->forecast.plan.count;i++){
            echoy_plan_entry_t *entry=&r->forecast.plan.devices[i];if(entry->device!=0)continue;
            if(!r->audio_clock||entry->boot_id!=r->audio_boot||entry->session_id!=r->audio_session||entry->epoch_us!=r->audio_epoch||entry->rate_ppm!=r->audio_rate||entry->clock_version!=r->audio_source_clock){
                r->audio_clock++;r->audio_boot=entry->boot_id;r->audio_session=entry->session_id;r->audio_epoch=entry->epoch_us;r->audio_rate=entry->rate_ppm;r->audio_source_clock=entry->clock_version;
            }
            entry->clock_version=r->audio_clock;
        }
        const echoy_bar_plan_t *audio_plan=&r->forecast.plan;
        if(echoy_speaker_publish(audio_plan)!=ESP_OK){r->diag.audio_queue_failures++;ok=false;}
    }
    if(!r->diag.ui_error&&s->time_us-r->ui_at_us>=33000&&bsp_display_lock(0)){
        r->ui_at_us=s->time_us;echoy_view_t *v=echoy_ui_view();if(v){echoy_view_update(v,s,0);echoy_view_locale(v,r->control.english);}bsp_display_unlock();
    }
    (void)plan;return ok;
}
static void refresh_diagnostics(runtime_t *r){
    esp_iris_status_t iris;if(esp_iris_get_status(&iris)==ESP_OK)r->diag.iris_boot=iris.boot_id;
    (void)esp_iris_format_device_id(r->diag.iris_id);
    r->diag.heap_free=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);r->diag.stack_free=uxTaskGetStackHighWaterMark(NULL);
    r->diag.radio_dropped=__atomic_load_n(&r->radio.dropped,__ATOMIC_RELAXED);r->diag.contact_errors=r->contact.errors;
    r->diag.settings_error=r->storage.last_error;r->diag.settings_failures=r->storage.failures;
    r->diag.battery_error=r->battery.last_error;r->diag.battery_failures=r->battery.failures;
    r->control.dropped+=__atomic_exchange_n(&r->input_dropped,0,__ATOMIC_RELAXED);
    if(echoy_speaker_status(&r->speaker)){r->diag.audio=r->speaker.dsp;r->diag.audio_error=r->speaker.codec_error;r->diag.output_gaps=r->speaker.output_gaps;}
    size_t n;if(echoy_diagnostics_format(&r->control,&r->diag,r->json_work,sizeof r->json_work,&n)&&xSemaphoreTake(r->json_lock,0)==pdTRUE){memcpy(r->json,r->json_work,n);r->json_size=n;xSemaphoreGive(r->json_lock);}
}
static void observe_battery(runtime_t *r,int err,unsigned charge,int64_t now,bool real){
    if(echoy_battery_observe(&r->battery,err,charge)&&r->ready){echoy_input_t e={.type=ECHOY_BATTERY,.device=0,.value=charge,.at_us=now};(void)echoy_dispatch(&r->control.core,&e);}
    if(!err&&charge==0&&real){
        /* This is the verified BSP open-drain shutdown request; no flash erase,
         * guessed rail switching, or sensor-error-to-zero conversion. */
        (void)echoy_storage_flush(&r->storage,INT64_MAX-ECHOY_SETTINGS_DEBOUNCE_US);
        esp_err_t power=bsp_power_set_shutdown(true);if(power!=ESP_OK){ESP_LOGE(TAG,"shutdown request failed: %s",esp_err_to_name(power));r->battery.last_error=power;}
    }
}
static void owner_task(void *ctx){
    runtime_t *r=ctx;r->storage.read=store_read;r->storage.write=store_write;
    if(!echoy_storage_load(&r->storage)&&r->storage.last_error==ESP_ERR_NVS_NOT_FOUND){r->storage.last_error=0;r->storage.failures=0;
#if CONFIG_ECHOY_DEFAULT_ENGLISH
        r->storage.saved.locale=1;
#endif
    }
    esp_err_t radio=echoy_radio_start(&r->radio);
    if(radio==ESP_OK&&r->radio.uid&&r->radio.boot){
        uint32_t nonce;do{nonce=esp_random();}while(!nonce);
        echoy_control_init(&r->control,r->radio.uid,r->radio.boot,nonce,&r->storage.saved,echoy_radio_send,&r->radio);
        r->control.publish=publish;r->control.publish_ctx=r;r->ready=true;
        esp_err_t contact=echoy_contact_start(&r->contact,&r->radio);if(contact!=ESP_OK){r->contact.errors++;ESP_LOGW(TAG,"contact unavailable: %s",esp_err_to_name(contact));}
        r->diag.audio_error=echoy_speaker_start(0);r->speaker_started=r->diag.audio_error==ESP_OK;
    }else{ESP_LOGE(TAG,"radio identity unavailable: %s; Iris remains available",esp_err_to_name(radio));r->diag.audio_error=ESP_ERR_INVALID_STATE;}
    if(!r->diag.ui_error&&bsp_display_lock(200)){echoy_view_t *v=echoy_ui_view();if(v)echoy_view_set_sink(v,ui_emit,r);bsp_display_unlock();}
    esp_err_t battery=bsp_battery_init();if(battery!=ESP_OK)(void)echoy_battery_observe(&r->battery,battery,0);
    /* These APIs only inspect an existing driver/manager. Do not initialize IMU
     * streaming or the module manager and never claim a slot for diagnostics. */
    r->diag.imu_error=bsp_imu_get_handle()?ESP_OK:ESP_ERR_INVALID_STATE;
    for(int i=0;i<2;i++){mosaico_module_mgr_info_t info;r->diag.module_error[i]=mosaico_module_mgr_get_info((mosaico_module_mgr_slot_t)i,&info);
        if(r->diag.module_error[i]==ESP_OK){r->diag.module_type[i]=info.eeprom.board_type;r->diag.module_state[i]=info.state;}}
    TickType_t wake=xTaskGetTickCount();
    for(;;){
        int64_t now=esp_timer_get_time();
        unsigned drained=0;while(drained++<ECHOY_CONTROL_DEPTH&&xQueueReceive(r->commands,&r->command,0)==pdTRUE){
            command_t *cmd=&r->command;
            if(cmd->kind==1)r->control.english=cmd->bytes[0];
            else if(cmd->kind==2){if(cmd->bytes[0]==0)observe_battery(r,cmd->bytes[2],cmd->bytes[1],now,false);else if(cmd->bytes[0]==1)r->diag.imu_error=cmd->bytes[2];else{unsigned ix=cmd->bytes[3]&1;r->diag.module_type[ix]=cmd->bytes[1];r->diag.module_error[ix]=cmd->bytes[2];}}
            else if(cmd->ui.action==UI_RECOVER){if(r->speaker_started)r->diag.audio_error=echoy_speaker_retry();else{r->diag.audio_error=echoy_speaker_start(0);r->speaker_started=r->diag.audio_error==ESP_OK;}}
            else if(r->ready)(void)echoy_control_enqueue(&r->control,cmd->ui);
        }
        if(r->ready){
            drained=0;while(drained++<ECHOY_RADIO_QUEUE_DEPTH&&echoy_radio_poll(&r->radio,&r->event)){
                echoy_radio_event_t *event=&r->event;
                if(event->kind==ECHOY_RADIO_NEIGHBOR){if(echoy_contact_neighbor(&r->contact,&event->data.neighbor.value,event->data.neighbor.attached)!=ECHOY_OK)r->contact.errors++;}
                else if(event->kind==ECHOY_RADIO_PACKET){const mosaico_peer_message_t *m=&event->data.packet;
                    echoy_control_physical(&r->control,r->contact.left_uid,r->contact.right_uid,r->contact.uncertain||r->contact.ambiguous||r->contact.unsupported,now);
                    (void)echoy_control_receive(&r->control,m->source_id,m->source_boot_id,m->payload,m->payload_len,event->received_us,now);}
                else r->contact.errors++;
            }
            if(r->contact.initialized&&now-r->contact_at_us>=20000){r->contact_at_us=now;(void)echoy_contact_sample(&r->contact,now);}
            echoy_control_physical(&r->control,r->contact.left_uid,r->contact.right_uid,r->contact.uncertain||r->contact.ambiguous||r->contact.unsupported,now);
            bool fault=!r->speaker_started||!echoy_speaker_status(&r->speaker)||r->speaker.output_fault;
            echoy_control_tick(&r->control,now,fault);
            if(!r->healthy&&!fault&&!r->diag.ui_error&&now>3000000){
                esp_err_t accepted=esp_iris_mark_healthy();
                if(accepted==ESP_OK)r->healthy=true;
            }
            echoy_settings_t settings;echoy_settings_capture(&settings,&r->control.core.devices[0],r->control.english);
            (void)echoy_storage_stage(&r->storage,&settings,now);(void)echoy_storage_flush(&r->storage,now);
        }
        if(now-r->battery_at_us>=5000000){r->battery_at_us=now;bsp_battery_status_t status={0};esp_err_t err=bsp_battery_read(&status);observe_battery(r,err,status.state_of_charge,now,true);}
        if(now-r->diag_at_us>=250000){r->diag_at_us=now;refresh_diagnostics(r);}
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(10));
    }
}
esp_err_t echoy_runtime_start(esp_err_t ui_error){
    if(runtime)return ESP_ERR_INVALID_STATE;
    runtime_t *r=heap_caps_calloc(1,sizeof *r,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!r)return ESP_ERR_NO_MEM;r->diag.ui_error=ui_error;
    r->commands=xQueueCreate(ECHOY_CONTROL_DEPTH,sizeof(command_t));r->json_lock=xSemaphoreCreateMutex();r->mirror_lock=xSemaphoreCreateMutex();
    if(!r->commands||!r->json_lock||!r->mirror_lock)goto fail;
    r->mirror=(echoy_mirror_t){.ctx=r,.capture=capture,.release=release};
#if CONFIG_ECHOY_DEV_INJECTION
    r->diag.injection_enabled=true;
#endif
    refresh_diagnostics(r);
    if(xTaskCreate(owner_task,"echoy_control",8192,r,5,&r->task)!=pdPASS)goto fail;
    /* After the task starts this singleton owns the callback context for the
     * application lifetime. Registration failure cannot free live callbacks. */
    runtime=r;
    esp_err_t err=esp_iris_rpc_register(SERVICE,1,snapshot_rpc,r);
    if(err!=ESP_OK)return err;
#if CONFIG_ECHOY_DEV_INJECTION
    for(int method=2;method<=4;method++){
        err=esp_iris_rpc_register(SERVICE,method,inject_rpc,r);
        if(err!=ESP_OK)return err;
    }
#endif
    if(ui_error==ESP_OK){
        const esp_iris_screen_backend_t backend={.begin=screen_begin,.read=screen_read,.end=screen_end,.user_ctx=r};
        err=esp_iris_screen_register(&backend);
        if(err!=ESP_OK)ESP_LOGW(TAG,"mirror unavailable: %s",esp_err_to_name(err));
    }
    return ESP_OK;
fail:
    if(r->commands)vQueueDelete(r->commands);
    if(r->json_lock)vSemaphoreDelete(r->json_lock);
    if(r->mirror_lock)vSemaphoreDelete(r->mirror_lock);
    heap_caps_free(r);return ESP_ERR_NO_MEM;
}
