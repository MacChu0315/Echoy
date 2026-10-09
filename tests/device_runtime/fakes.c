#define _GNU_SOURCE
#include "fakes.h"
#include "board_audio.h"
#include "board_health.h"
#include "echoy_band.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

atomic_int fake_open_calls, fake_close_calls, fake_write_calls, fake_flush_calls;
atomic_int fake_partial_next, fake_open_failure, fake_close_failure, fake_flush_failure;
atomic_int fake_task_failure, fake_health_stop_timeout, fake_event_tail_reads_after_delete;
atomic_int fake_watermark_after_exit, fake_nonzero_blocks;
static atomic_int health_percent = -1;
static atomic_bool hold_write, hold_create, hold_prepare, write_blocked, prepare_blocked, create_blocked;
void fake_sleep_ms(unsigned ms)
{
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&t, &t) && errno == EINTR) {
    }
}
int64_t esp_timer_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "ESP_OK" : "injected failure";
}
void fake_log(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
}
void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    (void)caps;
    return calloc(count, size);
}
size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return 4000000;
}
static struct timespec deadline(TickType_t ticks)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ticks / 100;
    t.tv_nsec += (ticks % 100) * 10000000L;
    if (t.tv_nsec >= 1000000000) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000;
    }
    return t;
}
static int wait(pthread_cond_t *c, pthread_mutex_t *m, TickType_t ticks, const struct timespec *t)
{
    if (!ticks)
        return ETIMEDOUT;
    return ticks == portMAX_DELAY ? pthread_cond_wait(c, m) : pthread_cond_timedwait(c, m, t);
}
struct fake_queue {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned count, size, head, used;
    char data[];
};
QueueHandle_t xQueueCreate(unsigned count, unsigned size)
{
    struct fake_queue *q = calloc(1, sizeof(*q) + count * size);
    assert(q);
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->changed, NULL);
    q->count = count;
    q->size = size;
    return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t timeout)
{
    struct timespec t = deadline(timeout);
    pthread_mutex_lock(&q->lock);
    while (q->used == q->count) {
        if (wait(&q->changed, &q->lock, timeout, &t) == ETIMEDOUT) {
            pthread_mutex_unlock(&q->lock);
            return pdFALSE;
        }
    }
    memcpy(q->data + ((q->head + q->used) % q->count) * q->size, item, q->size);
    q->used++;
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->lock);
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t timeout)
{
    struct timespec t = deadline(timeout);
    pthread_mutex_lock(&q->lock);
    while (!q->used) {
        if (wait(&q->changed, &q->lock, timeout, &t) == ETIMEDOUT) {
            pthread_mutex_unlock(&q->lock);
            return pdFALSE;
        }
    }
    memcpy(item, q->data + q->head * q->size, q->size);
    q->head = (q->head + 1) % q->count;
    q->used--;
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->lock);
    return pdTRUE;
}
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item)
{
    assert(q->count == 1);
    pthread_mutex_lock(&q->lock);
    memcpy(q->data, item, q->size);
    q->head = 0;
    q->used = 1;
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->lock);
    return pdTRUE;
}
void vQueueDelete(QueueHandle_t q)
{
    pthread_cond_destroy(&q->changed);
    pthread_mutex_destroy(&q->lock);
    free(q);
}
struct fake_mutex {
    pthread_mutex_t lock;
};
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    struct fake_mutex *m = malloc(sizeof(*m));
    assert(m);
    pthread_mutex_init(&m->lock, NULL);
    return m;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t timeout)
{
    int r;
    if (timeout == portMAX_DELAY)
        r = pthread_mutex_lock(&m->lock);
    else if (!timeout)
        r = pthread_mutex_trylock(&m->lock);
    else {
        struct timespec t = deadline(timeout);
        r = pthread_mutex_timedlock(&m->lock, &t);
    }
    return r == 0 ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m)
{
    return pthread_mutex_unlock(&m->lock) == 0 ? pdTRUE : pdFALSE;
}
void vSemaphoreDelete(SemaphoreHandle_t m)
{
    pthread_mutex_destroy(&m->lock);
    free(m);
}
struct fake_event {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    atomic_uint bits;
    atomic_bool deleted;
    struct fake_event *next;
};
static struct fake_event *events;
EventGroupHandle_t xEventGroupCreate(void)
{
    struct fake_event *e = calloc(1, sizeof(*e));
    assert(e);
    pthread_mutex_init(&e->lock, NULL);
    pthread_cond_init(&e->changed, NULL);
    e->next = events;
    events = e;
    return e;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t e, EventBits_t bits)
{
    pthread_mutex_lock(&e->lock);
    atomic_fetch_or(&e->bits, bits);
    pthread_cond_broadcast(&e->changed);
    pthread_mutex_unlock(&e->lock); /* Model the pinned SDK's read after unlock/waking waiters. */
    if (bits & (BIT0 | BIT1))
        fake_sleep_ms(8);
    if (atomic_load(&e->deleted))
        atomic_fetch_add(&fake_event_tail_reads_after_delete, 1);
    return atomic_load(&e->bits);
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t e, EventBits_t bits, BaseType_t clear,
                                BaseType_t all, TickType_t timeout)
{
    struct timespec t = deadline(timeout);
    pthread_mutex_lock(&e->lock);
    unsigned value;
    while (1) {
        value = atomic_load(&e->bits);
        if ((all ? (value & bits) == bits : (value & bits) != 0))
            break;
        if (wait(&e->changed, &e->lock, timeout, &t) == ETIMEDOUT)
            break;
    }
    if (clear)
        atomic_fetch_and(&e->bits, ~bits);
    pthread_mutex_unlock(&e->lock);
    return value;
}
void vEventGroupDelete(EventGroupHandle_t e)
{ /* Quarantine until join so the SDK-tail regression is deterministic. */
  atomic_store(&e->deleted, true); }
