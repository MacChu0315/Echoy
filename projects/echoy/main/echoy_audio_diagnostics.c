#include "echoy_audio_diagnostics.h"
#include "esp_iris.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static esp_err_t diagnostic_rpc(const esp_iris_rpc_request_t *request, uint8_t *response,
                                size_t capacity, size_t *size, void *ctx)
{
    if (!request || !response || !size || !ctx) {
        return ESP_ERR_INVALID_ARG;
    }
    *size = 0;
    if (request->payload_size != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (request->method_id == 2 || request->method_id == 3) {
        static const char accepted[] = "{\"accepted\":true}";
        if (capacity < sizeof(accepted)) return ESP_ERR_INVALID_SIZE;
        esp_err_t err = echoy_device_probe_request(ctx, request->method_id == 2);
        if (err != ESP_OK) {
            return err;
        }
        memcpy(response, accepted, sizeof(accepted));
        *size = sizeof(accepted) - 1;
        return ESP_OK;
    } else if (request->method_id != 1) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    echoy_device_diagnostics_t d;
    esp_err_t err = echoy_device_probe_get(ctx, &d);
    if (err != ESP_OK) {
        return err;
    }
    int n = snprintf(
        (char *)response, capacity,
        "{\"phase\":%d,\"error\":%d,\"condition\":%d,\"ui_volume\":%u,\"generation\":%u,"
        "\"requested_generation\":%u,"
        "\"hardware_volume\":%u,\"frames\":%u,\"frequency_hz\":1000,\"peak_pcm\":8192,"
        "\"before\":{\"valid\":%u,\"error\":%d,\"at_us\":%lld,\"pa_requested\":%u,"
        "\"regs\":[%u,%u,%u,%u,%u,%u,%u]},"
        "\"after\":{\"valid\":%u,\"error\":%d,\"at_us\":%lld,\"pa_requested\":%u,"
        "\"regs\":[%u,%u,%u,%u,%u,%u,%u]},"
        "\"output\":{\"accepted\":%llu,\"nonzero\":%llu,\"peak\":%u,\"pa_requested\":%u,\"errors\":"
        "%u}}",
        d.phase, d.error, d.condition, d.volume_percent, d.generation, d.requested_generation,
        BOARD_AUDIO_HARDWARE_VOLUME, d.frames_accepted, d.before.valid_mask, d.before.error,
        (long long)d.before.sampled_at_us, d.before.pa_requested, d.before.registers[0],
        d.before.registers[1], d.before.registers[2], d.before.registers[3], d.before.registers[4],
        d.before.registers[5], d.before.registers[6], d.after.valid_mask, d.after.error,
        (long long)d.after.sampled_at_us, d.after.pa_requested, d.after.registers[0],
        d.after.registers[1], d.after.registers[2], d.after.registers[3], d.after.registers[4],
        d.after.registers[5], d.after.registers[6], (unsigned long long)d.output.frames_written,
        (unsigned long long)d.output.nonzero_frames, (unsigned)d.output.peak_pcm,
        d.output.pa_enabled, (unsigned)d.output.write_failures);
    if (n < 0 || (size_t)n >= capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    *size = (size_t)n;
    return ESP_OK;
}
esp_err_t echoy_audio_diagnostics_register(echoy_device_handle_t device)
{
    if (!device) {
        return ESP_ERR_INVALID_ARG;
    }
    for (unsigned method = 1; method <= 3; ++method) {
        esp_err_t error = esp_iris_rpc_register(0xeca0, method, diagnostic_rpc, device);
        if (error != ESP_OK) {
            while (--method) {
                esp_err_t cleanup = esp_iris_rpc_unregister(0xeca0, method);
                if (cleanup != ESP_OK) ESP_LOGE("echoy_probe", "RPC cleanup failed: %d", cleanup);
            }
            return error;
        }
    }
    return ESP_OK;
}
