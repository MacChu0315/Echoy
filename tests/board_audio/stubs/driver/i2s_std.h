#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
typedef struct test_i2s_channel *i2s_chan_handle_t;
#define I2S_NUM_0 0
#define I2S_ROLE_MASTER 1
#define I2S_SLOT_MODE_MONO 1
#define I2S_SLOT_MODE_STEREO 2
#define I2S_STD_SLOT_LEFT 1
#define I2S_STD_SLOT_BOTH 3
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_BIT_WIDTH_16BIT 16
#define I2S_MCLK_MULTIPLE_256 256
#define I2S_GPIO_UNUSED -1
#define I2S_DIR_TX 1
#define I2S_COMM_MODE_STD 1
typedef struct { int id, role; uint32_t dma_desc_num, dma_frame_num; bool auto_clear; } i2s_chan_config_t;
typedef struct { uint32_t sample_rate_hz; int mclk_multiple, clk_src; } i2s_std_clk_config_t;
typedef struct { int data_bit_width, slot_bit_width, slot_mode, slot_mask, ws_width; bool ws_pol, bit_shift; } i2s_std_slot_config_t;
typedef struct { gpio_num_t mclk, bclk, ws, dout, din; } i2s_std_gpio_config_t;
typedef struct { i2s_std_clk_config_t clk_cfg; i2s_std_slot_config_t slot_cfg; i2s_std_gpio_config_t gpio_cfg; } i2s_std_config_t;
typedef struct { int dir, mode; bool is_enabled; i2s_chan_handle_t pair_chan; uint32_t total_dma_buf_size; const void *mode_cfg; } i2s_chan_info_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(port,kind) {.id=(port),.role=(kind),.dma_desc_num=6,.dma_frame_num=240}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) {.sample_rate_hz=(rate),.mclk_multiple=256}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits,channels) {.data_bit_width=(bits),.slot_mode=(channels),.slot_mask=I2S_STD_SLOT_BOTH,.ws_width=(bits),.bit_shift=true}
esp_err_t i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *, i2s_chan_handle_t *);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t *);
esp_err_t i2s_channel_get_info(i2s_chan_handle_t,i2s_chan_info_t *);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_del_channel(i2s_chan_handle_t);
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t,const void *,size_t,size_t *);
esp_err_t i2s_channel_write(i2s_chan_handle_t,const void *,size_t,size_t *,uint32_t);
