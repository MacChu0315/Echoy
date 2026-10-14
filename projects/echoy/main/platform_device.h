// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "board_audio.h"
#include "board_link.h"
#include "echoy_ui.h"
#include "esp_err.h"
#include "echoy_output_measurement.h"

typedef struct echoy_device *echoy_device_handle_t;
typedef enum {
    ECHOY_PROBE_IDLE,
    ECHOY_PROBE_QUEUED,
    ECHOY_PROBE_PLAYING,
    ECHOY_PROBE_DONE,
    ECHOY_PROBE_CANCELLED,
    ECHOY_PROBE_FAILED
} echoy_probe_phase_t;
typedef struct {
    echoy_probe_phase_t phase;
    esp_err_t error;
    unsigned frames_accepted, volume_percent;
    unsigned generation, requested_generation;
    echoy_condition_t condition;
    board_audio_probe_t before, after;
    board_audio_stats_t output;
} echoy_device_diagnostics_t;

/* Only the diagnostic build exposes these through private RPC. Cached read has
 * no hardware access. Start pauses music; one-second test never resumes it. */
esp_err_t echoy_device_probe_request(echoy_device_handle_t device, bool start);
esp_err_t echoy_device_probe_get(echoy_device_handle_t device, echoy_device_diagnostics_t *out);
/* Diagnostic-only, explicit finite admission. At most two accepted starts per
 * boot; a host authorization ledger must retain the budget across reboots.
 * Cancel never starts I/O. All hardware work remains on the audio owner. */
esp_err_t echoy_device_measure_request(echoy_device_handle_t device, bool start,
                                       unsigned input_gain_db,
                                       echoy_output_measurement_status_t *out);
esp_err_t echoy_device_measure_get(echoy_device_handle_t device,
                                   echoy_output_measurement_status_t *out);
/* Copies the completed current run only; no hardware access or borrowed PCM. */
esp_err_t echoy_device_measure_result(echoy_device_handle_t device, unsigned run_id,
                                      echoy_output_measurement_result_t *out);
/* Link-worker callbacks: cached reads and copied latest-state admission only. */
bool echoy_device_link_read(void *device, echoy_link_view_t *out);
bool echoy_device_link_receive(void *device, const board_link_update_t *update);

/* Firmware lifecycle only. BSP power/I2C must already be initialized. GSP
 * callbacks receive only nonblocking queue admission and a cached loading view.
 * Workers remain gated until activate: finish startup flash writes first. */
esp_err_t echoy_device_start(echoy_device_handle_t *out, echoy_platform_t *platform);
/* Lifecycle owner only, after UI creation and successful esp_iris_mark_healthy.
 * Starts silent audio once; never call from a render callback. */
esp_err_t echoy_device_activate(echoy_device_handle_t device);
/* After detaching all UI callbacks, outside any GSP task. Preparation may need
 * to finish before shutdown; timeout retains the handle for a later attempt. */
esp_err_t echoy_device_stop(echoy_device_handle_t *device, uint32_t timeout_ms);
