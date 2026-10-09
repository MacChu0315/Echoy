#pragma once
#include "FreeRTOS.h"
typedef struct fake_event *EventGroupHandle_t;
typedef unsigned EventBits_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, BaseType_t clear,
                                BaseType_t all, TickType_t timeout);
void vEventGroupDelete(EventGroupHandle_t group);
