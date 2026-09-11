#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include "FreeRTOS.h"
#include <stddef.h>
typedef void *QueueHandle_t;
#define pdTRUE 1
QueueHandle_t xQueueCreate(unsigned count,size_t size);
int xQueueSend(QueueHandle_t queue,const void *item,unsigned wait);
int xQueueReceive(QueueHandle_t queue,void *item,unsigned wait);
void vQueueDelete(QueueHandle_t queue);
#endif
