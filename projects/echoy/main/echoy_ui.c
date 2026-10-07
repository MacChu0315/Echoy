#include "echoy_ui.h"
#include "echoy_model.h"
#include "echoy_art.h"
#define GSP_BUNDLE_ENABLE_RAW_IDS
#include "bundle_gsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR_US INT64_C(2500000)
#define BEAT_US INT64_C(625000)
struct echoy_ui {
    esp_gsp_handle_t gsp;
    echoy_platform_t platform;
    echoy_model_handle_t model;
    void *timer;
    atomic_int error;
    atomic_uint art;
    bool events_registered, pointer_registered, draw_registered, feedback_registered;
    bool pressed;
    int drag, drag_y, drag_value, volume, richness;
    int64_t last_hit_us;
    char status[96], hint[224];
    int last_capture, last_busy, last_progress, last_phase;
    int last_step, last_progress_visible;
    uint32_t last_art;
};
static void record_error(echoy_ui_handle_t u, esp_gsp_err_t e, const char *operation, int line)
{
    if (e != ESP_GSP_OK && u->error == ESP_GSP_OK) {
        u->error = e;
        fprintf(stderr, "ECHOY_UI_ERROR: %d line=%d operation=%s\n", (int)e, line, operation);
        echoy_model_set_fault(u->model, true);
    }
}
#define check(ui, operation) record_error((ui), (operation), #operation, __LINE__)
static bool capture(echoy_phase_t p)
{
    return p == ECHOY_COUNT_IN || p == ECHOY_RECORDING;
}
static void update(echoy_ui_handle_t u)
{
    echoy_snapshot_t s;
    echoy_model_get_snapshot(u->model, &s);
    int64_t now = u->platform.now_us(u->platform.ctx);
    bool recording = capture(s.phase), busy = s.phase >= ECHOY_PREPARING;
    char status[96] = "先试着敲几下";
    const char *hint = "点「录一段」才记录节奏";
    if (s.phase == ECHOY_COUNT_IN) {
        int beat = (int)((s.capture_at_us - now + BEAT_US - 1) / BEAT_US);
        if (beat > 2) {
            beat = 2;
        }
        if (beat < 1) {
            beat = 1;
        }
        snprintf(status, sizeof(status), "预备 · %d", beat);
        hint = "开头可以留空 · 提示音不会录入";
    } else if (s.phase == ECHOY_RECORDING) {
        int beat = (int)((now - s.capture_at_us) / BEAT_US) + 1;
        if (beat > 4) {
            beat = 4;
        }
        snprintf(status, sizeof(status), "录入 · %d / 4", beat);
        hint = "开头可以留空 · 提示音不会录入";
    } else if (s.phase == ECHOY_PREPARING) {
        strcpy(status, "准备新的声音");
        hint = "原来的继续，可随时暂停本机";
    } else if (s.phase == ECHOY_PREVIEW) {
        strcpy(status, "先听这一次");
        hint = "结束就停 · 点 ▶ 才持续演奏";
    } else if (s.phase == ECHOY_PLAYING) {
        strcpy(status, "正在演奏");
        hint = now < s.start_us ? "等共同拍点开始" : "Ⅱ 只暂停本机，不改变节奏";
    } else if (s.seed) {
        strcpy(status, "这段留在这里了");
        hint = "▶ 开始演奏 · 不自动改写";
    }
    if (s.notice == ECHOY_NOTICE_CANCELLED) {
        hint = "已取消，原节奏保留";
    }
    if (s.notice == ECHOY_NOTICE_EMPTY) {
        hint = "没有录入，原节奏保留";
    }
    if (strcmp(status, u->status)) {
        check(u, gsp_echoy_status_set_text(u->gsp, status));
        check(u, gsp_echoy_capture_status_set_text(u->gsp, status));
        strcpy(u->status, status);
    }
    if (strcmp(hint, u->hint)) {
        check(u, gsp_echoy_hint_set_text(u->gsp, hint));
        strcpy(u->hint, hint);
    }
    if (u->last_capture != recording) {
        check(u, gsp_echoy_record_text_set_text(u->gsp, recording ? "取消" : "录一段"));
        check(u, gsp_echoy_record_text_set_visible(u->gsp, !recording));
        check(u, gsp_echoy_cancel_text_set_visible(u->gsp, recording));
        check(u, gsp_echoy_status_set_visible(u->gsp, !recording));
        check(u, gsp_echoy_capture_status_set_visible(u->gsp, recording));
        check(u, gsp_echoy_record_icon_set_visible(u->gsp, !recording));
        check(u, gsp_echoy_cancel_icon_set_visible(u->gsp, recording));
        check(u,
              gsp_echoy_record_surface_set_color_rgb888(u->gsp, recording ? 0xeee2d8 : 0xe5e9de));
        check(u, gsp_echoy_role_set_enabled(u->gsp, !recording));
        check(u, gsp_echoy_settings_set_enabled(u->gsp, !recording));
        u->last_capture = recording;
    }
    int mode = (recording ? 4 : 0) | (busy ? 2 : 0) | (s.seed ? 1 : 0);
    if (mode != u->last_busy) {
        check(u, gsp_echoy_transport_set_enabled(u->gsp, !recording && s.seed));
        check(u, gsp_echoy_transport_set_color_rgb888(u->gsp, busy ? 0x415936 : 0x596d4b));
        check(u, gsp_echoy_play_icon_set_visible(u->gsp, !busy));
        check(u, gsp_echoy_pause_icon_set_visible(u->gsp, busy));
        check(u, gsp_echoy_touch_hint_set_text(u->gsp, !s.seed && !recording ? "试奏区" : ""));
        check(u, gsp_echoy_pad_border_set_visible(u->gsp, !s.seed && !recording));
        u->last_busy = mode;
    }
    const bool recording_now = s.phase == ECHOY_RECORDING;
    const bool once = s.phase == ECHOY_PREVIEW;
    const bool playing_now = (once || s.phase == ECHOY_PLAYING) && now >= s.start_us;
    const bool progress_visible = recording_now || once;
    int64_t elapsed = recording_now ? now - s.capture_at_us : now - s.start_us;
    if (elapsed < 0) {
        elapsed = 0;
    }
    int progress = (int)(elapsed * 100 / BAR_US);
    if (progress < 0) {
        progress = 0;
    }
    if (progress > 100) {
        progress = 100;
    }
    if (progress_visible != u->last_progress_visible) {
        check(u, gsp_echoy_progress_set_visible(u->gsp, progress_visible));
        u->last_progress_visible = progress_visible;
    }
    if (progress_visible && progress != u->last_progress) {
        check(u, gsp_echoy_progress_set_value(u->gsp, progress));
        u->last_progress = progress;
    }
    uint16_t mask = recording ? s.pending_seed : s.seed;
    unsigned step = 16;
    if (recording_now || playing_now) {
        step = (unsigned)(elapsed % BAR_US / 156250);
    }
    if ((int)step != u->last_step) {
        if (step < 16) {
            if (mask & (1u << step)) {
                u->last_hit_us = now;
            }
        }
        u->last_step = (int)step;
    }
    int64_t since = now - u->last_hit_us;
    unsigned energy =
        since >= 0 && since < 400000 ? (unsigned)(127 * (400000 - since) / 400000) : 0;
    uint32_t art =
        mask | (step << 16) | (energy << 21) | ((!s.seed && !recording) ? (1u << 28) : 0);
    if (art != u->last_art) {
        atomic_store_explicit(&u->art, art, memory_order_relaxed);
        check(u, esp_gsp_canvas_invalidate(u->gsp, GSP_ECHOY_BIND_ART));
        check(u, esp_gsp_canvas_invalidate(u->gsp, GSP_ECHOY_BIND_FEEDBACK));
        u->last_art = art;
    }
    if ((int)s.phase != u->last_phase) {
        fprintf(stderr, "ECHOY_STATE phase=%d seed=%04x generation=%u at_us=%lld\n", (int)s.phase,
                s.seed, (unsigned)s.generation, (long long)now);
        u->last_phase = s.phase;
    }
}
static void tick(esp_gsp_handle_t gsp, void *ctx)
{
    (void)gsp;
    echoy_ui_handle_t u = ctx;
    const int64_t now = u->platform.now_us(u->platform.ctx);
    echoy_model_tick(u->model, now);
    echoy_snapshot_t s;
    echoy_model_get_snapshot(u->model, &s);
    /* Foundation only: a simulated output service completes preparation.
       Replace this explicit adapter boundary when the C audio engine is connected. */
    if (s.phase == ECHOY_PREPARING) {
        echoy_model_prepare_done(u->model, s.generation, now);
    }
    update(u);
}
static void pointer(esp_gsp_handle_t gsp, int32_t x, int32_t y, bool pressed, void *ctx)
{
    (void)gsp;
    echoy_ui_handle_t u = ctx;
    echoy_snapshot_t s;
    echoy_model_get_snapshot(u->model, &s);
    if (pressed && !u->pressed) {
        u->drag = 0;
        if (y >= 113 && y < 320 && (x >= 16 && x < 80)) {
            u->drag = 1;
            u->drag_value = u->volume;
            u->drag_y = y;
        } else if (y >= 113 && y < 320 && x >= 400 && x < 464 && !capture(s.phase)) {
            u->drag = 2;
            u->drag_value = u->richness;
            u->drag_y = y;
        } else if (x >= 90 && x < 390 && y >= 113 && y < 317) {
            int64_t now = u->platform.now_us(u->platform.ctx);
            if (echoy_model_tap(u->model, now)) {
                u->last_hit_us = now;
            }
        }
    }
    if (pressed && u->drag) {
        int value = u->drag_value + (int)lround((u->drag_y - y) * 100.0 / 180);
        if (value < 0) {
            value = 0;
        }
        if (value > 100) {
            value = 100;
        }
        int height = (int)lround(value * 111.0 / 100);
        if (u->drag == 1 && value != u->volume) {
            u->volume = value;
            check(u, gsp_echoy_volume_knob_set_y(u->gsp, 148 - height));
            check(u, gsp_echoy_volume_fill_set_y(u->gsp, 111 - height));
        }
        if (u->drag == 2 && !capture(s.phase) && value != u->richness) {
            u->richness = value;
            check(u, gsp_echoy_richness_knob_set_y(u->gsp, 148 - height));
            check(u, gsp_echoy_richness_fill_set_y(u->gsp, 111 - height));
        }
    }
    if (!pressed) {
        u->drag = 0;
    }
    u->pressed = pressed;
    update(u);
}
static void event(esp_gsp_handle_t gsp, const esp_gsp_event_t *event, void *ctx)
{
    (void)gsp;
    echoy_ui_handle_t u = ctx;
    int64_t now = u->platform.now_us(u->platform.ctx);
    if (gsp_echoy_event_is_record_click(event)) {
        echoy_model_record(u->model, now);
    } else if (gsp_echoy_event_is_transport_click(event)) {
        echoy_model_transport(u->model, now);
    }
    update(u);
}
esp_gsp_err_t echoy_ui_create(esp_gsp_handle_t gsp, const echoy_platform_t *platform,
                              echoy_ui_handle_t *out)
{
    if (!gsp || !platform || !platform->now_us || !out) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    *out = NULL;
    echoy_ui_handle_t u = calloc(1, sizeof(*u));
    if (!u) {
        return ESP_GSP_ERR_NO_MEM;
    }
    u->gsp = gsp;
    u->platform = *platform;
    u->volume = 65;
    u->richness = 50;
    u->last_hit_us = -1000000;
    u->last_capture = u->last_busy = u->last_progress = u->last_phase = -1;
    u->last_step = 16;
    u->last_progress_visible = -1;
    u->last_art = (16u << 16) | (1u << 28);
    atomic_init(&u->art, u->last_art);
    u->model = echoy_model_create();
    if (!u->model) {
        free(u);
        return ESP_GSP_ERR_NO_MEM;
    }
    atomic_init(&u->error, ESP_GSP_OK);
    esp_gsp_err_t err =
        esp_gsp_canvas_set_draw_cb(gsp, GSP_ECHOY_BIND_ART, echoy_art_draw, &u->art);
    u->draw_registered = err == ESP_GSP_OK;
    check(u, err);
    if (err == ESP_GSP_OK) {
        err =
            esp_gsp_canvas_set_draw_cb(gsp, GSP_ECHOY_BIND_FEEDBACK, echoy_feedback_draw, &u->art);
        u->feedback_registered = err == ESP_GSP_OK;
        check(u, err);
    }
    /* Canvas reads only the atomic art snapshot. Finish all model/UI cache
       initialization before exposing callbacks to the firmware render task. */
    if (err == ESP_GSP_OK) {
        update(u);
        err = atomic_load(&u->error);
    }
    if (err == ESP_GSP_OK) {
        err = esp_gsp_on_event(gsp, event, u);
        u->events_registered = err == ESP_GSP_OK;
    }
    if (err == ESP_GSP_OK) {
        err = esp_gsp_set_pointer_observer(gsp, pointer, u);
        u->pointer_registered = err == ESP_GSP_OK;
    }
    if (err == ESP_GSP_OK) {
        u->timer = esp_gsp_timer_create(gsp, 20, tick, u);
        if (!u->timer) {
            err = ESP_GSP_ERR_NO_MEM;
        }
    }
    if (err != ESP_GSP_OK) {
        if (echoy_ui_delete(u) != ESP_GSP_OK) {
            *out = u; /* Keep callback storage alive if shutdown cannot be confirmed. */
        }
        return err;
    }
    *out = u;
    return ESP_GSP_OK;
}
esp_gsp_err_t echoy_ui_delete(echoy_ui_handle_t u)
{
    if (!u) {
        return ESP_GSP_OK;
    }
    esp_gsp_err_t err;
    if (u->events_registered) {
        err = esp_gsp_on_event(u->gsp, NULL, NULL);
        if (err != ESP_GSP_OK) {
            return err;
        }
        u->events_registered = false;
    }
    if (u->pointer_registered) {
        err = esp_gsp_set_pointer_observer(u->gsp, NULL, NULL);
        if (err != ESP_GSP_OK) {
            return err;
        }
        u->pointer_registered = false;
    }
    if (u->timer) {
        err = esp_gsp_timer_delete(u->gsp, u->timer);
        if (err != ESP_GSP_OK) {
            return err;
        }
        u->timer = NULL;
    }
    if (u->draw_registered) {
        err = esp_gsp_canvas_stop(u->gsp, GSP_ECHOY_BIND_ART);
        if (err != ESP_GSP_OK) {
            return err;
        }
        u->draw_registered = false;
    }
    if (u->feedback_registered) {
        err = esp_gsp_canvas_stop(u->gsp, GSP_ECHOY_BIND_FEEDBACK);
        if (err != ESP_GSP_OK) {
            return err;
        }
        u->feedback_registered = false;
    }
    err = esp_gsp_flush(u->gsp, 1000);
    if (err != ESP_GSP_OK) {
        return err;
    }
    echoy_model_delete(u->model);
    free(u);
    return ESP_GSP_OK;
}
esp_gsp_err_t echoy_ui_get_error(echoy_ui_handle_t u)
{
    return u ? u->error : ESP_GSP_ERR_INVALID_ARG;
}
