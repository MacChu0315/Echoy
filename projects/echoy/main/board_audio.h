// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define BOARD_AUDIO_SAMPLE_RATE 24000u
#define BOARD_AUDIO_MAX_FRAMES 480u
#define BOARD_AUDIO_DMA_FRAMES 120u
#define BOARD_AUDIO_DMA_DESCRIPTORS 3u
#define BOARD_AUDIO_WRITE_TIMEOUT_MS 40u
#define BOARD_AUDIO_HARDWARE_VOLUME 90

typedef struct board_audio board_audio_t;
typedef struct {
    uint64_t frames_written, nonzero_frames;
    uint32_t write_calls, write_failures, write_timeouts, partial_writes, flushes;
    uint32_t queue_capacity_frames;
    uint32_t peak_pcm, last_write_us, max_write_us, pa_activations;
    bool pa_enabled;
    esp_err_t last_error;
} board_audio_stats_t;

/* ES8311 public registers: reset, clock, serial input, mute, volume, chip ID.
 * Read only while the owner has deliberately paused musical playback. */
#define BOARD_AUDIO_PROBE_REG_COUNT 7
typedef struct {
    uint8_t registers[BOARD_AUDIO_PROBE_REG_COUNT];
    unsigned valid_mask;
    esp_err_t error;
    int64_t sampled_at_us;
    bool pa_requested;
} board_audio_probe_t;
esp_err_t board_audio_readback(board_audio_t *audio, board_audio_probe_t *out);

/* All calls belong to one serialized audio task, outside the band/UI mutex.
 * Owns TX-only I2S and codec interfaces; borrows the BSP I2C bus/power rails.
 * Do not also call bsp_audio_init or bsp_audio_codec_speaker_init in this app.
 * Open leaves PA/DAC muted. The first accepted nonzero PCM block enables PA.
 * No samples are resampled or duplicated: signed 16-bit mono at 24 kHz. */
esp_err_t board_audio_open(board_audio_t **out);

/* At most MAX_FRAMES per call. written_frames reports only fully accepted
 * samples, including a partial write on error; never replay that prefix.
 * A write failure immediately disables PA and latches a fault until close/open.
 * The I2S wait is bounded; codec setup/mute uses its own bounded I2C operations. */
esp_err_t board_audio_write(board_audio_t *audio, const int16_t *pcm, size_t frames,
                            size_t *written_frames);

/* Disable PA first, stop TX, overwrite every DMA descriptor with silence and
 * restart muted. Discards queued sound without changing the sample rate.
 * Scheduler-owned/stale PCM must also be discarded by the caller. */
esp_err_t board_audio_flush(board_audio_t *audio);

/* Statistics describe accepted PCM and successful PA control, not a measured
 * analog waveform, DAC playhead or hardware underrun counter. Getter has no I/O. */
esp_err_t board_audio_get_stats(const board_audio_t *audio, board_audio_stats_t *out);

/* Mutes before cleanup, consumes *audio and sets it to NULL. Shared BSP rails
 * and I2C bus remain available to display/touch/Recovery. If an SDK resource
 * destructor fails, later open calls return INVALID_STATE until reboot;
 * private handles are never reused or reconstructed. Closing NULL is a no-op. */
esp_err_t board_audio_close(board_audio_t **audio);
