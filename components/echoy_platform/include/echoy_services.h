// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "echoy_core.h"
#define ECHOY_SETTINGS_BYTES 64
#define ECHOY_SETTINGS_DEBOUNCE_US INT64_C(500000)
typedef struct { uint8_t role,phrase,locale; double volume,activity[4]; uint8_t phrases[4]; } echoy_settings_t;
typedef int (*echoy_store_read_fn)(void *,uint8_t *,size_t *);
typedef int (*echoy_store_write_fn)(void *,const uint8_t *,size_t);
typedef struct {
    echoy_settings_t saved,pending; bool dirty; int64_t due_us;
    unsigned failures; int last_error; void *ctx;
    echoy_store_read_fn read; echoy_store_write_fn write;
} echoy_storage_t;
void echoy_settings_defaults(echoy_settings_t *);
bool echoy_settings_valid(const echoy_settings_t *);
void echoy_settings_capture(echoy_settings_t *,const echoy_device_t *,bool english);
bool echoy_settings_restore(const echoy_settings_t *,echoy_device_t *);
bool echoy_storage_load(echoy_storage_t *);
bool echoy_storage_stage(echoy_storage_t *,const echoy_settings_t *,int64_t now);
bool echoy_storage_flush(echoy_storage_t *,int64_t now);
typedef struct { int percent,last_error; unsigned failures; bool valid,low,shutdown_requested; } echoy_battery_t;
bool echoy_battery_observe(echoy_battery_t *,int error,unsigned state_of_charge);
/* Each frame is immutable. A complete contiguous zero-to-size traversal allows
 * the next offset-zero read to refresh it; random access never refreshes it.
 * Backend serializes calls, and end releases all retained capture storage. */
typedef struct { uint8_t *pixels; size_t size,expected_offset; bool full_pass_complete; void *ctx;
    bool (*capture)(void *,uint8_t **,size_t *); void (*release)(void *,uint8_t *);
} echoy_mirror_t;
bool echoy_mirror_begin(echoy_mirror_t *);
bool echoy_mirror_read(echoy_mirror_t *,size_t offset,uint8_t *,size_t,size_t *);
void echoy_mirror_end(echoy_mirror_t *);