struct fake_task {
    pthread_t thread;
    TaskFunction_t fn;
    void *ctx;
    atomic_bool exited;
    struct fake_task *next;
};
static struct fake_task *tasks;
static _Thread_local struct fake_task *self;
static atomic_int task_creates;
static void *entry(void *p)
{
    self = p;
    self->fn(self->ctx);
    atomic_store(&self->exited, true);
    return NULL;
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, unsigned stack, void *ctx,
                       unsigned priority, TaskHandle_t *out)
{
    (void)name;
    (void)stack;
    (void)priority;
    int index = atomic_fetch_add(&task_creates, 1) + 1;
    if (atomic_load(&fake_task_failure) == index)
        return pdFALSE;
    struct fake_task *t = calloc(1, sizeof(*t));
    assert(t);
    t->fn = fn;
    t->ctx = ctx;
    t->next = tasks;
    tasks = t;
    *out = t;
    assert(!pthread_create(&t->thread, NULL, entry, t));
    return pdPASS;
}
void vTaskDelay(TickType_t delay) { fake_sleep_ms(delay * 10); }
void vTaskDelete(TaskHandle_t task)
{
    assert(!task);
    atomic_store(&self->exited, true);
    pthread_exit(NULL);
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    struct fake_task *t = task ? task : self;
    if (t && atomic_load(&t->exited))
        atomic_fetch_add(&fake_watermark_after_exit, 1);
    return 8192;
}
TickType_t xTaskGetTickCount(void) { return (TickType_t)(esp_timer_get_time() / 10000); }
void fake_join_tasks(void)
{
    while (tasks) {
        struct fake_task *t = tasks;
        tasks = t->next;
        pthread_join(t->thread, NULL);
        free(t);
    }
    while (events) {
        struct fake_event *e = events;
        events = e->next;
        assert(atomic_load(&e->deleted));
        pthread_cond_destroy(&e->changed);
        pthread_mutex_destroy(&e->lock);
        free(e);
    }
    assert(!atomic_load(&fake_watermark_after_exit));
    assert(!atomic_load(&fake_event_tail_reads_after_delete));
}

