#define _GNU_SOURCE
#include "device_fakes.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct producer_fake_task {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    TaskFunction_t fn;
    void *ctx;
    atomic_uint state;
    unsigned notices, watermark_calls;
    bool deleted;
};
static _Thread_local TaskHandle_t current;
static atomic_bool hold_suspend, suspend_pending;
static unsigned external_checks, external_fail;
static void sleep_ms(unsigned ms){struct timespec t={ms/1000,(long)(ms%1000)*1000000};nanosleep(&t,NULL);}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void producer_test_external_check_fail(unsigned nth){external_checks=0;external_fail=nth;}
bool esp_ptr_external_ram(const void *ptr){return ptr && ++external_checks!=external_fail;}
void producer_test_hold_suspend(bool hold){atomic_store(&hold_suspend,hold);}
void producer_test_wait_suspend_pending(void){int64_t end=esp_timer_get_time()+3000000;while(!atomic_load(&suspend_pending)){assert(esp_timer_get_time()<end);sleep_ms(1);}}
static void *run(void *arg){current=arg;current->fn(current->ctx);abort();}
BaseType_t xTaskCreateWithCaps(TaskFunction_t fn,const char *name,unsigned stack,void *ctx,
                             unsigned priority,TaskHandle_t *out,unsigned caps){
    assert(stack==12288 && priority==1 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    assert(!strcmp(name,"echoy_art"));
    TaskHandle_t task=calloc(1,sizeof(*task));assert(task);
    task->fn=fn;task->ctx=ctx;atomic_init(&task->state,eRunning);
    assert(!pthread_mutex_init(&task->lock,NULL));assert(!pthread_cond_init(&task->changed,NULL));
    *out=task;atomic_store(&suspend_pending,false);
    if(pthread_create(&task->thread,NULL,run,task)){
        pthread_cond_destroy(&task->changed);pthread_mutex_destroy(&task->lock);free(task);*out=NULL;return 0;
    }
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task){assert(task);pthread_mutex_lock(&task->lock);assert(!task->deleted);task->notices++;pthread_cond_signal(&task->changed);pthread_mutex_unlock(&task->lock);}
unsigned ulTaskNotifyTake(BaseType_t clear,TickType_t ticks){
    assert(current && clear==pdTRUE);struct timespec until;clock_gettime(CLOCK_REALTIME,&until);
    until.tv_sec+=ticks/1000;until.tv_nsec+=(long)(ticks%1000)*1000000;
    if(until.tv_nsec>=1000000000){until.tv_nsec-=1000000000;until.tv_sec++;}
    pthread_mutex_lock(&current->lock);
    while(!current->notices){int ret=pthread_cond_timedwait(&current->changed,&current->lock,&until);assert(!ret || ret==ETIMEDOUT);if(ret)break;}
    unsigned n=current->notices;current->notices=0;pthread_mutex_unlock(&current->lock);return n;
}
void vTaskDelay(TickType_t ticks){sleep_ms(ticks);}
void vTaskSuspend(TaskHandle_t task){
    assert(!task && current);atomic_store(&suspend_pending,true);
    while(atomic_load(&hold_suspend))sleep_ms(1);
    pthread_mutex_lock(&current->lock);atomic_store(&current->state,eSuspended);
    pthread_cond_broadcast(&current->changed);
    while(!current->deleted)pthread_cond_wait(&current->changed,&current->lock);
    pthread_mutex_unlock(&current->lock);pthread_exit(NULL);
}
eTaskState eTaskGetState(TaskHandle_t task){assert(task);return atomic_load(&task->state);}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task){assert(!task && current);assert(atomic_load(&current->state)==eRunning);return 10000-64*++current->watermark_calls;}
void vTaskDeleteWithCaps(TaskHandle_t task){
    assert(task && current!=task);pthread_mutex_lock(&task->lock);
    assert(atomic_load(&task->state)==eSuspended);task->deleted=true;
    pthread_cond_broadcast(&task->changed);pthread_mutex_unlock(&task->lock);
    assert(!pthread_join(task->thread,NULL));
    atomic_store(&task->state,eDeleted);pthread_cond_destroy(&task->changed);pthread_mutex_destroy(&task->lock);free(task);
}
