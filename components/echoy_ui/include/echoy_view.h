// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "echoy_core.h"
#include "echoy_touch.h"
#include "lvgl.h"
typedef struct echoy_view echoy_view_t;
typedef void (*echoy_ui_emit_fn)(void *context,echoy_ui_event_t event);
/* All functions below are UI-thread-only, under the BSP display lock on MCU.
 * emit must enqueue only: no storage, radio, codec or core mutation in callback. */
echoy_view_t *echoy_view_create(lv_obj_t *screen,echoy_ui_emit_fn emit,void *context);
void echoy_view_set_sink(echoy_view_t *,echoy_ui_emit_fn,void *context);
/* Copies the immutable snapshot and presents newly elapsed audible events via
 * echoy_view_hit. The plan carries audible_since_us, so skipped UI snapshots
 * cannot replay a waiting/mute/fault window. Runtime and PC use this entry. */
void echoy_view_update(echoy_view_t *,const echoy_snapshot_t *,int device);
void echoy_view_locale(echoy_view_t *,bool english);
void echoy_view_route(echoy_view_t *,echoy_route_t);
void echoy_view_motion(echoy_view_t *,bool reduced_motion);
void echoy_view_hit(echoy_view_t *,const echoy_event_t *,int64_t at_us);
void echoy_view_frame(echoy_view_t *,int64_t at_us);
echoy_ui_event_t echoy_view_hit_test(const echoy_view_t *,int x,int y);
void echoy_view_pointer(echoy_view_t *,echoy_pointer_t,int x,int y);
void echoy_view_destroy(echoy_view_t *);
const lv_font_t *echoy_font(int size,bool bold);
const char *echoy_ui_text(const char *key,bool english);
void echoy_ui_feedback(const echoy_snapshot_t *,int device,bool english,char *out,size_t size);
