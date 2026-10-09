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
       FAIL_PARTIAL, FAIL_SHORT, FAIL_PA };
static int failure, pa_level, live, checks, muted, hardware_volume;
static unsigned write_calls, preload_calls, delay_ticks, mute_before_open;
static int64_t now_us;
static int64_t transfer_us=10000;
static bsp_board_variant_t board_variant=BSP_BOARD_VARIANT_V1_2;
static bool fail_release;
static uint8_t accepted[32768];
static size_t accepted_bytes;
static uint32_t waits[32];
static esp_codec_dev_sample_info_t requested;
static int open_references, enable_references;
struct test_i2s_channel { i2s_chan_config_t channels; i2s_std_config_t format; bool enabled, initialized; uint8_t dma[720]; };
static i2s_chan_handle_t channel;
typedef struct { audio_codec_ctrl_if_t base; } fake_ctrl_t;
typedef struct { audio_codec_data_if_t base; } fake_data_t;
typedef struct { audio_codec_if_t base; const audio_codec_ctrl_if_t *ctrl; bool enabled; } fake_codec_t;
typedef struct { esp_codec_dev_cfg_t cfg; bool opened; int volume; bool mute; } fake_device_t;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
static void *allocate(size_t size) { void *p=calloc(1,size); assert(p); ++live; return p; }
static void release(void *p) { if(p){--live;free(p);} }

