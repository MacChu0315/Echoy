#include "board_audio.h"
#include "bsp/esp_mosaico.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FAIL_NONE, FAIL_BOARD, FAIL_RAIL, FAIL_I2C, FAIL_GPIO, FAIL_CHANNEL,
       FAIL_INIT, FAIL_DATA, FAIL_CTRL, FAIL_CODEC, FAIL_DEVICE,
       FAIL_FORMAT, FAIL_ENABLE, FAIL_CODEC_START, FAIL_CURVE, FAIL_VOLUME, FAIL_MUTE,
       FAIL_WRONG_RATE, FAIL_WRONG_CHANNELS, FAIL_INFO, FAIL_PRELOAD,
       FAIL_PARTIAL, FAIL_SHORT, FAIL_PA, FAIL_READBACK,
       FAIL_RX_INIT, FAIL_RX_ENABLE, FAIL_GAIN, FAIL_RX_PAIR, FAIL_RX_DMA,
       FAIL_READ_PARTIAL, FAIL_READ_SHORT, FAIL_READ_ODD, FAIL_READ_OVERSIZE,
       FAIL_TX_PAIR, FAIL_RX_SLOT, FAIL_RX_CALLBACK, FAIL_RX_OVERFLOW,
       FAIL_DIAG_UNMUTE, FAIL_PRE_UNMUTE_DEADLINE, FAIL_UNMUTE_DEADLINE,
       FAIL_UNMUTE_OVERFLOW, FAIL_PA_DEADLINE };
static uint8_t registers[256];
static int failure, pa_level, live, checks, muted, hardware_volume, input_gain;
static bool measuring, fail_board_allocation, injected_reg_error, dma_preloaded;
static unsigned read_calls, gain_calls, control_write_calls, unmute_calls, pa_high_calls;
static int64_t register_delay_us;
static size_t source_frame;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t n, size_t size) { if(fail_board_allocation){fail_board_allocation=false;return NULL;}return __real_calloc(n,size); }
static unsigned write_calls, preload_calls, delay_ticks, mute_before_open;
static int64_t now_us;
static int64_t transfer_us=10000;
static bsp_board_variant_t board_variant=BSP_BOARD_VARIANT_V1_2;
static bool fail_release, fail_close_mute, fail_rx_stop_once, rx_stop_failed;
static uint8_t accepted[32768];
static size_t accepted_bytes;
static uint32_t waits[32];
static esp_codec_dev_sample_info_t requested;
static int open_references, enable_references;
struct test_i2s_channel { i2s_chan_config_t channels; i2s_std_config_t format; bool enabled, initialized; uint8_t dma[720]; i2s_event_callbacks_t callbacks; void *user_data; };
static i2s_chan_handle_t channel, rx_channel;
typedef struct { audio_codec_ctrl_if_t base; } fake_ctrl_t;
typedef struct { audio_codec_data_if_t base; } fake_data_t;
typedef struct { audio_codec_if_t base; const audio_codec_ctrl_if_t *ctrl; bool enabled; } fake_codec_t;
typedef struct { esp_codec_dev_cfg_t cfg; bool opened; int volume; bool mute, gain_set; float gain; } fake_device_t;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
static void *allocate(size_t size) { void *p=calloc(1,size); assert(p); ++live; return p; }
static void release(void *p) { if(p){--live;free(p);} }

