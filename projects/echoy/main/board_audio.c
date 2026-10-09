// SPDX-License-Identifier: Apache-2.0
#include "board_audio.h"
#include "bsp/esp_mosaico.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct { audio_codec_ctrl_if_t base; board_audio_t *owner; } control_proxy_t;
typedef struct { audio_codec_data_if_t base; board_audio_t *owner; } data_proxy_t;

struct board_audio {
    i2s_chan_handle_t tx;
    const audio_codec_ctrl_if_t *control;
    const audio_codec_data_if_t *data;
    const audio_codec_if_t *codec;
    esp_codec_dev_handle_t device;
    control_proxy_t control_proxy;
    data_proxy_t data_proxy;
    esp_err_t interface_error;
    bool pa_configured, pa_on, tx_enabled, faulted;
    size_t written_bytes;
    int64_t write_deadline;
    board_audio_stats_t stats;
    int16_t pcm[BOARD_AUDIO_MAX_FRAMES];
};

/* These are application ownership guards, not copies of BSP private handles.
 * A failed resource destructor cannot be retried through a freed SDK object. */
static board_audio_t *owner;
static bool release_failed;

static int remember(board_audio_t *audio, int error)
{
    if (audio->interface_error == ESP_OK && error != ESP_OK) audio->interface_error = error;
    return error;
}

static esp_err_t result(board_audio_t *audio, int error)
{
    return audio->interface_error != ESP_OK ? audio->interface_error : error;
}

/* The pinned facade discards several codec/data errors. ES8311 also acquires
 * an enable reference before I2C but cannot release it after a failed start.
 * This public control proxy latches the FIRST transport failure and suppresses
 * further I2C for that operation. It lets the driver's bookkeeping complete;
 * result() then reports the actual failure to OUR caller, while PA stays off.
 * Cleanup can consequently release both public interfaces and codec references.
 * No transport error is converted into application success. */
static bool control_opened(const audio_codec_ctrl_if_t *interface)
{
    board_audio_t *audio = ((const control_proxy_t *)interface)->owner;
    return audio->control->is_open(audio->control);
}

static int control_read(const audio_codec_ctrl_if_t *interface, int reg, int reg_len,
                        void *data, int data_len)
{
    board_audio_t *audio = ((const control_proxy_t *)interface)->owner;
    if (data && data_len > 0) memset(data, 0, (size_t)data_len);
    if (audio->interface_error == ESP_OK)
        remember(audio, audio->control->read_reg(audio->control, reg, reg_len, data, data_len));
    return ESP_OK;
}

static int control_write(const audio_codec_ctrl_if_t *interface, int reg, int reg_len,
                         void *data, int data_len)
{
    board_audio_t *audio = ((const control_proxy_t *)interface)->owner;
    if (audio->interface_error == ESP_OK)
        remember(audio, audio->control->write_reg(audio->control, reg, reg_len, data, data_len));
    return ESP_OK;
}

static int control_info(const audio_codec_ctrl_if_t *interface, audio_codec_ctrl_info_t *info)
{
    board_audio_t *audio = ((const control_proxy_t *)interface)->owner;
    /* Identity metadata must remain available for the codec reference-count
     * cleanup even when an earlier I2C register operation failed. */
    return remember(audio, audio->control->get_info(audio->control, info));
}

static bool data_opened(const audio_codec_data_if_t *interface)
{
    board_audio_t *audio = ((const data_proxy_t *)interface)->owner;
    return audio->data->is_open(audio->data);
}

static int data_enable(const audio_codec_data_if_t *interface, esp_codec_dev_type_t type, bool enabled)
{
    board_audio_t *audio = ((const data_proxy_t *)interface)->owner;
    if (type != ESP_CODEC_DEV_TYPE_OUT) return remember(audio, ESP_ERR_NOT_SUPPORTED);
    if (enabled && audio->interface_error != ESP_OK) return audio->interface_error;
    /* This single owner knows whether its previous enable succeeded. Avoid a
     * second disable during esp_codec_dev_close/delete, which IDF rejects. */
    if (audio->tx_enabled == enabled) return ESP_OK;
    int error = audio->data->enable(audio->data, type, enabled);
    if (error == ESP_OK) audio->tx_enabled = enabled;
    return remember(audio, error);
}

static int data_format(const audio_codec_data_if_t *interface, esp_codec_dev_type_t type,
                       esp_codec_dev_sample_info_t *format)
{
    board_audio_t *audio = ((const data_proxy_t *)interface)->owner;
    return remember(audio, audio->data->set_fmt(audio->data, type, format));
}

