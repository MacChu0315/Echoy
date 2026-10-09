#define _POSIX_C_SOURCE 200809L
#include "board_health.h"
#include "bsp/esp_mosaico.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_t owner;
static pthread_mutex_t critical = PTHREAD_MUTEX_INITIALIZER, lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_ready = PTHREAD_COND_INITIALIZER;
static atomic_int workers, devices, semaphores;
static atomic_llong offset;
static atomic_uint give_delay_ms;
static int variant, fail_command = -1, fail_error, init_error, variant_error, add_error,
                    remove_error;
static int init_calls, reads, remove_calls, logs;
static bool block_init, init_entered, block_read, read_entered, fail_task;
static uint16_t registers[256];
static bool unplug_at_end;
static unsigned status_reads;

struct fake_device {
    unsigned address;
};
struct fake_semaphore {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool available, completion;
};

static void delay_ms(unsigned ms)
{
    struct timespec delay = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000};
    nanosleep(&delay, NULL);
}
int64_t esp_timer_get_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000 + atomic_load(&offset);
}
const char *esp_err_to_name(esp_err_t error)
{
    (void)error;
    return "injected error";
}
void fake_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
    pthread_mutex_lock(&lock);
    ++logs;
    pthread_mutex_unlock(&lock);
}
void fake_critical_enter(void)
{
    pthread_mutex_lock(&critical);
}
void fake_critical_exit(void)
{
    pthread_mutex_unlock(&critical);
}
SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    SemaphoreHandle_t s = calloc(1, sizeof(*s));
    assert(s);
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->changed, NULL);
    s->completion = (atomic_fetch_add(&semaphores, 1) % 2) == 1;
    return s;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    pthread_mutex_lock(&s->lock);
    bool previous = s->available;
    s->available = true;
    pthread_cond_signal(&s->changed);
    pthread_mutex_unlock(&s->lock);
    if (s->completion && !pthread_equal(owner, pthread_self()) && atomic_load(&give_delay_ms)) {
        /* Model the ESP-IDF SMP window: the waiter is awake while Give still
         * owns the RTOS object. Premature deletion is observable under ASan. */
        delay_ms(atomic_load(&give_delay_ms));
        pthread_mutex_lock(&s->lock);
        assert(s->completion);
        pthread_mutex_unlock(&s->lock);
    }
    return previous ? pdFALSE : pdTRUE;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t timeout)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += timeout / 1000;
    until.tv_nsec += (long)(timeout % 1000) * 1000000;
    if (until.tv_nsec >= 1000000000) {
        ++until.tv_sec;
        until.tv_nsec -= 1000000000;
    }
    pthread_mutex_lock(&s->lock);
    while (!s->available && timeout) {
        int error = timeout == portMAX_DELAY
                        ? pthread_cond_wait(&s->changed, &s->lock)
                        : pthread_cond_timedwait(&s->changed, &s->lock, &until);
        if (error == ETIMEDOUT) {
            break;
        }
        assert(error == 0);
    }
    bool available = s->available;
    s->available = false;
    pthread_mutex_unlock(&s->lock);
    return available ? pdTRUE : pdFALSE;
}
void vSemaphoreDelete(SemaphoreHandle_t s)
{
    pthread_cond_destroy(&s->changed);
    pthread_mutex_destroy(&s->lock);
    free(s);
    atomic_fetch_sub(&semaphores, 1);
}
struct task_start {
    TaskFunction_t entry;
    void *context;
};
static void *task_entry(void *arg)
{
    struct task_start start = *(struct task_start *)arg;
    free(arg);
    start.entry(start.context);
    assert(!"worker returned without vTaskDelete");
    return NULL;
}
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack, void *context,
                       UBaseType_t priority, TaskHandle_t *out)
{
    assert(!strcmp(name, "echoy_health") && stack >= 4096 && priority == 1 && !out);
    if (fail_task) {
        return pdFALSE;
    }
    struct task_start *start = malloc(sizeof(*start));
    assert(start);
    *start = (struct task_start){entry, context};
    pthread_t thread;
    atomic_fetch_add(&workers, 1);
    assert(pthread_create(&thread, NULL, task_entry, start) == 0);
    assert(pthread_detach(thread) == 0);
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task)
{
    assert(!task);
    atomic_fetch_sub(&workers, 1);
    pthread_exit(NULL);
}
void vTaskDelay(TickType_t ticks)
{
    delay_ms(ticks);
}
static void require_worker(void)
{
    assert(!pthread_equal(owner, pthread_self()));
}
esp_err_t bsp_board_variant_get(bsp_board_variant_t *out)
{
    require_worker();
    pthread_mutex_lock(&lock);
    *out = (bsp_board_variant_t)variant;
    int error = variant_error;
    pthread_mutex_unlock(&lock);
    return error;
}
esp_err_t bsp_battery_init(void)
{
    require_worker();
    pthread_mutex_lock(&lock);
    ++init_calls;
    init_entered = true;
    pthread_cond_broadcast(&io_ready);
    while (block_init) {
        pthread_cond_wait(&io_ready, &lock);
    }
    int error = init_error;
    pthread_mutex_unlock(&lock);
    return error;
}
i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    require_worker();
    return &registers;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *out)
{
    require_worker();
    assert(bus == &registers && config->device_address == 0x55 && config->scl_speed_hz == 400000 &&
           config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    pthread_mutex_lock(&lock);
    int error = add_error;
    pthread_mutex_unlock(&lock);
    if (error) {
        return error;
    }
    *out = calloc(1, sizeof(**out));
    assert(*out);
    (*out)->address = 0x55;
    atomic_fetch_add(&devices, 1);
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    require_worker();
    assert(device && device->address == 0x55);
    pthread_mutex_lock(&lock);
    ++remove_calls;
    int error = remove_error;
    remove_error = 0;
    pthread_mutex_unlock(&lock);
    if (error) {
        return error;
    }
    free(device);
    atomic_fetch_sub(&devices, 1);
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *write,
                                      size_t write_size, uint8_t *read, size_t read_size,
                                      int timeout)
{
    require_worker();
    assert(device && write_size == 1 && read_size == 2 && timeout == 100);
    assert(*write == 0x06 || *write == 0x08 || *write == 0x0a || *write == 0x0c || *write == 0x2c ||
           *write == 0x3a);
    pthread_mutex_lock(&lock);
    ++reads;
    read_entered = true;
    pthread_cond_broadcast(&io_ready);
    while (block_read) {
        pthread_cond_wait(&io_ready, &lock);
    }
    int error = *write == fail_command ? fail_error : 0;
    uint16_t value = registers[*write];
    if (*write == 0x0a && unplug_at_end && (++status_reads % 2) == 0) {
        value = 0;
    }
    if (error) {
        read[0] = read[1] = 0;
    } /* Vendor convenience getters would fabricate zero. */
    else {
        read[0] = value & 255;
        read[1] = value >> 8;
    }
    pthread_mutex_unlock(&lock);
    return error;
}

static board_health_raw_t good_raw(void)
{
    return (board_health_raw_t){.battery_status = 8,
                                .operation_status = 0x20,
                                .state_of_charge = 72,
                                .voltage_mv = 3800,
                                .current_raw = 0xff9c,
                                .temperature_dk = 2982};
}
static void pure_cases(void)
{
    board_health_raw_t raw = good_raw();
    board_health_snapshot_t sample, aged;
    assert(board_health_decode(&raw, ESP_OK, 0, &sample));
    assert(sample.valid && sample.percentage == 72 && sample.current_ma == -100 &&
           sample.voltage_mv == 3800);
    assert(fabsf(sample.temperature_c - 25.05f) < .001f);
    assert(!board_health_battery_empty(&sample));
    raw.state_of_charge = 0;
    assert(board_health_decode(&raw, ESP_OK, 100, &sample) && board_health_battery_empty(&sample));
    assert(board_health_with_age(&sample, 10000100, 10000000, &aged) &&
           board_health_battery_empty(&aged));
    assert(!board_health_with_age(&sample, 10000101, 10000000, &aged));
    assert(aged.stale && aged.percentage == -1 && !board_health_battery_empty(&aged));
    assert(!board_health_with_age(&sample, 99, 10000000, &aged));
    assert(aged.reason == BOARD_HEALTH_STALE);
    assert(board_health_decode(&raw, ESP_OK, 0, &sample));
    assert(!board_health_with_age(&sample, INT64_MAX, 10000000, &aged));
    for (unsigned soc = 0; soc <= UINT16_MAX; ++soc) {
        raw.state_of_charge = soc;
        assert(board_health_decode(&raw, ESP_OK, 1, &sample) == (soc <= 100));
        if (soc > 100) {
            assert(sample.percentage == -1 && sample.reason == BOARD_HEALTH_INVALID_SAMPLE);
        }
    }
    raw = good_raw();
    for (uint32_t current = 0; current <= UINT16_MAX; ++current) {
        raw.current_raw = current;
        assert(board_health_decode(&raw, ESP_OK, 1, &sample));
        assert(sample.current_ma ==
               (current >= 32768 ? (int32_t)current - 65536 : (int32_t)current));
    }
    raw = good_raw();
    raw.battery_status = 0;
    assert(!board_health_decode(&raw, ESP_OK, 1, &sample));
    assert(sample.present_known && !sample.present && sample.reason == BOARD_HEALTH_ABSENT &&
           sample.percentage == -1);
    raw = good_raw();
    raw.operation_status = 0;
    assert(!board_health_decode(&raw, ESP_OK, 1, &sample) &&
           sample.reason == BOARD_HEALTH_GAUGE_NOT_READY);
    raw.operation_status = 0x420;
    assert(!board_health_decode(&raw, ESP_OK, 1, &sample));
    for (unsigned index = 0; index < 6; ++index) {
        raw = good_raw();
        uint16_t *field[] = {&raw.battery_status, &raw.operation_status, &raw.voltage_mv,
                             &raw.voltage_mv,     &raw.temperature_dk,   &raw.temperature_dk};
        *field[index] = index == 2 || index == 4 ? 0 : UINT16_MAX;
        assert(!board_health_decode(&raw, ESP_OK, 1, &sample) && sample.percentage == -1);
    }
    raw = good_raw();
    raw.state_of_charge = 0;
    assert(!board_health_decode(&raw, ESP_ERR_TIMEOUT, 1, &sample));
    assert(sample.reason == BOARD_HEALTH_READ_ERROR && sample.io_error == ESP_ERR_TIMEOUT &&
           sample.percentage == -1);
    assert(!sample.present_known && !board_health_battery_empty(&sample));
    assert(!board_health_decode(NULL, ESP_OK, 1, &sample));
    assert(!board_health_decode(&raw, ESP_OK, -1, &sample));
    assert(!board_health_decode(&raw, ESP_OK, 1, NULL));
    assert(!board_health_with_age(NULL, 1, 1, &sample));
    assert(!board_health_with_age(&sample, 1, 0, &aged));
    assert(!board_health_battery_empty(NULL));
    raw = good_raw();
    assert(board_health_decode(&raw, ESP_OK, 1, &sample));
    assert(board_health_decode(&sample.raw, ESP_OK, 2, &sample)); /* input/output alias */
}

static void reset(void)
{
    for (int i = 0; i < 1000 && atomic_load(&workers); ++i) {
        delay_ms(1);
    }
    assert(!atomic_load(&workers) && !atomic_load(&devices) && !atomic_load(&semaphores));
    pthread_mutex_lock(&lock);
    variant = 0;
    fail_command = -1;
    fail_error = init_error = variant_error = add_error = remove_error = 0;
    init_calls = reads = remove_calls = logs = 0;
    block_init = init_entered = block_read = read_entered = fail_task = unplug_at_end = false;
    status_reads = 0;
    memset(registers, 0, sizeof(registers));
    registers[0x0a] = 8;
    registers[0x3a] = 0x20;
    registers[0x2c] = 72;
    registers[0x08] = 3800;
    registers[0x0c] = 0xff9c;
    registers[0x06] = 2982;
    atomic_store(&offset, 0);
    atomic_store(&give_delay_ms, 0);
    pthread_mutex_unlock(&lock);
}
static board_health_snapshot_t wait_reason(board_health_handle_t h, board_health_reason_t reason)
{
    board_health_snapshot_t s;
    for (int i = 0; i < 1500; ++i) {
        assert(board_health_get(h, &s) == ESP_OK);
        if (s.reason == reason) {
            return s;
        }
        delay_ms(1);
    }
    fprintf(stderr, "expected reason %d, actual %d io=%d\n", reason, s.reason, s.io_error);
    abort();
}
static void lifecycle_cases(void)
{
    board_health_config_t config = BOARD_HEALTH_CONFIG_DEFAULT();
    config.sample_period_ms = 100;
    board_health_handle_t h = NULL;
    reset();
    assert(board_health_start(NULL, NULL) == ESP_ERR_INVALID_ARG);
    board_health_config_t invalid = config;
    invalid.sample_period_ms = 0;
    assert(board_health_start(&invalid, &h) == ESP_ERR_INVALID_ARG && !h);
    fail_task = true;
    assert(board_health_start(&config, &h) == ESP_ERR_NO_MEM && !h);
    reset();
    block_init = true;
    assert(board_health_start(&config, &h) == ESP_OK);
    board_health_handle_t saved = h;
    assert(board_health_start(&config, &h) == ESP_ERR_INVALID_STATE && h == saved);
    board_health_snapshot_t s = wait_reason(h, BOARD_HEALTH_STARTING);
    assert(!s.valid && s.percentage == -1 && !s.present_known);
    pthread_mutex_lock(&lock);
    while (!init_entered) {
        pthread_cond_wait(&io_ready, &lock);
    }
    pthread_mutex_unlock(&lock);
    assert(board_health_get(h, &s) == ESP_OK && !s.valid); /* getter never waits for hardware */
    assert(board_health_stop(&h, 0) == ESP_ERR_TIMEOUT && h);
    assert(board_health_get(h, &s) == ESP_OK && s.reason == BOARD_HEALTH_STOPPING);
    pthread_mutex_lock(&lock);
    block_init = false;
    pthread_cond_broadcast(&io_ready);
    pthread_mutex_unlock(&lock);
    assert(board_health_stop(&h, 1000) == ESP_OK && !h);
    reset();
    init_error = ESP_ERR_TIMEOUT;
    assert(board_health_start(&config, &h) == ESP_OK);
    s = wait_reason(h, BOARD_HEALTH_INIT_ERROR);
    assert(s.io_error == ESP_ERR_TIMEOUT && s.percentage == -1);
    delay_ms(250);
    pthread_mutex_lock(&lock);
    assert(init_calls == 1 && reads == 0);
    pthread_mutex_unlock(&lock);
    assert(board_health_stop(&h, 1000) == ESP_OK);
    reset();
    variant_error = ESP_ERR_TIMEOUT;
    assert(board_health_start(&config, &h) == ESP_OK);
    s = wait_reason(h, BOARD_HEALTH_INIT_ERROR);
    assert(s.io_error == ESP_ERR_TIMEOUT && s.board_variant == BOARD_HEALTH_VARIANT_UNKNOWN);
    pthread_mutex_lock(&lock);
    assert(init_calls == 0);
    pthread_mutex_unlock(&lock);
    assert(board_health_stop(&h, 1000) == ESP_OK);
    reset();
    variant = 99;
    assert(board_health_start(&config, &h) == ESP_OK);
    s = wait_reason(h, BOARD_HEALTH_INIT_ERROR);
    assert(s.io_error == ESP_ERR_NOT_SUPPORTED);
    pthread_mutex_lock(&lock);
    assert(init_calls == 0);
    pthread_mutex_unlock(&lock);
    assert(board_health_stop(&h, 1000) == ESP_OK);
    for (int board = 0; board < 2; ++board) {
        reset();
        variant = board;
        assert(board_health_start(&config, &h) == ESP_OK);
        s = wait_reason(h, BOARD_HEALTH_VALID);
        assert(s.percentage == 72 && (int)s.board_variant == board);
        int64_t success = s.last_success_us;
        pthread_mutex_lock(&lock);
        fail_command = 0x2c;
        fail_error = ESP_ERR_TIMEOUT;
        pthread_mutex_unlock(&lock);
        s = wait_reason(h, BOARD_HEALTH_READ_ERROR);
        assert(s.io_error == ESP_ERR_TIMEOUT && s.failed_command == 0x2c && !s.valid &&
               s.percentage == -1);
        assert(s.last_success_us == success && !board_health_battery_empty(&s));
        pthread_mutex_lock(&lock);
        fail_command = -1;
        registers[0x2c] = 256;
        pthread_mutex_unlock(&lock);
        s = wait_reason(h, BOARD_HEALTH_INVALID_SAMPLE);
        assert(s.percentage == -1 && !board_health_battery_empty(&s));
        pthread_mutex_lock(&lock);
        registers[0x2c] = 0;
        pthread_mutex_unlock(&lock);
        s = wait_reason(h, BOARD_HEALTH_VALID);
        assert(board_health_battery_empty(&s));
        pthread_mutex_lock(&lock);
        block_read = true;
        read_entered = false;
        while (!read_entered) {
            pthread_cond_wait(&io_ready, &lock);
        }
        pthread_mutex_unlock(&lock);
        atomic_store(&offset, 11000000);
        assert(board_health_get(h, &s) == ESP_OK && s.stale && !s.valid && s.percentage == -1);
        assert(!board_health_battery_empty(&s));
        pthread_mutex_lock(&lock);
        block_read = false;
        registers[0x0a] = 0;
        pthread_cond_broadcast(&io_ready);
        pthread_mutex_unlock(&lock);
        s = wait_reason(h, BOARD_HEALTH_ABSENT);
        assert(s.present_known && !s.present && s.percentage == -1);
        pthread_mutex_lock(&lock);
        assert(init_calls == 1);
        pthread_mutex_unlock(&lock);
        assert(board_health_stop(&h, 1000) == ESP_OK && !h);
    }
    reset();
    unplug_at_end = true;
    assert(board_health_start(&config, &h) == ESP_OK);
    s = wait_reason(h, BOARD_HEALTH_ABSENT);
    assert(!s.valid && s.percentage == -1);
    assert(board_health_stop(&h, 1000) == ESP_OK);
    reset();
    remove_error = ESP_FAIL;
    assert(board_health_start(&config, &h) == ESP_OK);
    wait_reason(h, BOARD_HEALTH_VALID);
    assert(board_health_stop(&h, 1000) == ESP_FAIL && h && atomic_load(&devices) == 1);
    assert(board_health_stop(&h, 1000) == ESP_OK && !h);
    pthread_mutex_lock(&lock);
    assert(init_calls == 1 && remove_calls == 2);
    pthread_mutex_unlock(&lock);
    assert(board_health_stop(&h, 1000) == ESP_OK);
    reset();
    atomic_store(&give_delay_ms, 50);
    assert(board_health_start(&config, &h) == ESP_OK);
    wait_reason(h, BOARD_HEALTH_VALID);
    assert(board_health_stop(&h, 5) == ESP_ERR_TIMEOUT && h);
    assert(atomic_load(&semaphores) == 2);
    delay_ms(80);
    assert(board_health_stop(&h, 1000) == ESP_OK && !h);
    reset();
}
int main(void)
{
    owner = pthread_self();
    pure_cases();
    lifecycle_cases();
    puts(
        "PASS: 65536 SOC values, signed-current conversion, absence/not-ready/sentinels/staleness, "
        "checked-read errors, dedicated worker lifecycle and cleanup.");
    return 0;
}