esp_err_t bsp_board_variant_get(bsp_board_variant_t *v) { injected_reg_error=false;dma_preloaded=false;*v=board_variant; return failure==FAIL_BOARD?ESP_FAIL:ESP_OK; }
esp_err_t bsp_power_set_vcc_3v3(bool on) { CHECK(on); return failure==FAIL_RAIL?ESP_FAIL:ESP_OK; }
esp_err_t bsp_power_set_codec_3v3(bool on) { CHECK(on); return ESP_OK; }
esp_err_t bsp_i2c_init(void) { return failure==FAIL_I2C?ESP_FAIL:ESP_OK; }
void *bsp_i2c_get_handle(void) { return (void*)1; }
void vTaskDelay(unsigned ticks) { delay_ticks+=ticks; now_us+=ticks*1000; }
int64_t esp_timer_get_time(void) { return now_us; }
esp_err_t gpio_config(const gpio_config_t *c) { CHECK(c->pin_bit_mask==(UINT64_C(1)<<45)); CHECK(c->mode==GPIO_MODE_OUTPUT); return failure==FAIL_GPIO?ESP_FAIL:ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level)
{
    CHECK(pin==45);
    if(level){
        ++pa_high_calls;
        if(measuring){
            CHECK(dma_preloaded&&channel&&rx_channel&&channel->enabled&&rx_channel->enabled&&!muted);
            for(size_t i=0;i<sizeof(channel->dma);++i)CHECK(channel->dma[i]==0);
        }
        if(failure==FAIL_PA)return ESP_FAIL;
        if(failure==FAIL_PA_DEADLINE)now_us+=12000000;
    }
    pa_level=level;return ESP_OK;
}
esp_err_t i2s_new_channel(const i2s_chan_config_t *c,i2s_chan_handle_t *tx,i2s_chan_handle_t *rx)
{
    CHECK(tx&&(measuring ? rx!=NULL : rx==NULL)); CHECK(c->id==0&&c->role==I2S_ROLE_MASTER);
    CHECK(c->auto_clear&&c->dma_desc_num==3&&c->dma_frame_num==120);
    if(failure==FAIL_CHANNEL)return ESP_ERR_NO_MEM;
    *tx=channel=allocate(sizeof(**tx));channel->channels=*c;
    if(rx){*rx=rx_channel=allocate(sizeof(**rx));rx_channel->channels=*c;}return ESP_OK;
}
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h,const i2s_std_config_t *c)
{
    CHECK(h==channel||h==rx_channel); CHECK(c->clk_cfg.sample_rate_hz==24000&&c->clk_cfg.mclk_multiple==256);
    CHECK(c->slot_cfg.data_bit_width==16&&c->slot_cfg.slot_mode==I2S_SLOT_MODE_MONO&&c->slot_cfg.slot_mask==I2S_STD_SLOT_LEFT);
    CHECK(c->gpio_cfg.mclk==54&&c->gpio_cfg.bclk==37&&c->gpio_cfg.ws==49&&c->gpio_cfg.dout==52&&c->gpio_cfg.din==(measuring?40:-1));
    if(failure==FAIL_INIT||(h==rx_channel&&failure==FAIL_RX_INIT))return ESP_FAIL;
    h->format=*c;h->initialized=true;
    if(failure==FAIL_WRONG_RATE)h->format.clk_cfg.sample_rate_hz=48000;
    if(failure==FAIL_WRONG_CHANNELS)h->format.slot_cfg.slot_mode=I2S_SLOT_MODE_STEREO;
    if(h==rx_channel&&failure==FAIL_RX_SLOT)h->format.slot_cfg.slot_mask=I2S_STD_SLOT_BOTH;
    return ESP_OK;
}
esp_err_t i2s_channel_get_info(i2s_chan_handle_t h,i2s_chan_info_t *out)
{
    if(failure==FAIL_INFO)return ESP_FAIL;
    *out=(i2s_chan_info_t){.dir=h==channel?I2S_DIR_TX:I2S_DIR_RX,.pair_chan=h==channel?(failure==FAIL_TX_PAIR?channel:rx_channel):(failure==FAIL_RX_PAIR?NULL:channel),.mode=I2S_COMM_MODE_STD,.is_enabled=h->enabled,.mode_cfg=&h->format,.total_dma_buf_size=(h==rx_channel&&failure==FAIL_RX_DMA)?1440:(h->initialized?720:0)};return ESP_OK;
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t h) { if(h==rx_channel)CHECK(channel&&channel->enabled); if(failure==FAIL_ENABLE||(h==rx_channel&&failure==FAIL_RX_ENABLE))return ESP_FAIL;CHECK(!h->enabled);h->enabled=true;return ESP_OK; }
esp_err_t i2s_channel_disable(i2s_chan_handle_t h) { if(!h->enabled)return ESP_ERR_INVALID_STATE; if(h==rx_channel&&fail_rx_stop_once){fail_rx_stop_once=false;rx_stop_failed=true;return ESP_FAIL;}h->enabled=false;return ESP_OK; }
esp_err_t i2s_del_channel(i2s_chan_handle_t h) { if(h->enabled)return ESP_ERR_INVALID_STATE; if(h==channel)channel=NULL;else rx_channel=NULL;release(h);return ESP_OK; }
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t h,const void *data,size_t size,size_t *written)
{
    CHECK(!pa_level&&!h->enabled); CHECK(size==720); ++preload_calls;
    if(failure==FAIL_PRELOAD){*written=0;return ESP_FAIL;}
    for(size_t i=0;i<size;++i)CHECK(((const uint8_t*)data)[i]==0);
    memcpy(h->dma,data,size);*written=size;dma_preloaded=true;
    if(failure==FAIL_PRE_UNMUTE_DEADLINE)now_us+=11950000;
    return ESP_OK;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t h,const void *data,size_t size,size_t *written,uint32_t timeout)
{
    CHECK(h->enabled&&size<=240); CHECK(timeout>0&&timeout<=40);
    waits[write_calls++%32]=timeout;
    if(transfer_us>timeout*INT64_C(1000)){now_us+=timeout*INT64_C(1000);*written=0;return ESP_ERR_TIMEOUT;}
    now_us+=transfer_us;
    *written=(failure==FAIL_PARTIAL||failure==FAIL_SHORT)?80:size;
    CHECK(accepted_bytes+*written<=sizeof(accepted));memcpy(accepted+accepted_bytes,data,*written);accepted_bytes+=*written;
    memcpy(h->dma,data,*written);
    return failure==FAIL_PARTIAL?ESP_ERR_TIMEOUT:ESP_OK;
}
esp_err_t i2s_channel_register_event_callback(i2s_chan_handle_t h,const i2s_event_callbacks_t *callbacks,void *user_data)
{
    CHECK(h==rx_channel&&!h->enabled&&callbacks->on_recv_q_ovf);
    if(failure==FAIL_RX_CALLBACK)return ESP_FAIL;
    h->callbacks=*callbacks;h->user_data=user_data;return ESP_OK;
}
esp_err_t i2s_channel_read(i2s_chan_handle_t h,void *data,size_t size,size_t *read,uint32_t timeout)
{
    CHECK(h==rx_channel&&h->enabled&&size<=240&&timeout>0&&timeout<=40);
    ++read_calls;
    if(transfer_us>timeout*INT64_C(1000)){now_us+=timeout*INT64_C(1000);*read=0;return ESP_ERR_TIMEOUT;}
    now_us+=transfer_us;
    if(failure==FAIL_RX_OVERFLOW&&h->callbacks.on_recv_q_ovf)
        CHECK(!h->callbacks.on_recv_q_ovf(h,NULL,h->user_data));
    *read=(failure==FAIL_READ_PARTIAL||failure==FAIL_READ_SHORT)?80:size;
    if(failure==FAIL_READ_ODD)*read=3;
    if(failure==FAIL_READ_OVERSIZE){*read=size+2;return ESP_OK;}
    for(size_t i=0;i<*read/2;++i)((int16_t*)data)[i]=(int16_t)(1000+source_frame++);
    return failure==FAIL_READ_PARTIAL?ESP_ERR_TIMEOUT:ESP_OK;
}
static bool opened_ctrl(const audio_codec_ctrl_if_t *h) { (void)h;return true; }
static int read_reg(const audio_codec_ctrl_if_t *h,int reg,int bytes,void *data,int len) { (void)h;(void)bytes;CHECK(len==1);if(failure==FAIL_READBACK&&reg==0x32)return ESP_ERR_TIMEOUT;*(uint8_t*)data=registers[(uint8_t)reg];return ESP_OK; }
static int write_reg(const audio_codec_ctrl_if_t *h,int reg,int bytes,void *data,int len)
{
    (void)h;(void)bytes;(void)len;
    ++control_write_calls;now_us+=register_delay_us;
    if(reg==0x31&&!*(uint8_t*)data){
        ++unmute_calls;
        if(failure==FAIL_UNMUTE_DEADLINE)now_us+=12000000;
        if(failure==FAIL_UNMUTE_OVERFLOW)
            CHECK(!rx_channel->callbacks.on_recv_q_ovf(rx_channel,NULL,rx_channel->user_data));
    }
    if(fail_close_mute&&reg==0x31)return ESP_CODEC_DEV_WRITE_FAIL;
    if(!injected_reg_error&&((reg==0x31&&failure==FAIL_MUTE)||(reg==0x32&&failure==FAIL_VOLUME)||
       (reg==0x00&&failure==FAIL_CODEC_START)||(reg==0x16&&failure==FAIL_GAIN)||
       (reg==0x31&&!*(uint8_t*)data&&failure==FAIL_DIAG_UNMUTE))){injected_reg_error=true;return ESP_CODEC_DEV_WRITE_FAIL;}
    if(reg==0x31)muted=*(uint8_t*)data;
    registers[(uint8_t)reg]=*(uint8_t*)data;
    return ESP_OK;
}
static int ctrl_info(const audio_codec_ctrl_if_t *h,audio_codec_ctrl_info_t *info) { (void)h;*info=(audio_codec_ctrl_info_t){.type=AUDIO_CODEC_CTRL_I2C,.i2c={.addr=0x32,.port=0}};return ESP_OK; }
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *c)
{
    CHECK(c->addr==0x32&&c->bus_handle==(void*)1);
    if(failure==FAIL_CTRL)return NULL;
    fake_ctrl_t *f=allocate(sizeof(*f));f->base=(audio_codec_ctrl_if_t){.is_open=opened_ctrl,.read_reg=read_reg,.write_reg=write_reg,.get_info=ctrl_info};return &f->base;
}
static bool opened_data(const audio_codec_data_if_t *h) { (void)h;return true; }
static int enable_data(const audio_codec_data_if_t *h,esp_codec_dev_type_t type,bool on) { (void)h;CHECK(type==ESP_CODEC_DEV_TYPE_OUT);return on?i2s_channel_enable(channel):i2s_channel_disable(channel); }
static int set_fmt(const audio_codec_data_if_t *h,esp_codec_dev_type_t type,esp_codec_dev_sample_info_t *fs)
{
    (void)h;CHECK(type==ESP_CODEC_DEV_TYPE_OUT);requested=*fs;
    if(failure==FAIL_WRONG_RATE)channel->format.clk_cfg.sample_rate_hz=48000;
    if(failure==FAIL_WRONG_CHANNELS)channel->format.slot_cfg.slot_mode=I2S_SLOT_MODE_STEREO;
    return failure==FAIL_FORMAT?ESP_FAIL:ESP_OK;
}
const audio_codec_data_if_t *audio_codec_new_i2s_data(audio_codec_i2s_cfg_t *c)
{
    CHECK(c->tx_handle==channel&&c->rx_handle==rx_channel&&c->port==0);
    if(failure==FAIL_DATA)return NULL;
    fake_data_t *f=allocate(sizeof(*f));f->base=(audio_codec_data_if_t){.is_open=opened_data,.enable=enable_data,.set_fmt=set_fmt};return &f->base;
}
static bool opened_codec(const audio_codec_if_t *h) { (void)h;return true; }
static int codec_mute(const audio_codec_if_t *h,bool mute) { const fake_codec_t *c=(const fake_codec_t*)h;uint8_t v=mute;return c->ctrl->write_reg(c->ctrl,0x31,1,&v,1); }
static int codec_volume(const audio_codec_if_t *h,float db) { const fake_codec_t *c=(const fake_codec_t*)h;uint8_t v=(uint8_t)(db+100);return c->ctrl->write_reg(c->ctrl,0x32,1,&v,1); }
static int codec_gain(const audio_codec_if_t *h,float gain) { const fake_codec_t *c=(const fake_codec_t*)h;uint8_t v=(uint8_t)gain;input_gain=(int)gain;++gain_calls;return c->ctrl->write_reg(c->ctrl,0x16,1,&v,1); }
static int codec_enable(const audio_codec_if_t *h,bool on)
{
    fake_codec_t *c=(fake_codec_t*)h;
    if(measuring&&!on)CHECK(!pa_level&&(!rx_channel||!rx_channel->enabled||rx_stop_failed));
    if(on==c->enabled)return ESP_OK;
    if(on)++enable_references;else --enable_references;
    uint8_t v=on;int error=c->ctrl->write_reg(c->ctrl,0x00,1,&v,1);
    /* The pinned driver acquires its enable reference before I2C, but changes
     * enabled only on success: a raw transport error would strand that ref. */
    if(error==ESP_OK)c->enabled=on;
    return error;
}
const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *c)
{
    CHECK(!pa_level);CHECK(c->pa_pin==-1&&c->codec_mode==(measuring?ESP_CODEC_DEV_WORK_MODE_BOTH:ESP_CODEC_DEV_WORK_MODE_DAC)&&!c->master_mode&&c->use_mclk);
    CHECK(c->ctrl_if&&c->hw_gain.pa_voltage==5&&c->hw_gain.codec_dac_voltage==3.3f);
    if(failure==FAIL_CODEC)return NULL;
    fake_codec_t *f=allocate(sizeof(*f));++open_references;f->ctrl=c->ctrl_if;
    f->base=(audio_codec_if_t){.is_open=opened_codec,.mute=codec_mute,.set_vol=codec_volume,.set_mic_gain=codec_gain,.enable=codec_enable};return &f->base;
}
esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *c)
{
    CHECK(c->dev_type==(measuring?ESP_CODEC_DEV_TYPE_IN_OUT:ESP_CODEC_DEV_TYPE_OUT));if(failure==FAIL_DEVICE)return NULL;
    fake_device_t *d=allocate(sizeof(*d));d->cfg=*c;return d;
}
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t handle,bool mute)
{
    fake_device_t *d=handle;if(!d->opened&&mute)++mute_before_open;d->mute=mute;
    (void)d->cfg.codec_if->mute(d->cfg.codec_if,mute);return ESP_OK; /* Real facade ignores this error. */
}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle,int volume)
{
    fake_device_t *d=handle;hardware_volume=d->volume=volume;(void)d->cfg.codec_if->set_vol(d->cfg.codec_if,-50+volume*.5f);return ESP_OK;
}
int esp_codec_dev_set_vol_curve(esp_codec_dev_handle_t handle,esp_codec_dev_vol_curve_t *curve)
{
    (void)handle;CHECK(curve->count==2&&curve->vol_map[0].vol==0&&curve->vol_map[0].db_value==-50);
    CHECK(curve->vol_map[1].vol==100&&curve->vol_map[1].db_value==0);
    return failure==FAIL_CURVE?ESP_ERR_NO_MEM:ESP_OK;
}
int esp_codec_dev_open(esp_codec_dev_handle_t handle,esp_codec_dev_sample_info_t *fs)
{
    fake_device_t *d=handle;CHECK(!pa_level);
    if(measuring)CHECK(d->gain_set);
    d->opened=true;
    (void)d->cfg.data_if->set_fmt(d->cfg.data_if,d->cfg.dev_type,fs);
    (void)d->cfg.data_if->enable(d->cfg.data_if,d->cfg.dev_type,true);
    if(d->cfg.codec_if->enable(d->cfg.codec_if,true)!=ESP_OK)return ESP_FAIL;
    (void)esp_codec_dev_set_out_mute(d,d->mute);(void)esp_codec_dev_set_out_vol(d,d->volume);
    if(measuring)(void)esp_codec_dev_set_in_gain(d,d->gain);
    return ESP_OK; /* Mirrors the real unchecked data-interface returns. */
}
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t handle,float gain) { fake_device_t *d=handle;d->gain_set=true;d->gain=gain;(void)d->cfg.codec_if->set_mic_gain(d->cfg.codec_if,gain);return ESP_OK; }
int esp_codec_dev_read(esp_codec_dev_handle_t handle,void *data,int size) { fake_device_t *d=handle;return d->cfg.data_if->read(d->cfg.data_if,data,size); }
int esp_codec_dev_write(esp_codec_dev_handle_t handle,void *data,int size) { fake_device_t *d=handle;return d->cfg.data_if->write(d->cfg.data_if,data,size); }
int esp_codec_dev_read_reg(esp_codec_dev_handle_t handle,int reg,int *value) { fake_device_t *d=handle;const fake_codec_t *c=(const fake_codec_t*)d->cfg.codec_if;*value=0;return c->ctrl->read_reg(c->ctrl,reg,1,value,1); }
int esp_codec_dev_close(esp_codec_dev_handle_t handle) { fake_device_t *d=handle;if(d->opened){d->cfg.codec_if->enable(d->cfg.codec_if,false);d->cfg.data_if->enable(d->cfg.data_if,d->cfg.dev_type,false);d->opened=false;}return ESP_OK; }
void esp_codec_dev_delete(esp_codec_dev_handle_t h) { esp_codec_dev_close(h);release(h); }
int audio_codec_delete_codec_if(const audio_codec_if_t *h) { --open_references;release((void*)h);return ESP_OK; }
int audio_codec_delete_data_if(const audio_codec_data_if_t *h) { release((void*)h);return ESP_OK; }
int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *h) { release((void*)h);return fail_release?ESP_FAIL:ESP_OK; }

