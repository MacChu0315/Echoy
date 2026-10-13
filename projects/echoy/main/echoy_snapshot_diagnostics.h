#pragma once
#include "echoy_ui.h"
#include "esp_err.h"

#define ECHOY_SNAPSHOT_SERVICE_ID 0xeca1u
#define ECHOY_SNAPSHOT_CHUNK_BYTES 512u
#define ECHOY_SNAPSHOT_METADATA_BYTES 1024u
#define ECHOY_TOUCH_SNAPSHOT_BYTES 3500u

typedef struct echoy_snapshot_diagnostics *echoy_snapshot_diagnostics_handle_t;

/* Read-only application RPC, independent of the optional reference-tone RPC.
 * Method 1: empty payload -> frozen metadata JSON and a new hex u64 token.
 * Method 2: little-endian u64 token, u32 offset, u16 length -> raw ECHP bytes.
 * Method 3: empty payload -> read-only touch summary and latest <=16 pressed
 * observer edges, with explicit omission counts; does not replace plan token.
 * A new successful capture invalidates the previous token. Max chunk is 512.
 * Reads use only platform.read_control's coherent cache, never band/hardware.
 * The platform context must outlive this registration. Large storage is PSRAM.
 * On startup failure, *out can remain non-NULL if RPC rollback was busy; retain
 * the context and retry stop. A non-NULL handle must never be freed directly.
 * The application lifecycle owner serializes start/stop calls. */
esp_err_t echoy_snapshot_diagnostics_start(const echoy_platform_t *platform,
                                          echoy_snapshot_diagnostics_handle_t *out);
/* Call outside an RPC callback. Busy/failed unregister leaves *handle alive
 * and disabled for retry. Only success clears the handle and releases storage. */
esp_err_t echoy_snapshot_diagnostics_stop(echoy_snapshot_diagnostics_handle_t *handle);