void fake_set_health(int percentage) { atomic_store(&health_percent, percentage); }
void fake_hold_write(bool value) { atomic_store(&hold_write, value); }
void fake_hold_create(bool value) { atomic_store(&hold_create, value); }
void fake_hold_prepare(bool value) { atomic_store(&hold_prepare, value); }
bool fake_write_is_blocked(void) { return atomic_load(&write_blocked); }
bool fake_prepare_is_blocked(void) { return atomic_load(&prepare_blocked); }
struct board_audio {
    board_audio_stats_t stats;
    int64_t next_write;
};
esp_err_t board_audio_open(board_audio_t **out)
{
    *out = NULL;
    atomic_fetch_add(&fake_open_calls, 1);
    if (atomic_exchange(&fake_open_failure, 0))
        return ESP_FAIL;
    *out = calloc(1, sizeof(**out));
    assert(*out);
    (*out)->next_write = esp_timer_get_time();
    return ESP_OK;
}
esp_err_t board_audio_write(board_audio_t *a, const int16_t *pcm, size_t frames, size_t *written)
{
    assert(a);
    atomic_fetch_add(&fake_write_calls, 1);
    while (atomic_load(&hold_write)) {
        atomic_store(&write_blocked, true);
        fake_sleep_ms(1);
    }
    atomic_store(&write_blocked, false);
    bool partial = atomic_exchange(&fake_partial_next, 0);
    *written = partial ? frames / 2 : frames;
    for (size_t i = 0; i < *written; i++)
        if (pcm[i]) {
            atomic_fetch_add(&fake_nonzero_blocks, 1);
            break;
        }
    a->stats.frames_written += *written;
    a->stats.write_calls++;
    a->next_write += (int64_t)(*written * 1000000 / BOARD_AUDIO_SAMPLE_RATE);
    struct timespec t = {a->next_write / 1000000, (a->next_write % 1000000) * 1000};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
    return partial ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t board_audio_flush(board_audio_t *a)
{
    assert(a);
    atomic_fetch_add(&fake_flush_calls, 1);
    a->stats.flushes++;
    a->next_write = esp_timer_get_time();
    return atomic_exchange(&fake_flush_failure, 0) ? ESP_FAIL : ESP_OK;
}
esp_err_t board_audio_close(board_audio_t **a)
{
    if (!a || !*a)
        return ESP_OK;
    atomic_fetch_add(&fake_close_calls, 1);
    free(*a);
    *a = NULL;
    return atomic_exchange(&fake_close_failure, 0) ? ESP_FAIL : ESP_OK;
}
esp_err_t board_audio_get_stats(const board_audio_t *a, board_audio_stats_t *out)
{
    *out = a->stats;
    return ESP_OK;
}
struct board_health {
    int unused;
};
esp_err_t board_health_start(const board_health_config_t *c, board_health_handle_t *out)
{
    (void)c;
    *out = calloc(1, sizeof(**out));
    return *out ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t board_health_get(board_health_handle_t h, board_health_snapshot_t *out)
{
    assert(h);
    int p = atomic_load(&health_percent);
    *out = (board_health_snapshot_t){.percentage = p >= 0 ? p : -1,
                                     .valid = p >= 0,
                                     .present_known = p >= 0 || p == -2,
                                     .present = p >= 0};
    return ESP_OK;
}
esp_err_t board_health_stop(board_health_handle_t *h, uint32_t timeout)
{
    (void)timeout;
    if (atomic_exchange(&fake_health_stop_timeout, 0))
        return ESP_ERR_TIMEOUT;
    free(*h);
    *h = NULL;
    return ESP_OK;
}
bool board_health_battery_empty(const board_health_snapshot_t *s)
{
    return s->valid && s->present_known && s->present && s->percentage == 0;
}
echoy_band_t *__real_echoy_band_create(void);
echoy_band_t *__wrap_echoy_band_create(void)
{
    while (atomic_load(&hold_create)) {
        atomic_store(&create_blocked, true);
        fake_sleep_ms(1);
    }
    atomic_store(&create_blocked, false);
    return __real_echoy_band_create();
}
bool fake_create_is_blocked(void)
{
    return atomic_load(&create_blocked);
}
bool __real_echoy_band_prepare_job(echoy_band_job_t *job);
bool __wrap_echoy_band_prepare_job(echoy_band_job_t *job)
{
    while (atomic_load(&hold_prepare)) {
        atomic_store(&prepare_blocked, true);
        fake_sleep_ms(1);
    }
    atomic_store(&prepare_blocked, false);
    return __real_echoy_band_prepare_job(job);
}
