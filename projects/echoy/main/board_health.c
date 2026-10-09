#include "board_health.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* BQ27220 TRM standard commands and status bits, matching the pinned
 * espressif__bq27220 driver. Do not include its private register header. */
enum {
    BQ_TEMPERATURE = 0x06,
    BQ_VOLTAGE = 0x08,
    BQ_BATTERY_STATUS = 0x0a,
    BQ_CURRENT = 0x0c,
    BQ_SOC = 0x2c,
    BQ_OPERATION_STATUS = 0x3a,
    BQ_BATTPRES = 1u << 3,
    BQ_INITCOMP = 1u << 5,
    BQ_CFGUPDATE = 1u << 10
};

static void clear_values(board_health_snapshot_t *out)
{
    out->valid = false;
    out->percentage = -1;
    out->voltage_mv = -1;
    out->current_ma = INT32_MIN;
    out->temperature_c = NAN;
}

static board_health_snapshot_t unknown(void)
{
    board_health_snapshot_t result = {.reason = BOARD_HEALTH_STARTING,
                                      .board_variant = BOARD_HEALTH_VARIANT_UNKNOWN,
                                      .failed_command = UINT8_MAX,
                                      .sampled_at_us = -1,
                                      .last_success_us = -1};
    clear_values(&result);
    return result;
}

bool board_health_decode(const board_health_raw_t *raw, esp_err_t error, int64_t at,
                         board_health_snapshot_t *out)
{
    if (!out) {
        return false;
    }
    board_health_raw_t input = raw ? *raw : (board_health_raw_t){0};
    bool supplied = raw != NULL;
    *out = unknown();
    out->io_error = error;
    out->sampled_at_us = at;
    if (supplied) {
        out->raw = input;
    }
    if (error != ESP_OK) {
        out->reason = BOARD_HEALTH_READ_ERROR;
        return false;
    }
    if (!supplied || at < 0) {
        out->io_error = ESP_ERR_INVALID_ARG;
        out->reason = BOARD_HEALTH_INVALID_SAMPLE;
        return false;
    }
    raw = &input;
    if (raw->battery_status == UINT16_MAX) {
        out->reason = BOARD_HEALTH_INVALID_SAMPLE;
        return false;
    }
    out->present_known = true;
    out->present = (raw->battery_status & BQ_BATTPRES) != 0;
    if (!out->present) {
        out->reason = BOARD_HEALTH_ABSENT;
        return false;
    }
    if (raw->operation_status == UINT16_MAX) {
        out->reason = BOARD_HEALTH_INVALID_SAMPLE;
        return false;
    }
    if (!(raw->operation_status & BQ_INITCOMP) || (raw->operation_status & BQ_CFGUPDATE)) {
        out->reason = BOARD_HEALTH_GAUGE_NOT_READY;
        return false;
    }
    /* Preserve the 16-bit SOC: narrowing 0x0100 to uint8_t would fabricate 0%.
     * A present cell with no measurable voltage is not a trusted empty reading.
     * Zero kelvin and all-ones unsigned measurements are not sensor values.
     * Signed current 0xffff is legitimate -1 mA, not an error sentinel. */
    if (raw->state_of_charge > 100 || !raw->voltage_mv || raw->voltage_mv == UINT16_MAX ||
        !raw->temperature_dk || raw->temperature_dk == UINT16_MAX) {
        out->reason = BOARD_HEALTH_INVALID_SAMPLE;
        return false;
    }
    out->valid = true;
    out->reason = BOARD_HEALTH_VALID;
    out->percentage = raw->state_of_charge;
    out->voltage_mv = raw->voltage_mv;
    out->current_ma =
        raw->current_raw >= 0x8000u ? (int32_t)raw->current_raw - 65536 : raw->current_raw;
    out->temperature_c = raw->temperature_dk / 10.0f - 273.15f;
    out->last_success_us = at;
    return true;
}