int main(void)
{
    board_audio_t *audio=NULL;
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    const char *cleanup_case=getenv("BOARD_AUDIO_CLEANUP_CASE");
    if(cleanup_case){
        measuring=true;
        CHECK(board_audio_measure_open(&audio,0,now_us+12000000)==ESP_OK);
        int16_t sample=100;size_t count=0;
        CHECK(board_audio_write(audio,&sample,1,&count)==ESP_OK&&pa_level);
        if(!strcmp(cleanup_case,"i2c"))fail_close_mute=true;
        else if(!strcmp(cleanup_case,"rx"))fail_rx_stop_once=true;
        else fail_release=true;
        CHECK(board_audio_close(&audio)!=ESP_OK&&!audio&&!pa_level&&live==0);
        CHECK(board_audio_open(&audio)==ESP_ERR_INVALID_STATE&&!audio);
        CHECK(board_audio_measure_open(&audio,0,now_us+12000000)==ESP_ERR_INVALID_STATE&&!audio);
        printf("board audio diagnostic cleanup %s checks: %d\n",cleanup_case,checks);
        return 0;
    }
#endif
    CHECK(board_audio_open(&audio)==ESP_OK);CHECK(audio&&live==5);
    CHECK(!pa_level&&muted&&mute_before_open&&hardware_volume==90&&delay_ticks>=10);
    CHECK(requested.sample_rate==24000&&requested.bits_per_sample==16&&requested.channel==1);
    board_audio_t *duplicate=NULL;CHECK(board_audio_open(&duplicate)==ESP_ERR_INVALID_STATE&&!duplicate);
    int16_t pcm[240];for(unsigned i=0;i<240;++i)pcm[i]=(int16_t)(i*271-32768);
    int16_t unchanged[240];memcpy(unchanged,pcm,sizeof(pcm));size_t written=0;
    CHECK(board_audio_write(audio,pcm,240,&written)==ESP_OK&&written==240);
    CHECK(accepted_bytes==sizeof(pcm)&&!memcmp(accepted,pcm,sizeof(pcm))&&!memcmp(unchanged,pcm,sizeof(pcm)));
    CHECK(pa_level==1&&!muted&&write_calls==2&&waits[0]==40&&waits[1]==30);
    board_audio_stats_t stats;CHECK(board_audio_get_stats(audio,&stats)==ESP_OK);
    CHECK(stats.frames_written==240&&stats.queue_capacity_frames==360&&stats.write_calls==1);
    CHECK(stats.nonzero_frames==240&&stats.peak_pcm==32768&&stats.pa_enabled&&stats.pa_activations==1);
    CHECK(stats.last_write_us==20000&&stats.max_write_us==20000);
    board_audio_probe_t probe;
    CHECK(board_audio_readback(audio,&probe)==ESP_OK&&probe.valid_mask==127&&probe.pa_requested);
    CHECK(probe.registers[4]==95&&probe.registers[3]==0); /* Stub register mapping, not chip dB. */
    CHECK(board_audio_flush(audio)==ESP_OK&&!pa_level&&muted);
    CHECK(preload_calls==2);for(unsigned i=0;i<720;++i)CHECK(channel->dma[i]==0);
    memset(pcm,0,sizeof(pcm));CHECK(board_audio_write(audio,pcm,240,&written)==ESP_OK&&!pa_level&&muted);
    CHECK(board_audio_get_stats(audio,&stats)==ESP_OK&&stats.nonzero_frames==240&&!stats.pa_enabled&&stats.pa_activations==1);
    CHECK(board_audio_write(audio,pcm,481,&written)==ESP_ERR_INVALID_ARG&&written==0);
    failure=FAIL_READBACK;
    CHECK(board_audio_readback(audio,&probe)==ESP_ERR_TIMEOUT&&probe.valid_mask==15);
    failure=FAIL_NONE;
    CHECK(board_audio_close(&audio)==ESP_OK&&!audio&&live==0&&!pa_level);

    for(failure=FAIL_BOARD;failure<=FAIL_PRELOAD;++failure) {
        CHECK(board_audio_open(&audio)!=ESP_OK&&!audio&&live==0&&!pa_level);
        CHECK(open_references==0&&enable_references==0);
        int saved=failure;failure=FAIL_NONE;
        CHECK(board_audio_open(&audio)==ESP_OK);CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
        failure=saved;
    }
    failure=FAIL_NONE;CHECK(board_audio_open(&audio)==ESP_OK);failure=FAIL_PARTIAL;
    pcm[0]=100;CHECK(board_audio_write(audio,pcm,240,&written)==ESP_ERR_TIMEOUT&&written==40&&!pa_level);
    CHECK(board_audio_get_stats(audio,&stats)==ESP_OK&&stats.write_failures==1&&stats.write_timeouts==1&&stats.partial_writes==1&&stats.frames_written==40);
    CHECK(stats.nonzero_frames==1&&stats.peak_pcm==100&&!stats.pa_enabled&&stats.pa_activations==0);
    CHECK(board_audio_write(audio,pcm,1,&written)==ESP_ERR_INVALID_STATE&&written==0);
    failure=FAIL_NONE;CHECK(board_audio_close(&audio)==ESP_OK&&!audio&&live==0);
    CHECK(board_audio_open(&audio)==ESP_OK);failure=FAIL_SHORT;
    CHECK(board_audio_write(audio,pcm,240,&written)!=ESP_OK&&written==40&&!pa_level);
    failure=FAIL_NONE;CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    CHECK(board_audio_open(&audio)==ESP_OK);failure=FAIL_PA;
    CHECK(board_audio_write(audio,pcm,240,&written)!=ESP_OK&&written==240&&!pa_level);
    failure=FAIL_NONE;CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    CHECK(board_audio_open(&audio)==ESP_OK);transfer_us=25000;
    int64_t before=now_us;unsigned first=write_calls;
    CHECK(board_audio_write(audio,pcm,240,&written)==ESP_ERR_TIMEOUT&&written==120&&!pa_level);
    CHECK(now_us-before==40000&&waits[first%32]==40&&waits[(first+1)%32]==15);
    transfer_us=10000;CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    board_variant=BSP_BOARD_VARIANT_V1_0;
    CHECK(board_audio_open(&audio)==ESP_OK);CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    CHECK(board_audio_open(NULL)==ESP_ERR_INVALID_ARG);
    CHECK(board_audio_close(&audio)==ESP_OK);

    /* Break caught: ordinary builds creating any ADC/RX, or normal handles
     * accepting capture, even while diagnostics are compiled in. */
    size_t received=999;
    CHECK(board_audio_measure_read(NULL,pcm,120,&received)!=ESP_OK&&received==0);
#if CONFIG_ECHOY_OUTPUT_MEASUREMENT
    measuring=true;
    CHECK(board_audio_measure_open(&audio,6,now_us+12000000)==ESP_OK);
    CHECK(audio&&live==6&&rx_channel&&rx_channel->enabled&&channel->enabled);
    CHECK(pa_level&&!muted&&hardware_volume==90&&input_gain==6&&gain_calls>0&&read_calls==0);
    CHECK(board_audio_get_stats(audio,&stats)==ESP_OK&&stats.pa_enabled&&stats.pa_activations==1);
    CHECK(stats.write_calls==0&&stats.frames_written==0&&stats.nonzero_frames==0&&stats.peak_pcm==0);
    unsigned settling_unmutes=unmute_calls,settling_activations=pa_high_calls;
    int16_t quiet[240]={0};
    CHECK(board_audio_write(audio,quiet,240,&written)==ESP_OK&&written==240&&pa_level);
    CHECK(unmute_calls==settling_unmutes&&pa_high_calls==settling_activations);
    CHECK(board_audio_open(&duplicate)==ESP_ERR_INVALID_STATE&&!duplicate);
    int16_t capture[240];source_frame=0;unsigned reads_before=read_calls,writes_before=write_calls;
    CHECK(board_audio_measure_read(audio,capture,240,&received)==ESP_OK&&received==240);
    CHECK(capture[0]==1000&&capture[119]==1119&&capture[120]==1120&&capture[239]==1239);
    CHECK(read_calls==reads_before+2&&write_calls==writes_before&&pa_level);
    CHECK(board_audio_measure_read(audio,capture,120,&received)==ESP_OK&&capture[0]==1240);
    pcm[0]=100;CHECK(board_audio_write(audio,pcm,240,&written)==ESP_OK&&pa_level);
    CHECK(unmute_calls==settling_unmutes&&pa_high_calls==settling_activations);
    CHECK(board_audio_get_stats(audio,&stats)==ESP_OK&&stats.pa_activations==1&&stats.frames_written==480);
    CHECK(board_audio_close(&audio)==ESP_OK&&!audio&&live==0&&!pa_level);
    const int diag_failures[]={FAIL_BOARD,FAIL_RAIL,FAIL_I2C,FAIL_GPIO,FAIL_CHANNEL,FAIL_INIT,
        FAIL_RX_INIT,FAIL_DATA,FAIL_CTRL,FAIL_CODEC,FAIL_DEVICE,FAIL_ENABLE,FAIL_RX_ENABLE,
        FAIL_CODEC_START,FAIL_CURVE,FAIL_VOLUME,FAIL_MUTE,FAIL_GAIN,FAIL_WRONG_RATE,FAIL_WRONG_CHANNELS,FAIL_RX_CALLBACK,FAIL_RX_PAIR,FAIL_TX_PAIR,FAIL_RX_SLOT,FAIL_RX_DMA,FAIL_INFO,FAIL_PRELOAD};
    for(size_t i=0;i<sizeof(diag_failures)/sizeof(diag_failures[0]);++i){
        failure=diag_failures[i];
        CHECK(board_audio_measure_open(&audio,12,now_us+12000000)!=ESP_OK&&!audio&&live==0&&!pa_level);
        CHECK(open_references==0&&enable_references==0);
        failure=FAIL_NONE;
        CHECK(board_audio_measure_open(&audio,12,now_us+12000000)==ESP_OK);
        CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    }
    const int settle_failures[]={FAIL_DIAG_UNMUTE,FAIL_PRE_UNMUTE_DEADLINE,
        FAIL_UNMUTE_DEADLINE,FAIL_UNMUTE_OVERFLOW,FAIL_PA,FAIL_PA_DEADLINE};
    const int settle_errors[]={ESP_CODEC_DEV_WRITE_FAIL,ESP_ERR_TIMEOUT,
        ESP_ERR_TIMEOUT,ESP_FAIL,ESP_FAIL,ESP_ERR_TIMEOUT};
    for(size_t i=0;i<sizeof(settle_failures)/sizeof(settle_failures[0]);++i){
        failure=settle_failures[i];
        unsigned unmute_before=unmute_calls,pa_before=pa_high_calls;
        CHECK(board_audio_measure_open(&audio,12,now_us+12000000)==settle_errors[i]);
        CHECK(!audio&&live==0&&!pa_level&&!channel&&!rx_channel);
        CHECK(open_references==0&&enable_references==0);
        CHECK(unmute_calls==unmute_before+(failure==FAIL_PRE_UNMUTE_DEADLINE?0:1));
        CHECK(pa_high_calls==pa_before+((failure==FAIL_PA||failure==FAIL_PA_DEADLINE)?1:0));
        failure=FAIL_NONE;
        CHECK(board_audio_measure_open(&audio,12,now_us+12000000)==ESP_OK&&pa_level);
        CHECK(board_audio_close(&audio)==ESP_OK&&live==0&&!pa_level);
    }
    CHECK(board_audio_measure_open(&audio,7,now_us+12000000)==ESP_ERR_INVALID_ARG&&!audio&&live==0);
    CHECK(board_audio_measure_open(&audio,0,now_us)==ESP_ERR_INVALID_ARG&&!audio&&live==0);
    CHECK(board_audio_measure_open(&audio,0,now_us+12000001)==ESP_ERR_INVALID_ARG&&!audio&&live==0);
    fail_board_allocation=true;
    CHECK(board_audio_measure_open(&audio,0,now_us+12000000)==ESP_ERR_NO_MEM&&!audio&&live==0);
    /* Setup expiry cannot leave RX active. */
    CHECK(board_audio_measure_open(&audio,0,now_us+5000)==ESP_ERR_TIMEOUT&&!audio&&live==0);
    const int read_failures[]={FAIL_READ_PARTIAL,FAIL_READ_SHORT,FAIL_READ_ODD,FAIL_READ_OVERSIZE,FAIL_RX_OVERFLOW};
    const size_t read_counts[]={40,40,1,0,120};
    for(size_t i=0;i<5;++i){
        CHECK(board_audio_measure_open(&audio,0,now_us+12000000)==ESP_OK);
        failure=read_failures[i];
        CHECK(board_audio_measure_read(audio,capture,240,&received)!=ESP_OK&&received==read_counts[i]);
        CHECK(!pa_level&&!rx_channel->enabled&&!channel->enabled);
        CHECK(board_audio_measure_read(audio,capture,1,&received)==ESP_ERR_INVALID_STATE&&received==0);
        failure=FAIL_NONE;CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    }
    int64_t limit=now_us+12000000;
    CHECK(board_audio_measure_open(&audio,0,limit)==ESP_OK);
    now_us=limit-15000;before=now_us;
    CHECK(board_audio_measure_read(audio,capture,240,&received)==ESP_ERR_TIMEOUT&&received==120);
    CHECK(now_us-before==15000&&!rx_channel->enabled&&!channel->enabled&&!pa_level);
    CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    limit=now_us+12000000;
    CHECK(board_audio_measure_open(&audio,0,limit)==ESP_OK);
    now_us=limit-15000;
    CHECK(board_audio_write(audio,pcm,240,&written)==ESP_ERR_TIMEOUT&&written==120&&!pa_level&&!rx_channel->enabled);
    CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    limit=now_us+12000000;
    CHECK(board_audio_measure_open(&audio,30,limit)==ESP_OK);
    now_us=limit-500;before=now_us;reads_before=read_calls;
    CHECK(board_audio_measure_read(audio,capture,1,&received)==ESP_ERR_TIMEOUT&&received==0);
    CHECK(now_us==before&&read_calls==reads_before&&!rx_channel->enabled&&!pa_level);
    CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    limit=now_us+12000000;
    CHECK(board_audio_measure_open(&audio,0,limit)==ESP_OK);
    now_us=limit-50000;register_delay_us=100000;
    unsigned control_before=control_write_calls;
    CHECK(board_audio_write(audio,pcm,1,&written)==ESP_OK&&written==1);
    CHECK(now_us<=limit&&control_write_calls==control_before&&rx_channel->enabled&&pa_level);
    register_delay_us=0;CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    CHECK(board_audio_measure_open(&audio,30,now_us+12000000)==ESP_OK);
    now_us+=12000000;reads_before=read_calls;
    CHECK(board_audio_measure_read(audio,capture,1,&received)==ESP_ERR_TIMEOUT&&received==0&&read_calls==reads_before);
    CHECK(!rx_channel->enabled&&!channel->enabled&&!pa_level);
    CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    CHECK(board_audio_measure_open(&audio,0,now_us+12000000)==ESP_OK);
    CHECK(board_audio_flush(audio)==ESP_ERR_NOT_SUPPORTED&&!rx_channel->enabled&&!pa_level);
    CHECK(board_audio_close(&audio)==ESP_OK&&live==0);
    measuring=false;
#else
    unsigned writes_before=write_calls;
    CHECK(board_audio_measure_open(&audio,0,now_us+12000000)==ESP_ERR_NOT_SUPPORTED&&!audio&&live==0);
    CHECK(!read_calls&&write_calls==writes_before&&gain_calls==0&&!rx_channel);
#endif
    failure=FAIL_TX_PAIR;
    CHECK(board_audio_open(&audio)==ESP_ERR_INVALID_STATE&&!audio&&live==0);
    failure=FAIL_NONE;
    CHECK(board_audio_open(&audio)==ESP_OK);
    CHECK(board_audio_measure_read(audio,pcm,1,&received)==ESP_ERR_NOT_SUPPORTED&&received==0&&!rx_channel);
    fail_release=true;
    CHECK(board_audio_close(&audio)==ESP_FAIL&&!audio&&!pa_level&&live==0);
    fail_release=false;
    CHECK(board_audio_open(&audio)==ESP_ERR_INVALID_STATE&&!audio&&live==0);
    printf("board audio checks: %d\n",checks);
    return 0;
}
