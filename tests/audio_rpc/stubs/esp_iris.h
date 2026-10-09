#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint16_t service_id, method_id;
    uint32_t request_id, deadline_ms;
    const uint8_t *payload;
    size_t payload_size;
} esp_iris_rpc_request_t;
typedef esp_err_t (*esp_iris_rpc_handler_t)(const esp_iris_rpc_request_t *, uint8_t *, size_t,
                                         size_t *, void *);
esp_err_t esp_iris_rpc_register(uint16_t, uint16_t, esp_iris_rpc_handler_t, void *);
esp_err_t esp_iris_rpc_unregister(uint16_t, uint16_t);