bool board_health_with_age(const board_health_snapshot_t *sample, int64_t now, int64_t stale_after,
                           board_health_snapshot_t *out)
{
    if (!out) {
        return false;
    }
    if (!sample || stale_after <= 0) {
        *out = unknown();
        out->reason = BOARD_HEALTH_INVALID_SAMPLE;
        out->io_error = ESP_ERR_INVALID_ARG;
        return false;
    }
    *out = *sample;
    if (sample->sampled_at_us >= 0 &&
        (now < sample->sampled_at_us || now - sample->sampled_at_us > stale_after)) {
        clear_values(out);
        out->present_known = false;
        out->present = false;
        out->stale = true;
        out->reason = BOARD_HEALTH_STALE;
    }
    return out->valid;
}

bool board_health_battery_empty(const board_health_snapshot_t *sample)
{
    return sample && sample->valid && !sample->stale && sample->present_known && sample->present &&
           sample->percentage == 0;
}

#ifdef ESP_PLATFORM
#include "bsp/esp_mosaico.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>

struct board_health {
    board_health_config_t config;
    portMUX_TYPE lock;
    SemaphoreHandle_t wake, done;
    board_health_snapshot_t sample;
    i2c_master_dev_handle_t reader;
    bool stopping, finished, cleanup_only;
    atomic_bool task_exited;
    esp_err_t cleanup_error;
};

static const char *TAG = "echoy-health";
static atomic_bool service_owned;

static bool stopping(board_health_handle_t h)
{
    taskENTER_CRITICAL(&h->lock);
    bool result = h->stopping;
    taskEXIT_CRITICAL(&h->lock);
    return result;
}

static void publish(board_health_handle_t h, board_health_snapshot_t *sample)
{
    taskENTER_CRITICAL(&h->lock);
    board_health_snapshot_t previous = h->sample;
    sample->board_variant = previous.board_variant;
    if (!sample->valid) {
        sample->last_success_us = previous.last_success_us;
    }
    h->sample = *sample;
    taskEXIT_CRITICAL(&h->lock);
    if (sample->io_error != ESP_OK &&
        (sample->io_error != previous.io_error || sample->reason != previous.reason)) {
        ESP_LOGW(TAG, "Battery unknown: reason=%d error=%s command=0x%02x", (int)sample->reason,
                 esp_err_to_name(sample->io_error), (unsigned)sample->failed_command);
    }
}

static esp_err_t read_u16(board_health_handle_t h, uint8_t command, uint16_t *out,
                          uint8_t *failed_command)
{
    uint8_t bytes[2] = {0};
    esp_err_t error = i2c_master_transmit_receive(h->reader, &command, 1, bytes, sizeof(bytes),
                                                  (int)h->config.i2c_timeout_ms);
    if (error != ESP_OK) {
        *failed_command = command;
        return error;
    }
    *out = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return ESP_OK;
}

static esp_err_t read_raw(board_health_handle_t h, board_health_raw_t *raw, uint8_t *failed)
{
    memset(raw, 0xff, sizeof(*raw));
    *failed = UINT8_MAX;
    esp_err_t error = read_u16(h, BQ_BATTERY_STATUS, &raw->battery_status, failed);
    if (error != ESP_OK || !(raw->battery_status & BQ_BATTPRES)) {
        return error;
    }
    error = read_u16(h, BQ_OPERATION_STATUS, &raw->operation_status, failed);
    if (error != ESP_OK || !(raw->operation_status & BQ_INITCOMP) ||
        (raw->operation_status & BQ_CFGUPDATE)) {
        return error;
    }
    const uint8_t commands[] = {BQ_VOLTAGE, BQ_CURRENT, BQ_TEMPERATURE, BQ_SOC};
    uint16_t *outputs[] = {&raw->voltage_mv, &raw->current_raw, &raw->temperature_dk,
                           &raw->state_of_charge};
    for (unsigned i = 0; i < 4; ++i) {
        error = read_u16(h, commands[i], outputs[i], failed);
        if (error != ESP_OK) {
            return error;
        }
    }
    /* Recheck presence/readiness after the measurements: unplug/reset during
     * the read sequence must not produce a trusted 0% sample. */
    error = read_u16(h, BQ_BATTERY_STATUS, &raw->battery_status, failed);
    if (error != ESP_OK || !(raw->battery_status & BQ_BATTPRES)) {
        return error;
    }
    return read_u16(h, BQ_OPERATION_STATUS, &raw->operation_status, failed);
}

