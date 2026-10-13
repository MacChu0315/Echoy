#ifndef ESP_PLATFORM
#define _GNU_SOURCE
#endif
#include "echoy_art_producer.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#else
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#endif

enum { WIDTH = 300, HEIGHT = 204, FRAME_COUNT = 2, STACK_BYTES = 12288 };
enum frame_state { FREE, RENDERING, READY, BORROWED };
typedef struct {
    atomic_uint state;
    uint16_t *pixels;
    uint32_t epoch;
    uint64_t serial;
} producer_frame_t;
typedef struct {
    echoy_art_frame_t frame;
    echoy_plan_t plan;
    uint32_t epoch;
    uint64_t serial;
} producer_request_t;
struct echoy_art_producer {
    producer_frame_t frames[FRAME_COUNT];
    producer_request_t pending, drawing;
    struct echoy_art_workspace *workspace;
    atomic_flag mailbox;
    bool pending_valid, stopped;
    uint64_t next_serial;
    atomic_uint epoch;
    atomic_bool stopping, quiescent;
    atomic_int error;
    atomic_uint rendered, published, stale, coalesced, draw_max_us, worker_stack_min;
#ifdef ESP_PLATFORM
    TaskHandle_t task;
#else
    pthread_t thread;
    sem_t wake;
    atomic_bool wake_pending;
#endif
};

static int64_t now_us(void)
{
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
#endif
}

/* Only the serialized UI/lifecycle owner signals. Release callbacks do not
 * touch worker handles or synchronization objects, including after stop. */
static void wake_worker(echoy_art_producer_t *p)
{
#ifdef ESP_PLATFORM
    xTaskNotifyGive(p->task);
#else
    if (!atomic_exchange(&p->wake_pending, true) && sem_post(&p->wake) != 0)
        atomic_store(&p->error, ESP_GSP_ERR_INVALID_STATE);
#endif
}

static bool wait_worker(echoy_art_producer_t *p)
{
#ifdef ESP_PLATFORM
    (void)p;
    TickType_t ticks = pdMS_TO_TICKS(10);
    ulTaskNotifyTake(pdTRUE, ticks ? ticks : 1);
    return true;
#else
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 10000000;
    if (until.tv_nsec >= 1000000000) { until.tv_nsec -= 1000000000; ++until.tv_sec; }
    int result;
    do { result = sem_timedwait(&p->wake, &until); } while (result && errno == EINTR);
    if (result && errno != ETIMEDOUT) return false;
    if (!result) atomic_store(&p->wake_pending, false);
    return true;
#endif
}