esp_err_t bsp_board_variant_get(bsp_board_variant_t *v) { *v=board_variant; return failure==FAIL_BOARD?ESP_FAIL:ESP_OK; }
esp_err_t bsp_power_set_vcc_3v3(bool on) { CHECK(on); return failure==FAIL_RAIL?ESP_FAIL:ESP_OK; }
esp_err_t bsp_power_set_codec_3v3(bool on) { CHECK(on); return ESP_OK; }
esp_err_t bsp_i2c_init(void) { return failure==FAIL_I2C?ESP_FAIL:ESP_OK; }
void *bsp_i2c_get_handle(void) { return (void*)1; }
void vTaskDelay(unsigned ticks) { delay_ticks+=ticks; now_us+=ticks*1000; }
int64_t esp_timer_get_time(void) { return now_us; }
esp_err_t gpio_config(const gpio_config_t *c) { CHECK(c->pin_bit_mask==(UINT64_C(1)<<45)); CHECK(c->mode==GPIO_MODE_OUTPUT); return failure==FAIL_GPIO?ESP_FAIL:ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level) { CHECK(pin==45); if(level&&failure==FAIL_PA)return ESP_FAIL;pa_level=level;return ESP_OK; }
esp_err_t i2s_new_channel(const i2s_chan_config_t *c,i2s_chan_handle_t *tx,i2s_chan_handle_t *rx)
{
    CHECK(tx&&rx==NULL); CHECK(c->id==0&&c->role==I2S_ROLE_MASTER);
    CHECK(c->auto_clear&&c->dma_desc_num==3&&c->dma_frame_num==120);
    if(failure==FAIL_CHANNEL)return ESP_ERR_NO_MEM;
    *tx=channel=allocate(sizeof(**tx));channel->channels=*c;return ESP_OK;
}
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h,const i2s_std_config_t *c)
{
    CHECK(h==channel); CHECK(c->clk_cfg.sample_rate_hz==24000&&c->clk_cfg.mclk_multiple==256);
    CHECK(c->slot_cfg.data_bit_width==16&&c->slot_cfg.slot_mode==I2S_SLOT_MODE_MONO&&c->slot_cfg.slot_mask==I2S_STD_SLOT_LEFT);
    CHECK(c->gpio_cfg.mclk==54&&c->gpio_cfg.bclk==37&&c->gpio_cfg.ws==49&&c->gpio_cfg.dout==52&&c->gpio_cfg.din==-1);
    if(failure==FAIL_INIT)return ESP_FAIL;
    h->format=*c;h->initialized=true;return ESP_OK;
}
esp_err_t i2s_channel_get_info(i2s_chan_handle_t h,i2s_chan_info_t *out)
{
    if(failure==FAIL_INFO)return ESP_FAIL;
    *out=(i2s_chan_info_t){.dir=I2S_DIR_TX,.mode=I2S_COMM_MODE_STD,.is_enabled=h->enabled,.mode_cfg=&h->format,.total_dma_buf_size=h->initialized?720:0};return ESP_OK;
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t h) { if(failure==FAIL_ENABLE)return ESP_FAIL;CHECK(!h->enabled);h->enabled=true;return ESP_OK; }
esp_err_t i2s_channel_disable(i2s_chan_handle_t h) { if(!h->enabled)return ESP_ERR_INVALID_STATE;h->enabled=false;return ESP_OK; }
esp_err_t i2s_del_channel(i2s_chan_handle_t h) { CHECK(!h->enabled);release(h);channel=NULL;return ESP_OK; }
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t h,const void *data,size_t size,size_t *written)
{
    CHECK(!pa_level&&!h->enabled); CHECK(size==720); ++preload_calls;
    if(failure==FAIL_PRELOAD){*written=0;return ESP_FAIL;}
    for(size_t i=0;i<size;++i)CHECK(((const uint8_t*)data)[i]==0);
    memcpy(h->dma,data,size);*written=size;return ESP_OK;
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
static bool opened_ctrl(const audio_codec_ctrl_if_t *h) { (void)h;return true; }
static int read_reg(const audio_codec_ctrl_if_t *h,int reg,int bytes,void *data,int len) { (void)h;(void)reg;(void)bytes;memset(data,0,len);return ESP_OK; }
static int write_reg(const audio_codec_ctrl_if_t *h,int reg,int bytes,void *data,int len)
{
    (void)h;(void)bytes;(void)len;
    if((reg==0x31&&failure==FAIL_MUTE)||(reg==0x32&&failure==FAIL_VOLUME)||
       (reg==0x00&&failure==FAIL_CODEC_START))return ESP_CODEC_DEV_WRITE_FAIL;
    if(reg==0x31)muted=*(uint8_t*)data;
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
    CHECK(c->tx_handle==channel&&c->rx_handle==NULL&&c->port==0);
    if(failure==FAIL_DATA)return NULL;
    fake_data_t *f=allocate(sizeof(*f));f->base=(audio_codec_data_if_t){.is_open=opened_data,.enable=enable_data,.set_fmt=set_fmt};return &f->base;
}
static bool opened_codec(const audio_codec_if_t *h) { (void)h;return true; }
static int codec_mute(const audio_codec_if_t *h,bool mute) { const fake_codec_t *c=(const fake_codec_t*)h;uint8_t v=mute;return c->ctrl->write_reg(c->ctrl,0x31,1,&v,1); }
static int codec_volume(const audio_codec_if_t *h,float db) { const fake_codec_t *c=(const fake_codec_t*)h;uint8_t v=(uint8_t)(db+100);return c->ctrl->write_reg(c->ctrl,0x32,1,&v,1); }
static int codec_enable(const audio_codec_if_t *h,bool on)
{
    fake_codec_t *c=(fake_codec_t*)h;
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
    CHECK(!pa_level);CHECK(c->pa_pin==-1&&c->codec_mode==ESP_CODEC_DEV_WORK_MODE_DAC&&!c->master_mode&&c->use_mclk);
    CHECK(c->ctrl_if&&c->hw_gain.pa_voltage==5&&c->hw_gain.codec_dac_voltage==3.3f);
    if(failure==FAIL_CODEC)return NULL;
    fake_codec_t *f=allocate(sizeof(*f));++open_references;f->ctrl=c->ctrl_if;
    f->base=(audio_codec_if_t){.is_open=opened_codec,.mute=codec_mute,.set_vol=codec_volume,.enable=codec_enable};return &f->base;
}
esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *c)
{
    CHECK(c->dev_type==ESP_CODEC_DEV_TYPE_OUT);if(failure==FAIL_DEVICE)return NULL;
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
    fake_device_t *d=handle;CHECK(!pa_level);d->opened=true;
    (void)d->cfg.data_if->set_fmt(d->cfg.data_if,ESP_CODEC_DEV_TYPE_OUT,fs);
    (void)d->cfg.data_if->enable(d->cfg.data_if,ESP_CODEC_DEV_TYPE_OUT,true);
    if(d->cfg.codec_if->enable(d->cfg.codec_if,true)!=ESP_OK)return ESP_FAIL;
    (void)esp_codec_dev_set_out_mute(d,d->mute);(void)esp_codec_dev_set_out_vol(d,d->volume);
    return ESP_OK; /* Mirrors the real unchecked data-interface returns. */
}
int esp_codec_dev_write(esp_codec_dev_handle_t handle,void *data,int size) { fake_device_t *d=handle;return d->cfg.data_if->write(d->cfg.data_if,data,size); }
int esp_codec_dev_close(esp_codec_dev_handle_t handle) { fake_device_t *d=handle;if(d->opened){d->cfg.codec_if->enable(d->cfg.codec_if,false);d->cfg.data_if->enable(d->cfg.data_if,ESP_CODEC_DEV_TYPE_OUT,false);d->opened=false;}return ESP_OK; }
void esp_codec_dev_delete(esp_codec_dev_handle_t h) { esp_codec_dev_close(h);release(h); }
int audio_codec_delete_codec_if(const audio_codec_if_t *h) { --open_references;release((void*)h);return ESP_OK; }
int audio_codec_delete_data_if(const audio_codec_data_if_t *h) { release((void*)h);return ESP_OK; }
int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *h) { release((void*)h);return fail_release?ESP_FAIL:ESP_OK; }

int main(void)
{
    board_audio_t *audio=NULL;
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
    CHECK(board_audio_flush(audio)==ESP_OK&&!pa_level&&muted);
    CHECK(preload_calls==2);for(unsigned i=0;i<720;++i)CHECK(channel->dma[i]==0);
    memset(pcm,0,sizeof(pcm));CHECK(board_audio_write(audio,pcm,240,&written)==ESP_OK&&!pa_level&&muted);
    CHECK(board_audio_get_stats(audio,&stats)==ESP_OK&&stats.nonzero_frames==240&&!stats.pa_enabled&&stats.pa_activations==1);
    CHECK(board_audio_write(audio,pcm,481,&written)==ESP_ERR_INVALID_ARG&&written==0);
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
    CHECK(board_audio_open(&audio)==ESP_OK);fail_release=true;
    CHECK(board_audio_close(&audio)==ESP_FAIL&&!audio&&!pa_level&&live==0);
    fail_release=false;
    CHECK(board_audio_open(&audio)==ESP_ERR_INVALID_STATE&&!audio&&live==0);
    printf("board audio checks: %d\n",checks);
    return 0;
}