static int data_write(const audio_codec_data_if_t *interface, uint8_t *data, int size)
{
    board_audio_t *audio = ((const data_proxy_t *)interface)->owner;
    if (size <= 0 || size % (int)sizeof(int16_t)) return ESP_ERR_INVALID_SIZE;
    size_t remaining = (size_t)size;
    while (remaining) {
        int64_t left = audio->write_deadline - esp_timer_get_time();
        if (left <= 0) return ESP_ERR_TIMEOUT;
        uint32_t timeout_ms = (uint32_t)((left + 999) / 1000);
        size_t chunk = BOARD_AUDIO_DMA_FRAMES * sizeof(int16_t);
        if (chunk > remaining) chunk = remaining;
        size_t written = 0;
        /* IDF applies timeout per DMA wait. At most one descriptor per call
         * plus an overall deadline prevents a large block multiplying it. */
        esp_err_t error = i2s_channel_write(audio->tx, data, chunk, &written, timeout_ms);
        if (written > chunk) return ESP_FAIL;
        audio->written_bytes += written;
        if (error != ESP_OK) return error;
        if (written != chunk) return ESP_FAIL;
        data += written;
        remaining -= written;
    }
    return ESP_OK;
}

static esp_err_t amplifier_off(board_audio_t *audio)
{
    audio->pa_on = false;
    return audio->pa_configured ? gpio_set_level(BSP_AUDIO_PA_CTRL, 0) : ESP_OK;
}

static esp_err_t fault(board_audio_t *audio, esp_err_t error)
{
    /* Do not wait for I2C on a streaming failure: PA is independent of the
     * codec, then stopping TX prevents queued sound from reappearing. */
    audio->faulted = true;
    esp_err_t muted = amplifier_off(audio);
    int stopped = data_enable(&audio->data_proxy.base, ESP_CODEC_DEV_TYPE_OUT, false);
    if (error == ESP_OK) error = muted != ESP_OK ? muted : stopped;
    audio->stats.last_error = error;
    return error;
}

static void cleanup_error(esp_err_t *first, esp_err_t error, bool destructor)
{
    if (*first == ESP_OK && error != ESP_OK) *first = error;
    if (destructor && error != ESP_OK) release_failed = true;
}

static esp_err_t destroy(board_audio_t *audio)
{
    esp_err_t error = amplifier_off(audio);
    audio->interface_error = ESP_OK;
    if (audio->device) {
        cleanup_error(&error, esp_codec_dev_set_out_mute(audio->device, true), false);
        cleanup_error(&error, esp_codec_dev_close(audio->device), false);
        esp_codec_dev_delete(audio->device);
    }
    if (audio->codec) cleanup_error(&error, audio_codec_delete_codec_if(audio->codec), true);
    if (audio->data) cleanup_error(&error, audio_codec_delete_data_if(audio->data), true);
    if (audio->control) cleanup_error(&error, audio_codec_delete_ctrl_if(audio->control), true);
    if (audio->tx) {
        if (audio->tx_enabled) cleanup_error(&error, i2s_channel_disable(audio->tx), false);
        cleanup_error(&error, i2s_del_channel(audio->tx), true);
    }
    cleanup_error(&error, audio->interface_error, false);
    owner = NULL;
    free(audio);
    return error;
}

static esp_err_t clear_dma(board_audio_t *audio)
{
    int error = data_enable(&audio->data_proxy.base, ESP_CODEC_DEV_TYPE_OUT, false);
    if (error != ESP_OK) return error;
    /* In the pinned IDF, disable resets the DMA cursor; preload starts at
     * descriptor zero. Overwrite ALL descriptors, not just the pending queue. */
    const int16_t silence[BOARD_AUDIO_DMA_FRAMES * BOARD_AUDIO_DMA_DESCRIPTORS] = {0};
    size_t loaded = 0;
    error = i2s_channel_preload_data(audio->tx, silence, sizeof(silence), &loaded);
    if (error != ESP_OK) return error;
    if (loaded != sizeof(silence)) return ESP_FAIL;
    return data_enable(&audio->data_proxy.base, ESP_CODEC_DEV_TYPE_OUT, true);
}

