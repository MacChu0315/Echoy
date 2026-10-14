#include "echoy_measurement_diagnostics.h"
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static atomic_flag busy = ATOMIC_FLAG_INIT;
static atomic_bool ready;

static esp_err_t finish(int n, size_t capacity, size_t *size)
{
    if (n < 0 || (size_t)n >= capacity) return ESP_ERR_INVALID_SIZE;
    *size = (size_t)n;
    return ESP_OK;
}

static esp_err_t format_status(const echoy_output_measurement_status_t *s,
                               uint8_t *out, size_t capacity, size_t *size)
{
    return finish(snprintf((char *)out, capacity,
        "{\"schema\":1,\"phase\":%u,\"generation\":%" PRIu32 ","
        "\"requested_generation\":%" PRIu32 ",\"runs_started\":%" PRIu32 ","
        "\"runs_remaining\":%" PRIu32 ",\"frames_written\":%" PRIu32 ","
        "\"frames_captured\":%" PRIu32 ",\"elapsed_us\":%" PRIu64 ","
        "\"input_gain_db\":%.0f,\"error_code\":%d,\"result_ready\":%s}",
        (unsigned)s->phase, s->generation, s->requested_generation, s->runs_started,
        s->runs_remaining, s->frames_written, s->frames_captured, s->elapsed_us,
        (double)s->input_gain_db, s->error_code, s->result_ready ? "true" : "false"),
        capacity, size);
}

static esp_err_t result(echoy_device_handle_t d, const uint8_t *payload,
                        uint8_t *out, size_t capacity, size_t *size)
{
    uint32_t run = (uint32_t)payload[0] | (uint32_t)payload[1] << 8 |
                   (uint32_t)payload[2] << 16 | (uint32_t)payload[3] << 24;
    unsigned index = payload[4];
    if (!run || (index >= ECHOY_OUTPUT_MEASUREMENT_TONE_COUNT && index != 255))
        return ESP_ERR_INVALID_ARG;
    /* Iris has a 4096-byte stack. The full immutable result, including private
     * waveform excerpts, must never be an automatic object on that stack. */
    echoy_output_measurement_result_t *r = heap_caps_calloc(
        1, sizeof(*r), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!r) return ESP_ERR_NO_MEM;
    esp_err_t err = echoy_device_measure_result(d, run, r);
    if (err != ESP_OK) { free(r); return err; }
    int n;
    if (index == 255) {
        const echoy_output_measurement_noise_t *q = &r->noise;
        n = snprintf((char *)out, capacity,
            "{\"run_id\":%" PRIu32 ",\"flags\":%" PRIu32 ",\"frames_captured\":%" PRIu32 ","
            "\"tones_analyzed\":%" PRIu32 ",\"capture_adc_peak\":%.8g,"
            "\"capture_clipped\":%" PRIu32 ",\"capture_near_clip\":%" PRIu32 ","
            "\"noise\":{\"flags\":%" PRIu32 ","
            "\"sample_count\":%" PRIu32 ",\"clipped\":%" PRIu32 ",\"near_clip\":%" PRIu32 ","
            "\"adc_peak\":%.8g,\"rms\":%.8g,\"dc\":%.8g,\"ac_rms\":%.8g}}",
            run, r->flags, r->frames_captured, r->tones_analyzed, (double)r->capture_adc_peak,
            r->capture_clipped, r->capture_near_clip, q->flags,
            q->sample_count, q->clipped, q->near_clip, (double)q->adc_peak,
            (double)q->rms, (double)q->dc, (double)q->ac_rms);
    } else {
        const echoy_output_measurement_tone_t *t = &r->tones[index];
        n = snprintf((char *)out, capacity,
            "{\"run_id\":%" PRIu32 ",\"index\":%u,\"flags\":%" PRIu32 ","
            "\"frequency_hz\":%u,\"pcm_peak\":%.8g,\"window_start\":%" PRIu32 ","
            "\"sample_count\":%" PRIu32 ",\"adc_peak\":%.8g,\"rms\":%.8g,\"dc\":%.8g,"
            "\"clipped\":%" PRIu32 ",\"near_clip\":%" PRIu32 ","
            "\"harmonic_amplitude\":[%.8g,%.8g,%.8g,%.8g,%.8g],"
            "\"harmonic_ratio\":[%.8g,%.8g,%.8g,%.8g],"
            "\"harmonic_rss_ratio\":%.8g,\"fundamental_snr_db\":%.8g}",
            run, index, t->flags, t->frequency_hz, (double)t->pcm_peak,
            t->window_start, t->sample_count, (double)t->adc_peak, (double)t->rms, (double)t->dc,
            t->clipped, t->near_clip, (double)t->harmonic_amplitude[0],
            (double)t->harmonic_amplitude[1], (double)t->harmonic_amplitude[2],
            (double)t->harmonic_amplitude[3], (double)t->harmonic_amplitude[4],
            (double)t->harmonic_ratio[0], (double)t->harmonic_ratio[1],
            (double)t->harmonic_ratio[2], (double)t->harmonic_ratio[3],
            (double)t->harmonic_rss_ratio, (double)t->fundamental_snr_db);
    }
    free(r);
    return finish(n, capacity, size);
}

static esp_err_t measurement_rpc(const esp_iris_rpc_request_t *request, uint8_t *out,
                                 size_t capacity, size_t *size, void *ctx)
{
    if (size) *size = 0;
    if (!request || !out || !size || !ctx) return ESP_ERR_INVALID_ARG;
    if (!atomic_load(&ready)) return ESP_ERR_INVALID_STATE;
    if (request->service_id != 0xeca2) return ESP_ERR_NOT_SUPPORTED;
    if (atomic_flag_test_and_set(&busy)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_ERR_INVALID_SIZE;
    echoy_output_measurement_status_t status;
    if (request->method_id == 4) {
        if (request->payload_size == 5 && request->payload)
            err = result(ctx, request->payload, out, capacity, size);
    } else if (request->method_id >= 1 && request->method_id <= 3) {
        /* Check response capacity before submitting any mutating command. */
        if (capacity < 512) goto done;
        if (request->method_id == 2) {
            if (request->payload_size != 1 || !request->payload) goto done;
            unsigned gain = request->payload[0];
            if (gain > 30 || gain % 6) { err = ESP_ERR_INVALID_ARG; goto done; }
            err = echoy_device_measure_request(ctx, true, gain, &status);
        } else {
            if (request->payload_size) goto done;
            err = request->method_id == 1 ? echoy_device_measure_get(ctx, &status)
                                         : echoy_device_measure_request(ctx, false, 0, &status);
        }
        if (err == ESP_OK) err = format_status(&status, out, capacity, size);
    } else err = ESP_ERR_NOT_SUPPORTED;
done:
    atomic_flag_clear(&busy);
    return err;
}

esp_err_t echoy_measurement_diagnostics_register(echoy_device_handle_t device)
{
    if (!device) return ESP_ERR_INVALID_ARG;
    if (atomic_load(&ready)) return ESP_ERR_INVALID_STATE;
    for (unsigned method = 1; method <= 4; ++method) {
        esp_err_t err = esp_iris_rpc_register(0xeca2, method, measurement_rpc, device);
        if (err == ESP_OK) continue;
        while (--method) {
            esp_err_t cleanup = esp_iris_rpc_unregister(0xeca2, method);
            if (cleanup != ESP_OK) ESP_LOGE("echoy_measure", "RPC cleanup failed: %d", cleanup);
        }
        return err;
    }
    atomic_store(&ready, true);
    return ESP_OK;
}
