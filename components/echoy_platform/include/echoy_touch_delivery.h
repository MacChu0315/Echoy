// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "echoy_touch.h"
typedef struct {
    bool cancel_pending;
    unsigned processed,dropped,cleanups;
} echoy_touch_delivery_t;
typedef struct {
    bool (*lock)(void *);
    void (*unlock)(void *);
    void (*pointer)(void *,echoy_pointer_t,int,int);
    void *ctx;
} echoy_touch_delivery_ops_t;
/* Single control-task owner, including all counters. lock includes checking
 * that the view exists. A drop retains cancel_pending until UI_CANCEL has
 * actually run under that lock. Synthetic cancellation increments cleanups,
 * not processed. flush retries even without another input event. */
void echoy_touch_delivery_send(echoy_touch_delivery_t *,const echoy_touch_delivery_ops_t *,echoy_pointer_t,int,int);
void echoy_touch_delivery_flush(echoy_touch_delivery_t *,const echoy_touch_delivery_ops_t *);