static esp_err_t verify_format(board_audio_t *audio)
{
    i2s_chan_info_t info = {0};
    esp_err_t error = i2s_channel_get_info(audio->tx, &info);
    if (error != ESP_OK) return error;
    if (info.dir != I2S_DIR_TX || info.mode != I2S_COMM_MODE_STD || info.pair_chan ||
        !info.mode_cfg || !info.is_enabled) return ESP_ERR_INVALID_STATE;
    const i2s_std_config_t *format = info.mode_cfg; /* Public mode_cfg contract. */
    if (format->clk_cfg.sample_rate_hz != BOARD_AUDIO_SAMPLE_RATE ||
        format->clk_cfg.mclk_multiple != I2S_MCLK_MULTIPLE_256 ||
        format->slot_cfg.data_bit_width != I2S_DATA_BIT_WIDTH_16BIT ||
        format->slot_cfg.slot_bit_width != I2S_SLOT_BIT_WIDTH_16BIT ||
        format->slot_cfg.slot_mode != I2S_SLOT_MODE_MONO ||
        format->slot_cfg.slot_mask != I2S_STD_SLOT_LEFT ||
        info.total_dma_buf_size != BOARD_AUDIO_DMA_FRAMES * BOARD_AUDIO_DMA_DESCRIPTORS * sizeof(int16_t))
        return ESP_ERR_NOT_SUPPORTED;
    audio->stats.queue_capacity_frames = info.total_dma_buf_size / sizeof(int16_t);
    return ESP_OK;
}

esp_err_t board_audio_open(board_audio_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (owner || release_failed) return ESP_ERR_INVALID_STATE;
    board_audio_t *audio = calloc(1, sizeof(*audio));
    if (!audio) return ESP_ERR_NO_MEM;
    owner = audio;
    esp_err_t error;
    bsp_board_variant_t variant;
    /* BSP owns revision detection: GPIO56 is codec power on v1.0 but I2C SDA
     * on v1.2. Never drive that rail directly from application code. */
    if ((error = bsp_board_variant_get(&variant)) != ESP_OK) goto failed;
    if ((error = gpio_set_level(BSP_AUDIO_PA_CTRL, 0)) != ESP_OK) goto failed;
    const gpio_config_t pa = {.pin_bit_mask = UINT64_C(1) << BSP_AUDIO_PA_CTRL,
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    if ((error = gpio_config(&pa)) != ESP_OK) goto failed;
    audio->pa_configured = true;
    if ((error = bsp_power_set_vcc_3v3(true)) != ESP_OK) goto failed;
    if ((error = bsp_power_set_codec_3v3(true)) != ESP_OK) goto failed;
    vTaskDelay(pdMS_TO_TICKS(10));
    if ((error = bsp_i2c_init()) != ESP_OK) goto failed;

    i2s_chan_config_t channels = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channels.dma_desc_num = BOARD_AUDIO_DMA_DESCRIPTORS;
    channels.dma_frame_num = BOARD_AUDIO_DMA_FRAMES;
    channels.auto_clear = true;
    if ((error = i2s_new_channel(&channels, &audio->tx, NULL)) != ESP_OK) goto failed;
    i2s_std_config_t format = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = BSP_AUDIO_I2S_MCLK, .bclk = BSP_AUDIO_I2S_SCLK,
            .ws = BSP_AUDIO_I2S_LRCLK, .dout = BSP_AUDIO_I2S_SDOUT, .din = I2S_GPIO_UNUSED},
    };
    format.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    format.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    format.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    if ((error = i2s_channel_init_std_mode(audio->tx, &format)) != ESP_OK) goto failed;

    audio_codec_i2s_cfg_t data = {.port = I2S_NUM_0, .tx_handle = audio->tx};
    audio->data = audio_codec_new_i2s_data(&data);
    if (!audio->data) { error = ESP_FAIL; goto failed; }
    audio->data_proxy = (data_proxy_t){.owner = audio, .base = {
        .is_open = data_opened, .enable = data_enable, .set_fmt = data_format, .write = data_write}};
    audio_codec_i2c_cfg_t control = {.port = BSP_I2C_PORT, .addr = BSP_AUDIO_CODEC_I2C_ADDR,
                                    .bus_handle = bsp_i2c_get_handle()};
    audio->control = audio_codec_new_i2c_ctrl(&control);
    if (!audio->control) { error = ESP_FAIL; goto failed; }
    audio->control_proxy = (control_proxy_t){.owner = audio, .base = {
        .is_open = control_opened, .read_reg = control_read,
        .write_reg = control_write, .get_info = control_info}};
    es8311_codec_cfg_t codec = {.ctrl_if = &audio->control_proxy.base,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, .pa_pin = GPIO_NUM_NC,
        .use_mclk = true, .no_dac_ref = true, .mclk_div = I2S_MCLK_MULTIPLE_256,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f}};
    audio->codec = es8311_codec_new(&codec);
    if (!audio->codec) { error = result(audio, ESP_FAIL); goto failed; }
    if ((error = result(audio, ESP_OK)) != ESP_OK) goto failed;
    esp_codec_dev_cfg_t device = {.dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = audio->codec, .data_if = &audio->data_proxy.base};
    audio->device = esp_codec_dev_new(&device);
    if (!audio->device) { error = ESP_ERR_NO_MEM; goto failed; }
    error = result(audio, esp_codec_dev_set_out_mute(audio->device, true));
    if (error != ESP_OK) goto failed;
    /* The pinned constructor silently tolerates default-curve allocation
     * failure, which otherwise maps volume 40 to 0 dB. Install the same curve
     * through the checked public setter before enabling any output. */
    esp_codec_dev_vol_map_t volume_map[] = {{.vol = 0, .db_value = -50.0f},
                                          {.vol = 100, .db_value = 0.0f}};
    esp_codec_dev_vol_curve_t volume_curve = {.vol_map = volume_map, .count = 2};
    error = esp_codec_dev_set_vol_curve(audio->device, &volume_curve);
    if (error != ESP_OK) goto failed;
    error = result(audio, esp_codec_dev_set_out_vol(audio->device, BOARD_AUDIO_HARDWARE_VOLUME));
    if (error != ESP_OK) goto failed;
    esp_codec_dev_sample_info_t samples = {.sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .bits_per_sample = 16, .channel = 1, .mclk_multiple = I2S_MCLK_MULTIPLE_256};
    error = result(audio, esp_codec_dev_open(audio->device, &samples));
    if (error != ESP_OK) goto failed;
    if ((error = verify_format(audio)) != ESP_OK) goto failed;
    if ((error = clear_dma(audio)) != ESP_OK) goto failed;
    *out = audio;
    return ESP_OK;