static esp_err_t initialize(board_health_handle_t h)
{
    bsp_board_variant_t variant;
    esp_err_t error = bsp_board_variant_get(&variant);
    if (error != ESP_OK) {
        return error;
    }
    if (variant != BSP_BOARD_VARIANT_V1_0 && variant != BSP_BOARD_VARIANT_V1_2) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    taskENTER_CRITICAL(&h->lock);
    h->sample.board_variant =
        variant == BSP_BOARD_VARIANT_V1_0 ? BOARD_HEALTH_VARIANT_V1_0 : BOARD_HEALTH_VARIANT_V1_2;
    taskEXIT_CRITICAL(&h->lock);
    /* This public BSP call verifies/configures the fixed 80 mAh RAM profile.
     * Do not repeat it on transient sample failures and do not run it on UI. */
    error = bsp_battery_init();
    if (error != ESP_OK || stopping(h)) {
        return error;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                        .device_address = BSP_BATTERY_I2C_ADDR,
                                        .scl_speed_hz = BSP_BATTERY_I2C_SPEED_HZ};
    /* Borrow the already configured BSP bus. The pinned BSP/BQ convenience
     * readers return zero on I2C errors; a separate checked reader preserves
     * real esp_err_t failures without depending on a vendor-private handle. */
    return i2c_master_bus_add_device(bus, &config, &h->reader);
}

static void worker(void *context)
{
    board_health_handle_t h = context;
    if (!h->cleanup_only) {
        esp_err_t error = initialize(h);
        if (error != ESP_OK) {
            board_health_snapshot_t sample = unknown();
            sample.reason = BOARD_HEALTH_INIT_ERROR;
            sample.io_error = error;
            publish(h, &sample);
        }
        while (!stopping(h)) {
            if (error == ESP_OK) {
                board_health_raw_t raw;
                uint8_t failed;
                esp_err_t read_error = read_raw(h, &raw, &failed);
                board_health_snapshot_t sample;
                board_health_decode(&raw, read_error, esp_timer_get_time(), &sample);
                sample.failed_command = failed;
                publish(h, &sample);
            }
            /* Init failure waits for lifecycle shutdown; successful init polls
             * at a bounded cadence, with no in-cycle retries or recalibration. */
            xSemaphoreTake(h->wake, error == ESP_OK ? pdMS_TO_TICKS(h->config.sample_period_ms)
                                                    : portMAX_DELAY);
        }
    }
    esp_err_t cleanup = h->reader ? i2c_master_bus_rm_device(h->reader) : ESP_OK;
    if (cleanup == ESP_OK) {
        h->reader = NULL;
    }
    taskENTER_CRITICAL(&h->lock);
    clear_values(&h->sample);
    h->sample.present_known = h->sample.present = false;
    h->sample.reason = cleanup == ESP_OK ? BOARD_HEALTH_STOPPED : BOARD_HEALTH_STOPPING;
    if (cleanup != ESP_OK) {
        h->sample.io_error = cleanup;
    }
    h->cleanup_error = cleanup;
    h->finished = true;
    taskEXIT_CRITICAL(&h->lock);
    xSemaphoreGive(h->done);
    /* FreeRTOS may still access the semaphore after unblocking its waiter.
     * Publish a second release/acquire handshake only after Give returns. */
    atomic_store_explicit(&h->task_exited, true, memory_order_release);
    /* No further access to h: the owner may now free it. */
    vTaskDelete(NULL);
}

