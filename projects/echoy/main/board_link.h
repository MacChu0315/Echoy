#pragma once

#include "echoy_coop.h"
#include "esp_err.h"

typedef echoy_link_update_t board_link_update_t;

/* Both callbacks run on the link worker. They must copy their inputs/outputs;
 * returning false requests a later retry. Neither callback may block audio. */
typedef bool (*board_link_read_fn)(void *ctx, echoy_link_view_t *out);
typedef bool (*board_link_receive_fn)(void *ctx, const board_link_update_t *update);

/* Process-lifetime singleton. Wi-Fi/NVS/RF initialization is synchronous, so
 * call before activating audio. Shared Flash-backed horizontal thresholds need
 * no startup baseline capture, including when already joined. Sensor failures
 * leave the link disconnected; valid filtered/debounced contact is required. */
esp_err_t board_link_start(board_link_read_fn read, board_link_receive_fn receive, void *ctx);
/* Stop callbacks before releasing ctx. A timeout/error retains the singleton
 * for a later stop retry; ctx must remain alive until this returns ESP_OK. */
esp_err_t board_link_stop(uint32_t timeout_ms);
