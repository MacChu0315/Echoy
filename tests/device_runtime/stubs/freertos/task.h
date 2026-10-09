#pragma once
#include "FreeRTOS.h"
typedef struct fake_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, unsigned stack, void *ctx,
                       unsigned priority, TaskHandle_t *out);
void vTaskDelay(TickType_t delay);
void vTaskDelete(TaskHandle_t task);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
TickType_t xTaskGetTickCount(void);