esp_err_t board_health_start(const board_health_config_t *config, board_health_handle_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    board_health_config_t selected = config ? *config : BOARD_HEALTH_CONFIG_DEFAULT();
    if (selected.sample_period_ms < 100 || selected.sample_period_ms > 60000 ||
        selected.stale_after_ms < selected.sample_period_ms || selected.stale_after_ms > 3600000 ||
        selected.i2c_timeout_ms < 1 || selected.i2c_timeout_ms > 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    /* bsp_battery_init owns a process-wide, non-reference-counted singleton. */
    if (atomic_exchange_explicit(&service_owned, true, memory_order_acq_rel)) {
        return ESP_ERR_INVALID_STATE;
    }
    *out = NULL;
    board_health_handle_t h = calloc(1, sizeof(*h));
    if (!h) {
        atomic_store(&service_owned, false);
        return ESP_ERR_NO_MEM;
    }
    h->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    h->config = selected;
    h->sample = unknown();
    atomic_init(&h->task_exited, false);
    h->wake = xSemaphoreCreateBinary();
    h->done = xSemaphoreCreateBinary();
    if (!h->wake || !h->done) {
        if (h->wake) {
            vSemaphoreDelete(h->wake);
        }
        if (h->done) {
            vSemaphoreDelete(h->done);
        }
        free(h);
        atomic_store(&service_owned, false);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(worker, "echoy_health", 4096, h, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        vSemaphoreDelete(h->wake);
        vSemaphoreDelete(h->done);
        free(h);
        atomic_store(&service_owned, false);
        return ESP_ERR_NO_MEM;
    }
    *out = h;
    return ESP_OK;
}

esp_err_t board_health_get(board_health_handle_t h, board_health_snapshot_t *out)
{
    if (!h || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&h->lock);
    board_health_snapshot_t sample = h->sample;
    bool stop = h->stopping;
    taskEXIT_CRITICAL(&h->lock);
    board_health_with_age(&sample, esp_timer_get_time(), (int64_t)h->config.stale_after_ms * 1000,
                          out);
    if (stop) {
        clear_values(out);
        out->present_known = out->present = false;
        out->reason = BOARD_HEALTH_STOPPING;
    }
    return ESP_OK;
}

static TickType_t remaining_ticks(int64_t deadline)
{
    int64_t remaining = deadline - esp_timer_get_time();
    return remaining > 0 ? pdMS_TO_TICKS((uint32_t)(remaining / 1000)) : 0;
}

static esp_err_t await_exit(board_health_handle_t h, int64_t deadline)
{
    if (xSemaphoreTake(h->done, remaining_ticks(deadline)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    while (!atomic_load_explicit(&h->task_exited, memory_order_acquire)) {
        if (esp_timer_get_time() >= deadline) {
            /* Keep the completion observable by a later explicit stop. */
            xSemaphoreGive(h->done);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    return ESP_OK;
}

esp_err_t board_health_stop(board_health_handle_t *handle, uint32_t timeout_ms)
{
    if (!handle || timeout_ms > 60000) {
        return ESP_ERR_INVALID_ARG;
    }
    board_health_handle_t h = *handle;
    if (!h) {
        return ESP_OK;
    }
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    taskENTER_CRITICAL(&h->lock);
    bool retry_cleanup = h->finished && h->cleanup_error != ESP_OK;
    h->stopping = true;
    taskEXIT_CRITICAL(&h->lock);
    if (retry_cleanup) {
        if (await_exit(h, deadline) != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        /* A cleanup error keeps the private handle reachable. An explicit
         * second stop retries only removal on a worker, never BSP init. */
        h->cleanup_only = true;
        taskENTER_CRITICAL(&h->lock);
        h->finished = false;
        taskEXIT_CRITICAL(&h->lock);
        atomic_store_explicit(&h->task_exited, false, memory_order_release);
        if (xTaskCreate(worker, "echoy_health", 4096, h, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
            taskENTER_CRITICAL(&h->lock);
            h->finished = true;
            taskEXIT_CRITICAL(&h->lock);
            atomic_store_explicit(&h->task_exited, true, memory_order_release);
            xSemaphoreGive(h->done);
            return ESP_ERR_NO_MEM;
        }
    }
    /* A full binary semaphore already contains the desired wakeup. It remains
     * owned by h even after worker exit, avoiding a stale TaskHandle_t race. */
    xSemaphoreGive(h->wake);
    if (await_exit(h, deadline) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    if (h->cleanup_error != ESP_OK) {
        xSemaphoreGive(h->done);
        return h->cleanup_error;
    }
    vSemaphoreDelete(h->wake);
    vSemaphoreDelete(h->done);
    free(h);
    *handle = NULL;
    atomic_store_explicit(&service_owned, false, memory_order_release);
    return ESP_OK;
}
#endif
