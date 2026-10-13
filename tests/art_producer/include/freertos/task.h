#pragma once
#include "FreeRTOS.h"
typedef struct producer_fake_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef enum { eRunning, eReady, eBlocked, eSuspended, eDeleted, eInvalid } eTaskState;
void xTaskNotifyGive(TaskHandle_t task);
unsigned ulTaskNotifyTake(BaseType_t clear,TickType_t ticks);
void vTaskDelay(TickType_t ticks);
void vTaskSuspend(TaskHandle_t task);
eTaskState eTaskGetState(TaskHandle_t task);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