static bool render_one(echoy_art_producer_t *p)
{
    if (atomic_flag_test_and_set_explicit(&p->mailbox, memory_order_acquire)) return false;
    if (!p->pending_valid) {
        atomic_flag_clear_explicit(&p->mailbox, memory_order_release);
        return false;
    }
    if (p->pending.epoch != atomic_load(&p->epoch)) {
        p->pending_valid = false;
        atomic_fetch_add(&p->stale, 1);
        atomic_flag_clear_explicit(&p->mailbox, memory_order_release);
        return true;
    }
    producer_frame_t *slot = NULL;
    for (unsigned i = 0; i < FRAME_COUNT; ++i) {
        unsigned expected = FREE;
        if (atomic_compare_exchange_strong(&p->frames[i].state, &expected, RENDERING)) {
            slot = &p->frames[i];
            break;
        }
    }
    if (!slot) {
        atomic_flag_clear_explicit(&p->mailbox, memory_order_release);
        return false;
    }
    p->drawing = p->pending;
    p->drawing.frame.plan = p->pending.frame.plan ? &p->drawing.plan : NULL;
    p->drawing.frame.workspace = p->workspace;
    p->pending_valid = false;
    atomic_flag_clear_explicit(&p->mailbox, memory_order_release);

    esp_gsp_canvas_surface_t surface = {.pixels = slot->pixels, .stride_bytes = WIDTH * 2,
        .width = WIDTH, .height = HEIGHT, .pixel_format = ESP_GSP_CANVAS_PIXEL_RGB565};
    int64_t began = now_us();
    echoy_art_draw(&surface, &p->drawing.frame);
    int64_t elapsed = now_us() - began;
    unsigned duration = elapsed > UINT_MAX ? UINT_MAX : elapsed > 0 ? (unsigned)elapsed : 0;
    if (duration > atomic_load(&p->draw_max_us)) atomic_store(&p->draw_max_us, duration);
#ifdef ESP_PLATFORM
    unsigned remaining = uxTaskGetStackHighWaterMark(NULL);
    unsigned old = atomic_load(&p->worker_stack_min);
    if (!old || remaining < old) atomic_store(&p->worker_stack_min, remaining);
#endif
    if (atomic_load(&p->stopping) || p->drawing.epoch != atomic_load(&p->epoch)) {
        atomic_fetch_add(&p->stale, 1);
        atomic_store_explicit(&slot->state, FREE, memory_order_release);
    } else {
        slot->epoch = p->drawing.epoch;
        slot->serial = p->drawing.serial;
        atomic_store_explicit(&slot->state, READY, memory_order_release);
    }
    atomic_fetch_add(&p->rendered, 1);
    return true;
}

static void worker_loop(echoy_art_producer_t *p)
{
    while (!atomic_load(&p->stopping)) {
        if (!render_one(p) && !wait_worker(p)) {
            atomic_store(&p->error, ESP_GSP_ERR_INVALID_STATE);
            atomic_store(&p->stopping, true);
        }
    }
    atomic_store_explicit(&p->quiescent, true, memory_order_release);
}
#ifdef ESP_PLATFORM
static void worker_entry(void *ctx)
{
    worker_loop(ctx);
    /* No access to producer storage after this point. The lifecycle owner
     * confirms eSuspended and uses the matching WithCaps deletion API. */
    for (;;) vTaskSuspend(NULL);
}
#else
static void *worker_entry(void *ctx)
{
    worker_loop(ctx);
    return NULL;
}
#endif

static bool storage_valid(const void *ptr)
{
#ifdef ESP_PLATFORM
    return ptr && esp_ptr_external_ram(ptr);
#else
    return ptr != NULL;
#endif
}
static void free_storage(echoy_art_producer_t *p)
{
    echoy_art_workspace_delete(p->workspace);
    for (unsigned i = 0; i < FRAME_COUNT; ++i) free(p->frames[i].pixels);
    free(p);
}

