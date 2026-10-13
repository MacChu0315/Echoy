#pragma once
#include <pthread.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
void touch_test_critical_enter(portMUX_TYPE *lock);
#define portENTER_CRITICAL(lock) touch_test_critical_enter(lock)
#define portEXIT_CRITICAL(lock) pthread_mutex_unlock(lock)
#define portENTER_CRITICAL_ISR(lock) pthread_mutex_lock(lock)
#define portEXIT_CRITICAL_ISR(lock) pthread_mutex_unlock(lock)
