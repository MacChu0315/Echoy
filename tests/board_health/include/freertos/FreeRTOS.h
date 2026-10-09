#pragma once
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef struct {
    int unused;
} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(value) ((TickType_t)(value))
#define tskIDLE_PRIORITY 0
void fake_critical_enter(void);
void fake_critical_exit(void);
#define taskENTER_CRITICAL(lock)                                                                   \
    do {                                                                                           \
        (void)(lock);                                                                              \
        fake_critical_enter();                                                                     \
    } while (0)
#define taskEXIT_CRITICAL(lock)                                                                    \
    do {                                                                                           \
        (void)(lock);                                                                              \
        fake_critical_exit();                                                                      \
    } while (0)
