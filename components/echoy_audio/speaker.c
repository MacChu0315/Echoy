// SPDX-License-Identifier: Apache-2.0
#include "echoy_speaker.h"
#include "echoy_audio_output.h"
#include "bsp/audio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

typedef struct {bool retry;echoy_bar_plan_t plan;} message_t;
typedef struct {
    echoy_audio_t *audio;echoy_audio_output_t output;esp_codec_dev_handle_t codec;
    unsigned device;QueueHandle_t inbox,status_queue;TaskHandle_t task;
    uint8_t inbox_memory[4*sizeof(message_t)],status_memory[sizeof(echoy_speaker_status_t)];
    echoy_speaker_status_t status;message_t incoming,outgoing;
} speaker_t;
static speaker_t *speaker;
/* Kernel queue/list metadata must remain in internal RAM. */
static StaticQueue_t inbox_control,status_control;
static const char *TAG="echoy-audio";
static int codec_open(void *ctx,unsigned rate,unsigned bits,unsigned channels)
{
    speaker_t *s=ctx;
    if(!s->codec)s->codec=bsp_audio_codec_speaker_init();
    if(!s->codec)return ESP_FAIL;
    esp_codec_dev_sample_info_t format={.sample_rate=rate,.bits_per_sample=bits,.channel=channels};
    int err=esp_codec_dev_open(s->codec,&format);
    if(err)return err;
    err=esp_codec_dev_set_out_vol(s->codec,100);
    /* echoy_output_open owns checked cleanup for every error, including a
     * partially opened esp_codec_dev and a later volume setup failure. */
    return err;
}
static int codec_write(void *ctx,const void *pcm,size_t bytes)
{
    speaker_t *s=ctx;return esp_codec_dev_write(s->codec,(void *)pcm,(int)bytes);
}
static int codec_close(void *ctx){speaker_t *s=ctx;return s->codec?esp_codec_dev_close(s->codec):0;}
static void publish_status(speaker_t *s)
{
    s->status.dsp=echoy_audio_stats(s->audio);s->status.output_fault=s->status.dsp.output_fault;
    s->status.retry_required=s->output.retry_required;s->status.codec_error=s->output.last_error;
    s->status.close_error=s->output.close_error;
    (void)xQueueOverwrite(s->status_queue,&s->status);
}
static void reopen(speaker_t *s)
{
    if(!echoy_output_open(&s->output,esp_timer_get_time()+s->status.estimated_dma_us)){
        ESP_LOGE(TAG,"speaker open failed (%d); explicit retry remains available",s->output.last_error);
    }else{
        /* Codec initialization can take time. Map the first rendered sample
         * after open, including the configured DMA lead, not before I2C setup. */
        echoy_audio_skip_to(s->audio,esp_timer_get_time()+s->status.estimated_dma_us);
    }
    publish_status(s);
}
static void audio_task(void *arg)
{
    speaker_t *s=arg;reopen(s);
    for(;;){
        TickType_t wait=s->output.retry_required?pdMS_TO_TICKS(25):0;
        if(xQueueReceive(s->inbox,&s->incoming,wait)==pdTRUE){
            unsigned drained=0;
            do {
                if(s->incoming.retry)reopen(s);
                else if(!echoy_audio_plan(s->audio,&s->incoming.plan,1u<<s->device))s->status.plan_rejections++;
            }while(++drained<4&&xQueueReceive(s->inbox,&s->incoming,0)==pdTRUE);
        }
        if(!s->output.retry_required){
            int64_t now=esp_timer_get_time();
            /* If the renderer has fallen behind the wall clock, never replay
             * a backlog. The codec's hidden DMA handles prevent exact hardware
             * underrun/onset measurement; Task 10 must measure it externally. */
            if(echoy_audio_presentation_us(s->audio)<now){
                s->status.output_gaps++;
                echoy_audio_skip_to(s->audio,now+s->status.estimated_dma_us);
            }
            int64_t began=esp_timer_get_time();
            if(!echoy_output_block(&s->output))ESP_LOGE(TAG,"speaker write failed (%d); waiting for retry",s->output.last_error);
            int64_t took=esp_timer_get_time()-began;
            if(took>s->status.max_render_write_us)s->status.max_render_write_us=took;
        }
        else echoy_audio_skip_to(s->audio,esp_timer_get_time()+s->status.estimated_dma_us);
        publish_status(s);
    }
}
esp_err_t echoy_speaker_start(int device)
{
    if(device<0||device>=ECHOY_MAX_DEVICES)return ESP_ERR_INVALID_ARG;
    if(speaker)return ESP_ERR_INVALID_STATE;
    speaker_t *s=heap_caps_calloc(1,sizeof *s,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    void *storage=heap_caps_malloc(echoy_audio_size(),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s||!storage){free(s);free(storage);return ESP_ERR_NO_MEM;}
    s->audio=echoy_audio_init(storage,echoy_audio_size(),esp_timer_get_time());s->device=(unsigned)device;
    if(!s->audio){free(storage);free(s);return ESP_FAIL;}
    s->inbox=xQueueCreateStatic(4,sizeof(message_t),s->inbox_memory,&inbox_control);
    s->status_queue=xQueueCreateStatic(1,sizeof(s->status),s->status_memory,&status_control);
    if(!s->inbox||!s->status_queue){free(storage);free(s);return ESP_ERR_NO_MEM;}
    /* Same macro used inside pinned BSP bsp_audio_init; BSP exposes no handle
     * or DMA-size setter. Do not pretend these are 4x256 DMA descriptors. */
    i2s_chan_config_t dma=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    s->status.dma_descriptors=dma.dma_desc_num;s->status.dma_frames=dma.dma_frame_num;
    s->status.estimated_dma_us=(int64_t)dma.dma_desc_num*dma.dma_frame_num*1000000/ECHOY_AUDIO_RATE;
    s->status.software_blocks=ECHOY_AUDIO_BLOCKS;s->status.block_frames=ECHOY_AUDIO_FRAMES;
    echoy_output_ops_t ops={codec_open,codec_write,codec_close,s};
    if(!echoy_output_init(&s->output,s->audio,ops)){free(storage);free(s);return ESP_FAIL;}
    ESP_LOGI(TAG,"48 kHz s16 stereo, software %dx%d frames; BSP DMA %ux%u frames (%lld us configured lead)",ECHOY_AUDIO_BLOCKS,ECHOY_AUDIO_FRAMES,dma.dma_desc_num,dma.dma_frame_num,(long long)s->status.estimated_dma_us);
    speaker=s;
    if(xTaskCreate(audio_task,"echoy_audio",8192,s,6,&s->task)!=pdPASS){speaker=NULL;free(storage);free(s);return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t echoy_speaker_publish(const echoy_bar_plan_t *plan)
{
    if(!speaker)return ESP_ERR_INVALID_STATE;
    if(!plan||plan->count>3)return ESP_ERR_INVALID_ARG;
    speaker->outgoing.retry=false;speaker->outgoing.plan=*plan;
    return xQueueSend(speaker->inbox,&speaker->outgoing,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
esp_err_t echoy_speaker_retry(void)
{
    if(!speaker)return ESP_ERR_INVALID_STATE;
    speaker->outgoing.retry=true;
    return xQueueSend(speaker->inbox,&speaker->outgoing,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
bool echoy_speaker_status(echoy_speaker_status_t *out)
{
    return speaker&&out&&xQueuePeek(speaker->status_queue,out,0)==pdTRUE;
}