esp_gsp_err_t echoy_art_producer_create(const echoy_art_producer_config_t *config,
                                      echoy_art_producer_t **out)
{
    if (out) *out = NULL;
    if (!out || !config || !config->storage_alloc) return ESP_GSP_ERR_INVALID_ARG;
    echoy_art_producer_t *p = config->storage_alloc(config->storage_ctx, sizeof(*p));
    if (!storage_valid(p)) { free(p); return ESP_GSP_ERR_NO_MEM; }
    memset(p, 0, sizeof(*p));
    atomic_flag_clear(&p->mailbox);
    atomic_init(&p->epoch, 0);
    atomic_init(&p->stopping, false);
    atomic_init(&p->quiescent, false);
    atomic_init(&p->error, ESP_GSP_OK);
    atomic_init(&p->rendered, 0);
    atomic_init(&p->published, 0);
    atomic_init(&p->stale, 0);
    atomic_init(&p->coalesced, 0);
    atomic_init(&p->draw_max_us, 0);
    atomic_init(&p->worker_stack_min, 0);
    for (unsigned i = 0; i < FRAME_COUNT; ++i) {
        atomic_init(&p->frames[i].state, FREE);
        p->frames[i].pixels = config->storage_alloc(config->storage_ctx, WIDTH * HEIGHT * 2);
        if (!storage_valid(p->frames[i].pixels)) { free_storage(p); return ESP_GSP_ERR_NO_MEM; }
    }
    p->workspace = echoy_art_workspace_create_with_allocator(config->storage_alloc,
                                                             config->storage_ctx);
    if (!storage_valid(p->workspace)) { free_storage(p); return ESP_GSP_ERR_NO_MEM; }
#ifdef ESP_PLATFORM
    if (xTaskCreateWithCaps(worker_entry, "echoy_art", STACK_BYTES, p, 1, &p->task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        free_storage(p);
        return ESP_GSP_ERR_NO_MEM;
    }
#else
    atomic_init(&p->wake_pending, false);
    if (sem_init(&p->wake, 0, 0) != 0) { free_storage(p); return ESP_GSP_ERR_NO_MEM; }
    if (pthread_create(&p->thread, NULL, worker_entry, p) != 0) {
        sem_destroy(&p->wake);
        free_storage(p);
        return ESP_GSP_ERR_NO_MEM;
    }
#endif
    *out = p;
    return ESP_GSP_OK;
}

void echoy_art_producer_set_epoch(echoy_art_producer_t *p, uint32_t epoch)
{
    if (!p) return;
    atomic_store(&p->epoch, epoch);
    /* Only the UI retires READY storage, so it may inspect immutable metadata
     * without racing a worker that recycles the same frame. */
    for (unsigned i = 0; i < FRAME_COUNT; ++i) {
        producer_frame_t *slot = &p->frames[i];
        if (atomic_load_explicit(&slot->state, memory_order_acquire) == READY && slot->epoch != epoch) {
            atomic_fetch_add(&p->stale, 1);
            atomic_store_explicit(&slot->state, FREE, memory_order_release);
        }
    }
    if (!atomic_load(&p->stopping)) wake_worker(p);
}

bool echoy_art_producer_request(echoy_art_producer_t *p, const echoy_art_frame_t *frame,
                                uint32_t epoch)
{
    if (!p || !frame || frame->width != WIDTH || frame->height != HEIGHT ||
        atomic_load(&p->stopping) || atomic_load(&p->error) != ESP_GSP_OK ||
        epoch != atomic_load(&p->epoch)) return false;
    if (atomic_flag_test_and_set_explicit(&p->mailbox, memory_order_acquire)) return false;
    if (p->pending_valid) atomic_fetch_add(&p->coalesced, 1);
    p->pending.frame = *frame;
    p->pending.frame.workspace = NULL;
    if (frame->plan) p->pending.plan = *frame->plan;
    p->pending.frame.plan = frame->plan ? &p->pending.plan : NULL;
    p->pending.epoch = epoch;
    p->pending.serial = ++p->next_serial;
    p->pending_valid = true;
    atomic_flag_clear_explicit(&p->mailbox, memory_order_release);
    wake_worker(p);
    return true;
}

static void release_frame(void *ctx)
{
    producer_frame_t *slot = ctx;
    /* Last access to retained storage. No task notification: the task may
     * already have been joined/deleted before the GSP flush releases us.
     * The live worker retries free-buffer discovery within ten milliseconds. */
    atomic_store_explicit(&slot->state, FREE, memory_order_release);
}

esp_gsp_err_t echoy_art_producer_publish_ready(echoy_art_producer_t *p, esp_gsp_handle_t gsp,
                                             uint16_t bind, uint32_t epoch, bool *published)
{
    if (published) *published = false;
    if (!p || !gsp || !published) return ESP_GSP_ERR_INVALID_ARG;
    esp_gsp_err_t error = atomic_load(&p->error);
    if (error != ESP_GSP_OK) return error;
    if (atomic_load(&p->stopping) || epoch != atomic_load(&p->epoch))
        return ESP_GSP_ERR_INVALID_STATE;
    producer_frame_t *ready = NULL;
    for (unsigned i = 0; i < FRAME_COUNT; ++i) {
        producer_frame_t *slot = &p->frames[i];
        if (atomic_load_explicit(&slot->state, memory_order_acquire) != READY) continue;
        if (slot->epoch != epoch) {
            atomic_fetch_add(&p->stale, 1);
            atomic_store_explicit(&slot->state, FREE, memory_order_release);
        } else if (!ready || slot->serial < ready->serial) ready = slot;
    }
    if (!ready) return ESP_GSP_OK;
    atomic_store_explicit(&ready->state, BORROWED, memory_order_release);
    error = esp_gsp_canvas_try_push(gsp, bind, ready->pixels, WIDTH * 2, release_frame, ready);
    if (error != ESP_GSP_OK) {
        /* The GSP contract guarantees no release after an immediate error. */
        atomic_store_explicit(&ready->state, READY, memory_order_release);
        return error;
    }
    atomic_fetch_add(&p->published, 1);
    *published = true;
    return ESP_GSP_OK;
}

esp_gsp_err_t echoy_art_producer_stop(echoy_art_producer_t *p, uint32_t timeout_ms)
{
    if (!p) return ESP_GSP_ERR_INVALID_ARG;
    if (p->stopped) return ESP_GSP_OK;
    atomic_store(&p->stopping, true);
    wake_worker(p);
#ifdef ESP_PLATFORM
    int64_t deadline = now_us() + (int64_t)timeout_ms * 1000;
    while (!atomic_load_explicit(&p->quiescent, memory_order_acquire) ||
           eTaskGetState(p->task) != eSuspended) {
        if (now_us() >= deadline) return ESP_GSP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    vTaskDeleteWithCaps(p->task);
    p->task = NULL;
#else
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += timeout_ms / 1000;
    until.tv_nsec += (long)(timeout_ms % 1000) * 1000000;
    if (until.tv_nsec >= 1000000000) { until.tv_nsec -= 1000000000; ++until.tv_sec; }
    int result = pthread_timedjoin_np(p->thread, NULL, &until);
    if (result == ETIMEDOUT) return ESP_GSP_ERR_TIMEOUT;
    if (result) return ESP_GSP_ERR_INVALID_STATE;
#endif
    p->stopped = true;
    p->pending_valid = false;
    for (unsigned i = 0; i < FRAME_COUNT; ++i)
        if (atomic_load(&p->frames[i].state) == READY) atomic_store(&p->frames[i].state, FREE);
    return ESP_GSP_OK;
}

esp_gsp_err_t echoy_art_producer_destroy(echoy_art_producer_t **owner)
{
    if (!owner) return ESP_GSP_ERR_INVALID_ARG;
    echoy_art_producer_t *p = *owner;
    if (!p) return ESP_GSP_OK;
    if (!p->stopped) return ESP_GSP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < FRAME_COUNT; ++i)
        if (atomic_load_explicit(&p->frames[i].state, memory_order_acquire) != FREE)
            return ESP_GSP_ERR_INVALID_STATE;
#ifndef ESP_PLATFORM
    if (sem_destroy(&p->wake) != 0) return ESP_GSP_ERR_INVALID_STATE;
#endif
    *owner = NULL;
    free_storage(p);
    return ESP_GSP_OK;
}

void echoy_art_producer_get_stats(const echoy_art_producer_t *p, echoy_art_producer_stats_t *out)
{
    if (!out) return;
    *out = (echoy_art_producer_stats_t){0};
    if (!p) return;
    out->rendered = atomic_load(&p->rendered);
    out->published = atomic_load(&p->published);
    out->stale = atomic_load(&p->stale);
    out->coalesced = atomic_load(&p->coalesced);
    out->draw_max_us = atomic_load(&p->draw_max_us);
    out->worker_stack_min = atomic_load(&p->worker_stack_min);
}
