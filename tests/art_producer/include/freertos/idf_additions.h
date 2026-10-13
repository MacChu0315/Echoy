#pragma once
#include "task.h"
BaseType_t xTaskCreateWithCaps(TaskFunction_t fn,const char *name,unsigned stack,void *ctx,
                             unsigned priority,TaskHandle_t *out,unsigned caps);
void vTaskDeleteWithCaps(TaskHandle_t task);
