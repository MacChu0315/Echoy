#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct board_health *board_health_handle_t;
typedef enum {
    BOARD_HEALTH_STARTING,
    BOARD_HEALTH_VALID,
    BOARD_HEALTH_ABSENT,
    BOARD_HEALTH_GAUGE_NOT_READY,
    BOARD_HEALTH_INVALID_SAMPLE,
    BOARD_HEALTH_INIT_ERROR,
    BOARD_HEALTH_READ_ERROR,
    BOARD_HEALTH_STALE,
    BOARD_HEALTH_STOPPING,
    BOARD_HEALTH_STOPPED
} board_health_reason_t;
typedef enum {
    BOARD_HEALTH_VARIANT_UNKNOWN = -1,
    BOARD_HEALTH_VARIANT_V1_0,
    BOARD_HEALTH_VARIANT_V1_2
} board_health_variant_t;

/* Standard BQ27220 command values, before narrowing or unit conversion. */
typedef struct {
    uint16_t battery_status, operation_status;
    uint16_t state_of_charge, voltage_mv, current_raw, temperature_dk;
} board_health_raw_t;

typedef struct {
    bool valid; /* All measurements below are fresh and validated. */
    bool present_known, present, stale;
    int percentage; /* -1 whenever !valid; never a synthetic 0 or 72. */
    int voltage_mv;
    int32_t current_ma;  /* Signed gauge current; INT32_MIN when invalid. */
    float temperature_c; /* NAN when invalid. */
    board_health_reason_t reason;
    board_health_variant_t board_variant;
    esp_err_t io_error;     /* Actual BSP/IDF return; ESP_OK for a data-validity failure. */
    uint8_t failed_command; /* Register on read failure, otherwise UINT8_MAX. */
    int64_t sampled_at_us, last_success_us; /* -1 before a measurement. */
    board_health_raw_t raw;                 /* Diagnostic values only; consult valid/reason. */
} board_health_snapshot_t;

typedef struct {
    uint32_t sample_period_ms; /* Default 2000; [100,60000]. */
    uint32_t stale_after_ms;   /* Default 10000; >= period, <= 3600000. */
    uint32_t i2c_timeout_ms;   /* Default 100; [1,1000], per transaction. */
} board_health_config_t;
#define BOARD_HEALTH_CONFIG_DEFAULT()                                                              \
    ((board_health_config_t){                                                                      \
        .sample_period_ms = 2000, .stale_after_ms = 10000, .i2c_timeout_ms = 100})

/* Creates a low-priority worker. Its ESP_OK means task creation, not a usable
 * battery. The worker alone calls bsp_battery_init(), which may configure the
 * BSP's fixed gauge RAM profile, then performs checked standard-command reads.
 * Initialization failure is retained; there is no automatic reconfiguration.
 * Uses the BSP's detected bus variant and never reconfigures shared I2C pins.
 * Only one service may be active because BSP gauge initialization is global.
 * Call only from the authorized firmware lifecycle, not from a GSP callback.
 */
esp_err_t board_health_start(const board_health_config_t *config, board_health_handle_t *out);

/* Cache copy and freshness check only: no I2C, waits or hardware initialization. */
esp_err_t board_health_get(board_health_handle_t health, board_health_snapshot_t *out);

/* Lifecycle owner only; must not race other start/stop or destruction callers.
 * Requests worker shutdown and waits at most timeout_ms (0..60000). On timeout or cleanup
 * failure the handle is retained for a later explicit stop attempt. The BSP
 * gauge and shared bus remain BSP-owned; only our checked-reader handle closes.
 * Never call this blocking lifecycle operation on the GSP rendering thread.
 */
esp_err_t board_health_stop(board_health_handle_t *health, uint32_t timeout_ms);

/* Pure host-testable conversion/validation. No BSP or RTOS calls. */
bool board_health_decode(const board_health_raw_t *raw, esp_err_t io_error, int64_t sampled_at_us,
                         board_health_snapshot_t *out);
bool board_health_with_age(const board_health_snapshot_t *sample, int64_t now_us,
                           int64_t stale_after_us, board_health_snapshot_t *out);
bool board_health_battery_empty(const board_health_snapshot_t *sample);