failed:
    (void)destroy(audio); /* Original initialization error is the caller's cause. */
    return error;
}

esp_err_t board_audio_write(board_audio_t *audio, const int16_t *pcm, size_t frames,
                            size_t *written_frames)
{
    if (written_frames) *written_frames = 0;
    if (!audio || audio != owner || !written_frames || frames > BOARD_AUDIO_MAX_FRAMES ||
        (frames && !pcm)) return ESP_ERR_INVALID_ARG;
    if (audio->faulted) return ESP_ERR_INVALID_STATE;
    if (!frames) return ESP_OK;
    memcpy(audio->pcm, pcm, frames * sizeof(*pcm));
    audio->written_bytes = 0;
    audio->write_deadline = esp_timer_get_time() + BOARD_AUDIO_WRITE_TIMEOUT_MS * INT64_C(1000);
    ++audio->stats.write_calls;
    esp_err_t error = esp_codec_dev_write(audio->device, audio->pcm, (int)(frames * sizeof(*pcm)));
    *written_frames = audio->written_bytes / sizeof(*pcm);
    audio->stats.frames_written += *written_frames;
    if (*written_frames && *written_frames < frames) ++audio->stats.partial_writes;
    if (error == ESP_OK && audio->written_bytes != frames * sizeof(*pcm)) error = ESP_FAIL;
    if (error == ESP_OK && !audio->pa_on) {
        bool audible = false;
        for (size_t i = 0; i < frames; ++i) audible |= pcm[i] != 0;
        if (audible) {
            audio->interface_error = ESP_OK;
            error = result(audio, esp_codec_dev_set_out_mute(audio->device, false));
            if (error == ESP_OK) error = gpio_set_level(BSP_AUDIO_PA_CTRL, 1);
            if (error == ESP_OK) audio->pa_on = true;
        }
    }
    if (error != ESP_OK) {
        ++audio->stats.write_failures;
        if (error == ESP_ERR_TIMEOUT) ++audio->stats.write_timeouts;
        return fault(audio, error);
    }
    audio->stats.last_error = ESP_OK;
    return ESP_OK;
}

esp_err_t board_audio_flush(board_audio_t *audio)
{
    if (!audio || audio != owner) return ESP_ERR_INVALID_ARG;
    if (audio->faulted) return ESP_ERR_INVALID_STATE;
    esp_err_t error = amplifier_off(audio);
    audio->interface_error = ESP_OK;
    if (error == ESP_OK) error = result(audio, esp_codec_dev_set_out_mute(audio->device, true));
    if (error == ESP_OK) error = clear_dma(audio);
    if (error != ESP_OK) return fault(audio, error);
    ++audio->stats.flushes;
    audio->stats.last_error = ESP_OK;
    return ESP_OK;
}

esp_err_t board_audio_get_stats(const board_audio_t *audio, board_audio_stats_t *out)
{
    if (!audio || audio != owner || !out) return ESP_ERR_INVALID_ARG;
    *out = audio->stats;
    return ESP_OK;
}

esp_err_t board_audio_close(board_audio_t **audio)
{
    if (!audio) return ESP_ERR_INVALID_ARG;
    if (!*audio) return ESP_OK;
    if (*audio != owner) return ESP_ERR_INVALID_ARG;
    board_audio_t *closing = *audio;
    *audio = NULL;
    return destroy(closing);
}
