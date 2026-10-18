#pragma once

#include "echoy_coop.h"
#include "echoy_group.h"
#include "echoy_radio.h"
#include "esp_err.h"

typedef echoy_link_update_t board_link_update_t;

/* Both callbacks run on the link worker. They must copy their inputs/outputs;
 * returning false requests a later retry. Neither callback may block audio. */
typedef bool (*board_link_read_fn)(void *ctx, echoy_link_view_t *out);
typedef bool (*board_link_receive_fn)(void *ctx, const board_link_update_t *update);

/* Process-lifetime singleton. Wi-Fi/NVS/RF initialization is synchronous, so
 * call before activating audio. Shared read-only Flash thresholds need no
 * startup baseline capture, including joined startup. Wait for valid filtered
 * sampling; sensor failure leaves the link disconnected. */
esp_err_t board_link_start(board_link_read_fn read, board_link_receive_fn receive, void *ctx);
/* Stop callbacks before releasing ctx. A timeout/error retains the singleton
 * for a later stop retry; ctx must remain alive until this returns ESP_OK.
 * Pinned SDK teardown failures are latched until reboot: its subsequent no-op
 * success cannot prove RX cleanup, so those failures never release the context. */
esp_err_t board_link_stop(uint32_t timeout_ms);

/* Diagnostic-only latest sample, copied without waiting or reading hardware.
 * Axes are compensated BMM150 outputs, not ADC registers; index 0=left, 1=right.
 * Masks use the public BSP edge bits. sample_valid=false invalidates all axes.
 * False means disabled, inactive, not yet sampled, or briefly busy. */
typedef struct {
    echoy_group_timing_t timing;
    echoy_radio_phrase_timing_t phrase;
    uint32_t sequence;
    int64_t sampled_us;
    bool sample_valid, sensors_ready;
    int16_t raw[2][3];
    int32_t filtered[2][3];
    unsigned samples, calibration, classified, contacts, reported;
    uint64_t peers[2];
    uint32_t sessions[2];
    unsigned group_count, group_rank, group_phase;
    bool clock_mapped;
    uint32_t group_identity, attempt, radio_submit_failures, radio_queue_drops;
    uint64_t radio_graph;
    unsigned radio_known;
    bool radio_admitted, radio_fresh;
} board_link_diagnostics_t;
bool board_link_diagnostics_snapshot(board_link_diagnostics_t *out);
