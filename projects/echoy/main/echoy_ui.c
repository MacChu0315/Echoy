#include "echoy_ui.h"
#include "echoy_art.h"
#include "echoy_art_image.h"
#include "echoy_display.h"
#include "echoy_strip.h"
#include "echoy_strip_image.h"
#include "echoy_view_cache.h"
#define GSP_BUNDLE_ENABLE_RAW_IDS
#include "bundle_gsp.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define BAR_US INT64_C(2500000)
#define BEAT_US INT64_C(625000)
#define DRAW_US INT64_C(33333)
#define TRIAL_HINT_TAPS 3u
#define TRIAL_HINT_US INT64_C(3000000)
typedef enum { HOME, ROLE, SETTINGS, DISPLAY, GUIDE, INFO, STATUS, REMOVED } route_t;
static const char *const routes[] = {"home", "role_page", "settings_page", "display_page",
                                     "guide_page", "info_page", "status_page"};
static const char *const role_ids[] = {"pulse", "voice", "dust", "harmony"};
static const echoy_role_t next_role[] = {ECHOY_VOICE, ECHOY_DUST, ECHOY_HARMONY, ECHOY_PULSE};
static const echoy_role_t previous_role[] = {ECHOY_HARMONY, ECHOY_PULSE, ECHOY_VOICE, ECHOY_DUST};
static const uint32_t accents[] = {0xd95535, 0x496da1, 0x7b8452, 0xaa7b3d};
static const char *const phrases[4][3][2] = {
    {{"重音", "Accent"}, {"切分", "Syncopation"}, {"留白", "Space"}},
    {{"短句", "Short phrase"}, {"轻答", "Light reply"}, {"长线", "Long line"}},
    {{"颗粒", "Grains"}, {"点缀", "Touches"}, {"余韵", "Trails"}},
    {{"短和弦", "Short chords"}, {"分解", "Arpeggio"}, {"接句", "Answer"}}};
static const uint16_t canvases[] = {GSP_ECHOY_BIND_ART, GSP_ECHOY_BIND_FEEDBACK,
                                    GSP_ECHOY_BIND_ROLE_PREV_ART, GSP_ECHOY_BIND_ROLE_MAIN_ART,
                                    GSP_ECHOY_BIND_ROLE_NEXT_ART};
struct echoy_ui {
    esp_gsp_handle_t gsp;
    echoy_platform_t platform;
    echoy_view_cache_t view;
    echoy_model_handle_t model;
    echoy_control_view_t external;
    void *timer;
    atomic_int error, image_error;
    atomic_uint images_pending;
    bool events_registered, pointer_registered, canvas_registered[2];
    bool gallery_dirty, gallery_inflight, last_draw_animated;
    uint32_t gallery_submitted_revision;
    echoy_art_frame_t frames[5];
    struct echoy_art_workspace *art_workspace;
    echoy_plan_t capture_plan, gallery[3];
    route_t route;
    echoy_condition_t condition;
    bool english, removed, pressed;
    echoy_display_t display;
    int display_level;
    bool display_failed;
    unsigned battery;
    int drag, drag_y;
    double volume, richness, drag_value;
    bool density_dirty;
    int64_t density_due_us;
    int64_t last_hit_us, last_draw_us;
    unsigned trial_taps;
    int64_t record_hint_until_us;
#ifdef ESP_PLATFORM
    int64_t perf_tick_at, perf_log_at, perf_tick_max, perf_gap_max;
#endif
    int last_step, last_phase, last_route, last_condition, last_english;
    uint16_t last_capture_mask, last_gallery_seed;
    uint32_t last_gallery_revision, last_committed;
    bool frames_dirty;
    int last_scale, last_fade;
    echoy_strip_motion_t strip_motion;
    int strip_mode, strip_permissions, strip_glyph_alpha[4], strip_glyph_locale[4];
    uint32_t strip_border_rgb, strip_glyph_color[4];
    bool reduced_motion, strip_reduced;
    int64_t strip_frame_us;
};
static bool act(echoy_ui_handle_t u, echoy_action_t action, double value, int64_t now);
static bool capturing(echoy_phase_t p)
{
    return p == ECHOY_COUNT_IN || p == ECHOY_RECORDING;
}
static int64_t now_us(echoy_ui_handle_t u)
{
    return u->platform.now_us(u->platform.ctx);
}
static const char *tr(echoy_ui_handle_t u, const char *zh, const char *en)
{
    return u->english ? en : zh;
}
static void error_at(echoy_ui_handle_t u, esp_gsp_err_t error, const char *operation, int line)
{
    if (error != ESP_GSP_OK && atomic_load(&u->error) == ESP_GSP_OK) {
        atomic_store(&u->error, error);
        fprintf(stderr, "ECHOY_UI_ERROR: %d line=%d operation=%s\n", (int)error, line, operation);
        act(u, ECHOY_ACT_CONDITION, ECHOY_AUDIO_FAULT, now_us(u));
        u->condition = ECHOY_AUDIO_FAULT;
    }
}
#define CHECK(u, op) error_at((u), (op), #op, __LINE__)
#define SHOW(u, name, v) CHECK(u, echoy_view_show((u)->view, (name), (v)))
#define ENABLE(u, name, v) CHECK(u, echoy_view_enable((u)->view, (name), (v)))
#define TEXT(u, name, v) CHECK(u, echoy_view_text((u)->view, (name), (v)))
#define COLOR(u, name, v) CHECK(u, echoy_view_color((u)->view, (name), (v)))
static bool act(echoy_ui_handle_t u, echoy_action_t action, double value, int64_t now)
{
    if (u->platform.perform_action) {
        return u->platform.perform_action(u->platform.ctx, action, value, now);
    }
    switch (action) {
    case ECHOY_ACT_CANCEL_RECORD:
        return echoy_model_cancel_record(u->model);
    case ECHOY_ACT_RECORD:
        return echoy_model_record(u->model, now);
    case ECHOY_ACT_TAP:
        return echoy_model_tap(u->model, now);
    case ECHOY_ACT_PAUSE:
        echoy_model_pause(u->model);
        return true;
    case ECHOY_ACT_TRANSPORT:
        return echoy_model_transport(u->model, now);
    case ECHOY_ACT_AUDITION:
        return echoy_model_audition(u->model, now);
    case ECHOY_ACT_ROLE:
        return echoy_model_select_role(u->model, (echoy_role_t)value, now);
    case ECHOY_ACT_PHRASE:
        return echoy_model_select_phrase(u->model, (unsigned)value, now);
    case ECHOY_ACT_DENSITY:
        return echoy_model_set_density(u->model, value, now);
    case ECHOY_ACT_CONDITION:
        echoy_model_set_fault(u->model, value != ECHOY_NORMAL);
        return true;
    case ECHOY_ACT_VOLUME:
        return true;
    default:
        return false;
    }
}
static void read_state(echoy_ui_handle_t u, echoy_snapshot_t *out)
{
    if (!u->platform.read_control) {
        echoy_model_get_snapshot(u->model, out);
        return;
    }
    if (!u->platform.read_control(u->platform.ctx, &u->external)) {
        CHECK(u, ESP_GSP_ERR_INVALID_STATE);
    }
    if ((u->removed && u->external.present) ||
        (u->condition != ECHOY_NORMAL && u->external.condition == ECHOY_NORMAL)) {
        u->route = HOME;
    }
    if (u->english != u->external.english) {
        u->english = u->external.english;
    }
    u->removed = !u->external.present;
    u->condition = u->external.condition;
    u->battery = u->external.battery;
    u->volume = u->external.volume;
    u->reduced_motion = u->external.reduced_motion;
    if (!u->density_dirty) {
        u->richness = u->external.state.density;
    }
    *out = u->external.state;
}
static const echoy_plan_t *current_plan(echoy_ui_handle_t u)
{
    return u->platform.read_control ? &u->external.plan : echoy_model_get_plan(u->model);
}
static unsigned role_phrase(echoy_ui_handle_t u, echoy_role_t role)
{
    return u->platform.read_control ? u->external.memories[role]
                                    : echoy_model_get_role_phrase(u->model, role);
}
static void local_text(echoy_ui_handle_t u, const char *base, const char *value)
{
    char name[96];
    snprintf(name, sizeof(name), "%s%s", base, u->english ? "_en" : "");
    TEXT(u, name, value);
}
static uint16_t source_mask(const echoy_plan_t *p)
{
    uint16_t mask = 0;
    for (size_t i = 0; i < p->source_count; i++) {
        mask |= (uint16_t)(1u << (unsigned)p->source[i].step);
    }
    return mask;
}
static void select_route(echoy_ui_handle_t u, route_t route)
{
    u->route = route;
    u->frames_dirty = true;
}
static void recover(echoy_ui_handle_t u)
{
    u->condition = ECHOY_NORMAL;
    if (!u->battery) {
        u->battery = 72;
    }
    act(u, ECHOY_ACT_CONDITION, ECHOY_NORMAL, now_us(u));
    select_route(u, HOME);
}
static void set_condition(echoy_ui_handle_t u, echoy_condition_t condition)
{
    u->condition = condition;
    if (condition == ECHOY_BATTERY_EMPTY) {
        u->battery = 0;
    }
    act(u, ECHOY_ACT_CONDITION, condition, now_us(u));
    u->frames_dirty = true;
}
static void commit_density(echoy_ui_handle_t u, int64_t now)
{
    if (u->density_dirty) {
        act(u, ECHOY_ACT_DENSITY, u->richness, now);
        u->density_dirty = false;
        u->frames_dirty = true;
    }
}
static void prepare_graphics(echoy_ui_handle_t u, const echoy_snapshot_t *s, int64_t now)
{
    const echoy_plan_t *plan = current_plan(u);
    bool cap = capturing(s->phase);
    if (cap && (u->last_capture_mask != s->pending_seed || u->capture_plan.role != s->role ||
                u->capture_plan.phrase != s->phrase || u->capture_plan.density != s->density)) {
        if (!echoy_compose_mask(s->pending_seed, s->role, s->phrase, s->density,
                                &u->capture_plan)) {
            CHECK(u, ESP_GSP_ERR_INVALID_STATE);
            return;
        }
        u->last_capture_mask = s->pending_seed;
        u->frames_dirty = true;
    }
    const echoy_plan_t *draw = cap ? &u->capture_plan : plan;
    double progress;
    if (s->phase == ECHOY_RECORDING) {
        progress = (now - s->capture_at_us) / (double)BAR_US;
    } else if (s->phase == ECHOY_PREVIEW) {
        progress = (now - s->start_us) / (double)BAR_US;
    } else {
        progress = ((now - s->clock_origin_us) % BAR_US) / (double)BAR_US;
    }
    bool active = s->phase == ECHOY_RECORDING ||
                  ((s->phase == ECHOY_PREVIEW || s->phase == ECHOY_PLAYING) && now >= s->start_us);
    if (progress < 0) {
        progress = 0;
    }
    if (progress > 1) {
        progress = 1;
    }
    int step = active ? (int)fmin(15, floor(progress * 16)) : 16;
    if (step != u->last_step) {
        if (active) {
            for (size_t i = 0; i < draw->count; i++) {
                const echoy_note_t *note = &draw->events[i];
                if ((!cap || note->layer == ECHOY_ANCHOR) && (int)floor(note->step) == step) {
                    u->last_hit_us = now;
                    break;
                }
            }
        }
        u->last_step = step;
    }
    float energy = now >= u->last_hit_us && now - u->last_hit_us < 400000
                       ? 1 - (now - u->last_hit_us) / 400000.0f
                       : 0;
    u->frames[0] = (echoy_art_frame_t){.plan = draw,
                                       .workspace = u->art_workspace,
                                       .role = cap ? s->role : plan->role,
                                       .progress = progress,
                                       .active = active,
                                       .energy = energy,
                                       .empty = !plan->count && !cap,
                                       .anchors_only = cap,
                                       .width = 300,
                                       .height = 204,
                                       .background = 0xf1efe8};
    u->frames[1] = u->frames[0];
    u->frames[1].height = 15;
    uint16_t seed = plan->source_count ? source_mask(plan) : s->seed;
    if (u->last_gallery_revision != s->revision || u->last_gallery_seed != seed) {
        echoy_role_t roles[] = {previous_role[s->role], s->role, next_role[s->role]};
        for (unsigned i = 0; i < 3; i++) {
            if (!echoy_compose_mask(seed, roles[i], role_phrase(u, roles[i]), s->density,
                                    &u->gallery[i])) {
                CHECK(u, ESP_GSP_ERR_INVALID_STATE);
                return;
            }
            u->frames[i + 2] = (echoy_art_frame_t){.plan = &u->gallery[i],
                                                   .workspace = u->art_workspace,
                                                   .role = roles[i],
                                                   .width = i == 1 ? 206 : 73,
                                                   .height = i == 1 ? 101 : 48,
                                                   .background = i == 1 ? 0xe5e7de : 0xecefe4};
        }
        u->gallery_dirty = true;
        u->last_gallery_revision = s->revision;
        u->last_gallery_seed = seed;
        u->frames_dirty = true;
    }
}
/* Decoder callbacks may run on another thread. Only atomics cross that boundary. */
static void gallery_complete(esp_gsp_handle_t gsp, uint16_t bind, gsp_err_t status, void *ctx)
{
    (void)gsp;
    (void)bind;
    echoy_ui_handle_t u = ctx;
    if (status != GSP_OK && status != GSP_ERR_CANCELLED) {
        atomic_store(&u->image_error, status);
    }
    atomic_fetch_sub_explicit(&u->images_pending, 1, memory_order_release);
}
static void publish_gallery(echoy_ui_handle_t u)
{
    /* Back-pressure: finish the previous batch; the next batch uses latest state. */
    if (!u->gallery_dirty || atomic_load_explicit(&u->images_pending, memory_order_acquire)) {
        return;
    }
#ifdef ESP_PLATFORM
    int64_t gallery_start = now_us(u);
#endif
    for (unsigned i = 2; i < 5; i++) {
        uint8_t *encoded = NULL;
        size_t size = 0;
        if (!echoy_art_encode_qoi(&u->frames[i], &encoded, &size)) {
            CHECK(u, ESP_GSP_ERR_NO_MEM);
            return;
        }
        const esp_gsp_image_options_t options = {
            .ownership = ESP_GSP_IMAGE_COPY, .on_complete = gallery_complete, .complete_ctx = u};
        atomic_fetch_add(&u->images_pending, 1);
        esp_gsp_err_t err = esp_gsp_set_image_ex(u->gsp, canvases[i], encoded, size, &options);
        free(encoded);
        if (err != ESP_GSP_OK) {
            atomic_fetch_sub(&u->images_pending, 1);
            CHECK(u, err);
            return;
        }
    }
#ifdef ESP_PLATFORM
    fprintf(stderr, "ECHOY_GALLERY_COST role=%u us=%lld\n", (unsigned)u->frames[3].role,
            (long long)(now_us(u)-gallery_start));
#endif
    u->gallery_dirty = false;
    u->gallery_inflight = true;
    u->gallery_submitted_revision = u->last_gallery_revision;
}
static void show_metadata(echoy_ui_handle_t u, const echoy_snapshot_t *s)
{
    const echoy_plan_t *plan = current_plan(u);
    echoy_role_t actual = plan->count ? plan->role : s->role;
    route_t route = u->removed ? REMOVED : u->condition != ECHOY_NORMAL ? STATUS : u->route;
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        SHOW(u, routes[i], route == (route_t)i);
    }
    static const char *const locale_groups[] = {
        "home", "role", "settings_header", "settings", "display_header", "display",
        "guide_header", "guide", "info_header", "info", "status_page", "role_footer",
        "settings_footer", "record_hint"};
    char name[96];
    for (size_t i = 0; i < sizeof(locale_groups) / sizeof(locale_groups[0]); i++) {
        snprintf(name, sizeof(name), "%s_zh", locale_groups[i]);
        SHOW(u, name, !u->english);
        snprintf(name, sizeof(name), "%s_en", locale_groups[i]);
        SHOW(u, name, u->english);
    }
    for (unsigned r = 0; r < 4; r++) {
        snprintf(name, sizeof(name), "home_title_%s", role_ids[r]);
        SHOW(u, name, r == actual);
        snprintf(name, sizeof(name), "home_title_%s_en", role_ids[r]);
        SHOW(u, name, r == actual);
        snprintf(name, sizeof(name), "role_copy_%s_zh", role_ids[r]);
        SHOW(u, name, r == s->role);
        snprintf(name, sizeof(name), "role_copy_%s_en", role_ids[r]);
        SHOW(u, name, r == s->role);
    }
    local_text(u, "phrase_title",
               phrases[actual][(plan->count ? plan->phrase : s->phrase) - 1][u->english]);
    static const int phrase_x_en[4] = {117, 115, 107, 155};
    static const int separator_x_en[4] = {103, 102, 93, 142};
    CHECK(u, echoy_view_x(u->view, "identity_separator_en", separator_x_en[actual]));
    CHECK(u, echoy_view_x(u->view, "phrase_title_en", phrase_x_en[actual]));
    COLOR(u, "richness_fill", accents[actual]);
    COLOR(u, "richness_knob", accents[actual]);
    COLOR(u, "equal", accents[actual]);
    for (unsigned p = 1; p <= 3; p++) {
        snprintf(name, sizeof(name), "phrase_%u_selected", p);
        SHOW(u, name, p == s->phrase);
        snprintf(name, sizeof(name), "phrase_%u_selected_surface", p);
        COLOR(u, name, accents[s->role]);
        for (unsigned b = 0; b < 5; b++) {
            snprintf(name, sizeof(name), "phrase_%u_bar_%u", p, b);
            COLOR(u, name, accents[s->role]);
        }
        snprintf(name, sizeof(name), "phrase_%u_label_%s_%s", p, role_ids[s->role],
                 u->english ? "en" : "zh");
        COLOR(u, name, p == s->phrase ? accents[s->role] : 0x637555);
    }
    SHOW(u, "language_zh_selected", !u->english);
    SHOW(u, "language_en_selected", u->english);
    char battery[12];
    const bool battery_unknown = u->platform.read_control && u->external.battery_unknown;
    if (battery_unknown) {
        snprintf(battery, sizeof(battery), "--%%");
    } else {
        snprintf(battery, sizeof(battery), "%u%%", u->battery);
    }
    TEXT(u, "battery_text", battery);
    SHOW(u, "battery_fill", !battery_unknown);
    const char *headers[] = {"role", "settings", "display", "guide", "info"};
    for (unsigned i = 0; i < sizeof(headers) / sizeof(headers[0]); i++) {
        snprintf(name, sizeof(name), "%s_battery_text", headers[i]);
        TEXT(u, name, battery);
        snprintf(name, sizeof(name), "%s_battery_fill", headers[i]);
        SHOW(u, name, !battery_unknown);
    }
    local_text(u, "info_value_platform",
               u->platform.is_simulator ? tr(u, "电脑模拟", "Simulator") : "Mosaico");
    local_text(u, "info_value_version", u->platform.software_version
                                            ? u->platform.software_version
                                            : tr(u, "本地构建", "Local build"));
    local_text(u, "info_label_battery", u->platform.is_simulator
                                            ? tr(u, "模拟电量", "Simulated battery")
                                            : tr(u, "电量", "Battery"));
    local_text(u, "info_value_battery", battery);
}
static uint32_t blend(uint32_t foreground, double alpha)
{
    const uint32_t background = 0xe7ecdf;
    uint32_t result = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        double fg = (foreground >> shift) & 255, bg = (background >> shift) & 255;
        result |= (uint32_t)lround(bg + (fg - bg) * alpha) << shift;
    }
    return result;
}
static void strip_glyph(echoy_ui_handle_t u, unsigned kind, unsigned alpha, uint32_t color)
{
    static const char *names[] = {"strip_record_glyph", "strip_play_glyph", "strip_cancel_glyph",
                                  "strip_pause_glyph"};
    static const uint16_t binds[] = {
        GSP_ECHOY_BIND_STRIP_RECORD_GLYPH, GSP_ECHOY_BIND_STRIP_PLAY_GLYPH,
        GSP_ECHOY_BIND_STRIP_CANCEL_GLYPH, GSP_ECHOY_BIND_STRIP_PAUSE_GLYPH};
    SHOW(u, names[kind], alpha != 0);
    if (!alpha ||
        (u->strip_glyph_alpha[kind] == (int)alpha &&
         u->strip_glyph_locale[kind] == (int)u->english && u->strip_glyph_color[kind] == color)) {
        return;
    }
    uint8_t *encoded = NULL;
    size_t size = 0;
    if (!echoy_strip_glyph_qoi(u->english, kind, (uint8_t)alpha, color, &encoded, &size)) {
        CHECK(u, ESP_GSP_ERR_NO_MEM);
        return;
    }
    esp_gsp_image_options_t options = {
        .ownership = ESP_GSP_IMAGE_COPY, .on_complete = gallery_complete, .complete_ctx = u};
    atomic_fetch_add(&u->images_pending, 1);
    esp_gsp_err_t error = esp_gsp_set_image_ex(u->gsp, binds[kind], encoded, size, &options);
    free(encoded);
    if (error != ESP_GSP_OK) {
        atomic_fetch_sub(&u->images_pending, 1);
        CHECK(u, error);
        return;
    }
    u->strip_glyph_alpha[kind] = (int)alpha;
    u->strip_glyph_locale[kind] = u->english;
    u->strip_glyph_color[kind] = color;
}
static void show_strip(echoy_ui_handle_t u, const echoy_snapshot_t *s, int64_t now)
{
    bool preparing = u->density_dirty || (u->platform.read_control && u->external.preparing);
    echoy_strip_state_t v = echoy_strip_compute(s, current_plan(u)->count != 0, preparing);
    static const char *mode_names[] = {"idle", "cancel", "pause", "record_only"};
    int permissions =
        (int)v.record_enabled | ((int)v.play_enabled << 1) | ((int)v.single_enabled << 2);
    bool changed = u->strip_mode != (int)v.mode, preference = u->strip_reduced != u->reduced_motion;
    int tile = u->platform.read_control ? u->external.tile : 0;
    if (changed || preference) {
        fprintf(stderr, "ECHOY_STRIP tile=%c mode=%s at_us=%lld duration_ms=%d reduced_motion=%d\n",
                'A' + tile, mode_names[v.mode], (long long)now, u->reduced_motion ? 0 : 240,
                u->reduced_motion);
        if (u->strip_mode < 0) {
            echoy_strip_motion_init(&u->strip_motion, v.mode, now);
        }
        echoy_strip_motion_to(&u->strip_motion, v.mode, u->reduced_motion, now);
    }
    if (changed || permissions != u->strip_permissions) {
        /* Use separate Record and Cancel/Pause targets so a delayed Cancel
         * event cannot start a new recording after returning to an empty tile.
         * Hidden targets must not intercept the visible control underneath. */
        if (v.mode == ECHOY_STRIP_IDLE || v.mode == ECHOY_STRIP_RECORD_ONLY) {
            SHOW(u, "strip_action", false);
            ENABLE(u, "strip_action", false);
        } else {
            ENABLE(u, "strip_action", v.single_enabled);
            SHOW(u, "strip_action", true);
        }
        ENABLE(u, "record", v.record_enabled);
        ENABLE(u, "record_empty", v.record_enabled);
        ENABLE(u, "transport", v.play_enabled);
        SHOW(u, "record", v.mode == ECHOY_STRIP_IDLE);
        SHOW(u, "record_empty", v.mode == ECHOY_STRIP_RECORD_ONLY);
        SHOW(u, "transport", v.mode == ECHOY_STRIP_IDLE);
        fprintf(stderr,
                "ECHOY_STRIP_READY tile=%c mode=%s record=%d play=%d single=%d at_us=%lld\n",
                'A' + tile, mode_names[v.mode], v.record_enabled, v.play_enabled, v.single_enabled,
                (long long)now_us(u));
        u->strip_permissions = permissions;
    }
    u->strip_mode = v.mode;
    u->strip_reduced = u->reduced_motion;
    echoy_strip_frame_t f;
    echoy_strip_motion_frame(&u->strip_motion, now, &f);
    if (!changed && !preference && now - u->strip_frame_us < 20000) {
        return;
    }
    CHECK(u, echoy_view_x(u->view, "strip_record_tint", (int)lround(f.boundary) - 244));
    CHECK(u, echoy_view_x(u->view, "strip_play_tint", (int)lround(f.boundary)));
    COLOR(u, "strip_record_tint", blend(f.record_rgb, f.record_alpha));
    COLOR(u, "strip_play_tint", blend(f.play_rgb, f.play_alpha));
    CHECK(u, echoy_view_opacity(u->view, "strip_divider", (uint8_t)lround(255 * f.divider_alpha)));
    if (u->strip_border_rgb != f.border_rgb) {
        CHECK(u, gsp_echoy_strip_border_set_tint_rgb888(u->gsp, f.border_rgb));
        u->strip_border_rgb = f.border_rgb;
    }
    CHECK(u, echoy_view_x(u->view, "strip_record_glyph", 131 - (int)lround(f.shift)));
    CHECK(u, echoy_view_x(u->view, "strip_play_glyph", 253 + (int)lround(f.shift)));
    CHECK(u, echoy_view_scale(u->view, "strip_cancel_glyph", (uint32_t)lround(f.scale * 65536)));
    CHECK(u, echoy_view_scale(u->view, "strip_pause_glyph", (uint32_t)lround(f.scale * 65536)));
    /* v4's more-specific idle glyph rule overrides button:disabled opacity.
     * Disabled halves retain full opacity while their real input stays blocked. */
    strip_glyph(u, 0, (unsigned)lround(255 * f.idle_alpha), 0xaa6140);
    strip_glyph(u, 1, (unsigned)lround(255 * f.play_glyph_alpha), 0x536648);
    strip_glyph(u, 2, v.mode == ECHOY_STRIP_CANCEL ? (unsigned)lround(255 * f.single_alpha) : 0,
                0xa36144);
    /* v4 replaces the single button content with Pause even while returning
     * to idle; it fades out in the default orange, not the active white. */
    strip_glyph(u, 3, v.mode != ECHOY_STRIP_CANCEL ? (unsigned)lround(255 * f.single_alpha) : 0,
                v.mode == ECHOY_STRIP_PAUSE ? 0xf8f8ee : 0xa36144);
    u->strip_frame_us = now;
}
static void show_home_state(echoy_ui_handle_t u, const echoy_snapshot_t *s, int64_t now)
{
    const echoy_plan_t *plan = current_plan(u);
    bool cap = capturing(s->phase), busy = s->phase >= ECHOY_PREPARING;
    echoy_cooperation_t cooperation = u->platform.read_control ? u->external.cooperation
                                                              : ECHOY_COOP_SOLO;
    /* Once Record has been used or a phrase exists, first-use guidance is done. */
    if (plan->count || s->phase != ECHOY_IDLE) {
        u->trial_taps = TRIAL_HINT_TAPS;
        u->record_hint_until_us = 0;
    }
    if (s->following) {
        u->record_hint_until_us = 0;
    }
    bool hint = !plan->count && s->phase == ECHOY_IDLE && !s->following &&
                !s->plan_pending && !u->density_dirty &&
                !(u->platform.read_control && u->external.preparing) &&
                now < u->record_hint_until_us;
    SHOW(u, "record_hint_popup", hint);
    char status[192] = {0};
    if (cap) {
        if (s->phase == ECHOY_COUNT_IN) {
            int remaining = (int)((s->capture_at_us - now + ECHOY_COUNT_IN_STEP_US - 1) /
                                  ECHOY_COUNT_IN_STEP_US);
            if (remaining > ECHOY_COUNT_IN_STEPS) {
                remaining = ECHOY_COUNT_IN_STEPS;
            }
            if (remaining < 1) {
                remaining = 1;
            }
            snprintf(status, sizeof(status), u->english ? "Ready · %d" : "预备-%d", remaining);
        } else {
            int beat = (int)((now - s->capture_at_us) / BEAT_US) + 1;
            if (beat > 4) {
                beat = 4;
            }
            snprintf(status, sizeof(status), u->english ? "Recording · %d / 4" : "录入 · %d / 4",
                     beat);
        }
    } else if (u->density_dirty || (u->platform.read_control && u->external.preparing)) {
        strcpy(status, tr(u, "准备新的声音", "Preparing a new sound"));
    } else if (s->plan_pending) {
        strcpy(status, tr(u, "下一小节生效", "Changes at the next bar"));
    } else if (s->phase == ECHOY_PREVIEW) {
        strcpy(status, now < s->start_us
                           ? tr(u, "等共同拍点试听", "Audition at the shared beat")
                           : tr(u, "试听一次 · 结束自动停", "One reply · Stops at the end"));
    } else if (s->phase == ECHOY_PLAYING && now < s->start_us) {
        strcpy(status, tr(u, "等共同拍点开始", "Waiting for the shared beat"));
    } else if (!plan->count && cooperation != ECHOY_COOP_WAITING &&
               cooperation != ECHOY_COOP_FOLLOWING && cooperation != ECHOY_COOP_RECOVERING) {
        strcpy(status, tr(u, "中央试奏 · 录制才保存", "Tap to try · Record to keep"));
    }
    if (!cap && s->notice == ECHOY_NOTICE_CANCELLED) {
        strcpy(status, tr(u, "已取消，原节奏保留", "Cancelled; original rhythm kept"));
    }
    if (!cap && s->notice == ECHOY_NOTICE_EMPTY) {
        strcpy(status, tr(u, "没有录入，原节奏保留", "No notes captured; original kept"));
    }
    if (!cap && u->platform.read_control && u->external.preparation_failed) {
        strcpy(status, tr(u, "新声音没有准备好，原来的继续",
                          "Preparation failed; current sound continues"));
    }
    /* The existing status line carries both the local operation and the link.
     * Never replace capture progress or a cancel/failure notice with link text. */
    const char *link_status = NULL;
    switch (cooperation) {
    case ECHOY_COOP_CONNECTED:
        link_status = tr(u, "已连接", "Connected");
        break;
    case ECHOY_COOP_WAITING:
        link_status = plan->count ? tr(u, "已连接 · 等待左侧乐句", "Connected · Waiting for left phrase")
                                  : tr(u, "已连接 · 等待左侧录制", "Connected · Waiting for left recording");
        break;
    case ECHOY_COOP_DEFERRED:
        link_status = s->phase == ECHOY_PREVIEW
                          ? tr(u, "结束后加入", "Join when done")
                          : tr(u, "录完后加入", "Join after recording");
        break;
    case ECHOY_COOP_FOLLOWING:
        link_status = tr(u, "跟随左侧", "Following left");
        break;
    case ECHOY_COOP_RECOVERING:
        link_status = tr(u, "连接恢复中", "Reconnecting");
        break;
    case ECHOY_COOP_SOLO:
        break;
    }
    if (link_status) {
        size_t used = strlen(status);
        snprintf(status + used, sizeof(status) - used, "%s%s", used ? " · " : "", link_status);
    }
    if (status[0]) {
        local_text(u, "status", status);
    }
    SHOW(u, "status", status[0] && !cap);
    SHOW(u, "status_en", status[0] && !cap);
    SHOW(u, "touch_hint_group", !plan->count && !cap);
    SHOW(u, "touch_hint_group_en", !plan->count && !cap);
    for (unsigned r = 0; r < 4; r++) {
        for (unsigned en = 0; en < 2; en++) {
            char key[96];
            snprintf(key, sizeof(key), "capture_status%s%s",
                     r == ECHOY_PULSE   ? ""
                     : r == ECHOY_VOICE ? "_voice"
                     : r == ECHOY_DUST  ? "_dust"
                                        : "_harmony",
                     en ? "_en" : "");
            bool selected = cap && r == (plan->count ? plan->role : s->role) && en == u->english;
            if (selected) {
                TEXT(u, key, status);
            }
            SHOW(u, key, selected);
        }
    }
    ENABLE(u, "role", !cap);
    ENABLE(u, "settings", !cap);
    SHOW(u, "pad_border", !plan->count && !cap);
    ENABLE(u, "gallery_transport",
           !cap && (busy || (plan->count && !s->plan_pending && !u->density_dirty)));
    SHOW(u, "gallery_play", !busy);
    SHOW(u, "gallery_pause", busy);
    local_text(u, "gallery_text",
               busy ? tr(u, "暂停本机", "Pause this tile") : tr(u, "试听一次", "Hear once"));
    bool progress = s->phase == ECHOY_RECORDING || s->phase == ECHOY_PREVIEW;
    echoy_role_t actual = plan->count ? plan->role : s->role;
    for (unsigned r = 0; r < 4; r++) {
        char key[40];
        snprintf(key, sizeof(key), r ? "progress_%s" : "progress", role_ids[r]);
        SHOW(u, key, progress && r == actual);
    }
    if (progress) {
        int64_t from = s->phase == ECHOY_RECORDING ? s->capture_at_us : s->start_us;
        int value = (int)((now - from) * 100 / BAR_US);
        if (value < 0) {
            value = 0;
        }
        if (value > 100) {
            value = 100;
        }
        char key[40];
        snprintf(key, sizeof(key), actual ? "progress_%s" : "progress", role_ids[actual]);
        CHECK(u, echoy_view_value(u->view, key, value));
    }
}
static void show_display(echoy_ui_handle_t u, const echoy_snapshot_t *s, int64_t now)
{
    bool active = u->pressed || capturing(s->phase) || s->phase == ECHOY_PREPARING ||
                  s->phase == ECHOY_PREVIEW || s->phase == ECHOY_PLAYING || s->plan_pending ||
                  (u->platform.read_control && u->external.preparing);
    unsigned level = echoy_display_level(&u->display, now, active);
    if (u->display_level != (int)level) {
        u->display_failed = u->platform.set_brightness &&
                            !u->platform.set_brightness(u->platform.ctx, level);
        if (!u->platform.set_brightness && u->platform.is_simulator) {
            CHECK(u, echoy_view_opacity(u->view, "display_shade", (100 - level) * 255 / 100));
        }
        u->display_level = (int)level;
        fprintf(stderr, "ECHOY_DISPLAY brightness=%u effective=%u auto_dim=%d ok=%d\n",
                u->display.brightness, level, u->display.auto_dim, !u->display_failed);
    }
    char percent[8];
    snprintf(percent, sizeof(percent), "%u%%", u->display.brightness);
    local_text(u, "display_percent", percent);
    CHECK(u, echoy_view_x(u->view, "display_knob", 48 + (u->display.brightness - 20) * 356 / 80));
    CHECK(u, echoy_view_x(u->view, "display_switch_knob", u->display.auto_dim ? 378 : 345));
    COLOR(u, "display_switch", u->display.auto_dim ? 0x718661 : 0xb6c1aa);
    local_text(u, "display_note_1",
               u->display_failed
                   ? tr(u, "亮度调整未成功，请再试一次。", "Could not set brightness. Please try again.")
                   : tr(u, "静止 1 分钟后调暗，触摸恢复。", "Dims after 1 minute. Touch to brighten."));
}
static void refresh(echoy_ui_handle_t u, int64_t now, bool force)
{
    if (u->gallery_inflight && !atomic_load_explicit(&u->images_pending, memory_order_acquire)) {
        if (atomic_load(&u->image_error) == GSP_OK) {
            fprintf(stderr, "ECHOY_GALLERY revision=%u\n", (unsigned)u->gallery_submitted_revision);
        }
        u->gallery_inflight = false;
    }
    echoy_snapshot_t s;
    read_state(u, &s);
    if (s.fault && u->condition == ECHOY_NORMAL) {
        u->condition = ECHOY_AUDIO_FAULT;
    }
    show_display(u, &s, now);
    show_strip(u, &s, now);
    prepare_graphics(u, &s, now);
    u->frames_dirty |= s.committed_revision != u->last_committed;
    show_metadata(u, &s);
    show_home_state(u, &s, now);
    int height = (int)lround(u->volume * 111);
    CHECK(u, echoy_view_y(u->view, "volume_knob", 148 - height));
    CHECK(u, echoy_view_y(u->view, "volume_fill", 111 - height));
    height = (int)lround(u->richness * 111);
    CHECK(u, echoy_view_y(u->view, "richness_knob", 148 - height));
    CHECK(u, echoy_view_y(u->view, "richness_fill", 111 - height));
    if (u->condition != ECHOY_NORMAL) {
        local_text(u, "problem_title",
                   u->condition == ECHOY_AUDIO_FAULT ? tr(u, "暂时无法发声", "Audio unavailable")
                   : u->condition == ECHOY_BATTERY_EMPTY ? tr(u, "电量已耗尽", "Battery empty")
                                                         : tr(u, "准备声音", "Preparing sound"));
        double t = (now % 1800000) / 900000.0;
        if (t > 1) {
            t = 2 - t;
        }
        t = t * t * (3 - 2 * t);
        if (u->condition == ECHOY_AUDIO_FAULT) {
            t = 0;
        }
        int scale = (int)lround(65536 * (1 - .15 * t)), fade = (int)lround(204 * t);
        if (scale != u->last_scale) {
            const gsp_value_t value = {.type = GSP_VALUE_U32, .data = {.u32 = (uint32_t)scale}};
            CHECK(u, esp_gsp_component_set_property(u->gsp, GSP_ECHOY_OBJ_KEY_STATUS_ART,
                                                    GSP_ECHOY_PROP_KEY_SCALE, &value));
            u->last_scale = scale;
        }
        if (fade != u->last_fade) {
            CHECK(u, gsp_echoy_status_art_fade_set_opacity(u->gsp, fade));
            u->last_fade = fade;
        }
    }
    bool animated = u->frames[0].active || u->frames[0].energy > 0;
    if (u->condition == ECHOY_NORMAL && !u->removed &&
        (force || u->frames_dirty ||
         ((animated || u->last_draw_animated) && now - u->last_draw_us >= DRAW_US))) {
        unsigned count = u->route == HOME ? 2 : 0;
        for (unsigned i = 0; i < count; i++) {
            if (u->canvas_registered[i]) {
                CHECK(u, esp_gsp_canvas_invalidate(u->gsp, canvases[i]));
            }
        }
        u->last_draw_us = now;
        u->last_draw_animated = animated;
        u->frames_dirty = false;
    }
    if (u->route == ROLE && u->condition == ECHOY_NORMAL && !u->removed) {
        publish_gallery(u);
    }
    route_t shown = u->removed ? REMOVED : u->condition != ECHOY_NORMAL ? STATUS : u->route;
    if ((int)shown != u->last_route) {
        fprintf(stderr, "ECHOY_ROUTE route=%s locale=%s\n",
                shown < REMOVED ? routes[shown] : "removed", u->english ? "en" : "zh");
        u->last_route = shown;
    }
    if ((int)u->english != u->last_english) {
        fprintf(stderr, "ECHOY_LOCALE %s\n", u->english ? "en" : "zh");
        u->last_english = u->english;
    }
    if ((int)s.phase != u->last_phase) {
        fprintf(stderr, "ECHOY_STATE phase=%d seed=%04x generation=%u at_us=%lld\n", (int)s.phase,
                s.seed, (unsigned)s.generation, (long long)now);
        u->last_phase = s.phase;
    }
    if (s.committed_revision != u->last_committed) {
        const echoy_plan_t *p = current_plan(u);
        fprintf(stderr, "ECHOY_PLAN revision=%u role=%s phrase=%u anchors=%zu events=%zu\n",
                (unsigned)s.committed_revision, role_ids[p->role], p->phrase, p->source_count,
                p->count);
        u->last_committed = s.committed_revision;
    }
}
static void tick(esp_gsp_handle_t gsp, void *ctx)
{
    (void)gsp;
    echoy_ui_handle_t u = ctx;
    int64_t now = now_us(u);
    int image_error = atomic_exchange(&u->image_error, GSP_OK);
    if (image_error != GSP_OK) {
        CHECK(u, (esp_gsp_err_t)image_error);
    }
    if (u->platform.poll_command) {
        for (unsigned i = 0; i < 4; i++) {
            echoy_command_t c = u->platform.poll_command(u->platform.ctx);
            if (c == ECHOY_COMMAND_NONE) {
                break;
            }
            if (c == ECHOY_COMMAND_RESTORE) {
                u->removed = false;
                recover(u);
                if (u->platform.set_removed) {
                    u->platform.set_removed(u->platform.ctx, false);
                }
            } else if (c == ECHOY_COMMAND_RECOVER) {
                recover(u);
            } else {
                set_condition(u, c == ECHOY_COMMAND_LOADING         ? ECHOY_LOADING
                                 : c == ECHOY_COMMAND_BATTERY_EMPTY ? ECHOY_BATTERY_EMPTY
                                                                    : ECHOY_AUDIO_FAULT);
            }
        }
    }
    if (u->density_dirty && now >= u->density_due_us) {
        commit_density(u, now);
    }
    if (!u->platform.read_control) {
        echoy_model_tick(u->model, now);
    }
    echoy_snapshot_t s;
    read_state(u, &s);
    /* Standalone UI-only adapter; the shared band waits for real prepared audio. */
    if (!u->platform.read_control && s.phase == ECHOY_PREPARING && !s.plan_pending) {
        echoy_model_prepare_done(u->model, s.generation, now);
    }
    refresh(u, now, false);
#ifdef ESP_PLATFORM
    int64_t elapsed = now_us(u)-now, gap = u->perf_tick_at ? now-u->perf_tick_at : 0;
    if (elapsed > u->perf_tick_max) u->perf_tick_max = elapsed;
    if (gap > u->perf_gap_max) u->perf_gap_max = gap;
    u->perf_tick_at = now;
    if (now-u->perf_log_at >= 5000000) {
        fprintf(stderr, "ECHOY_UI_COST route=%u tick_max_us=%lld gap_max_us=%lld stack_min=%u\n",
                (unsigned)u->route, (long long)u->perf_tick_max, (long long)u->perf_gap_max,
                (unsigned)uxTaskGetStackHighWaterMark(NULL));
        u->perf_log_at = now;
        u->perf_tick_max = u->perf_gap_max = 0;
    }
#endif
}
static void pointer(esp_gsp_handle_t gsp, int32_t x, int32_t y, bool pressed, void *ctx)
{
    (void)gsp;
    echoy_ui_handle_t u = ctx;
    echoy_snapshot_t s;
    read_state(u, &s);
    int64_t now = now_us(u);
    if (pressed) {
        echoy_display_touch(&u->display, now);
    }
    if (pressed && !u->pressed) {
        u->drag = 0;
        u->drag_y = y;
        if (u->condition == ECHOY_NORMAL && !u->removed) {
            if (u->route == HOME) {
                if (y >= 89 && y < 296 && x >= 16 && x < 80) {
                    u->drag = 1;
                    u->drag_value = u->volume;
                } else if (y >= 89 && y < 296 && x >= 400 && x < 464 && !capturing(s.phase)) {
                    u->drag = 2;
                    u->drag_value = u->richness;
                } else if (x >= 90 && x < 390 && y >= 89 && y < 293 &&
                           act(u, ECHOY_ACT_TAP, 0, now)) {
                    u->last_hit_us = now;
                    echoy_strip_state_t strip = echoy_strip_compute(
                        &s, current_plan(u)->count != 0,
                        u->density_dirty || (u->platform.read_control && u->external.preparing));
                    if (strip.mode == ECHOY_STRIP_RECORD_ONLY && strip.record_enabled &&
                        u->trial_taps < TRIAL_HINT_TAPS && ++u->trial_taps == TRIAL_HINT_TAPS) {
                        u->record_hint_until_us = now + TRIAL_HINT_US;
                    }
                }
            } else if (u->route == DISPLAY && x >= 36 && x < 444 && y >= 160 && y < 230) {
                u->drag = 4;
            }
        }
    }
    if (pressed && u->drag) {
        if (u->drag == 4) {
            int position = x < 62 ? 0 : x > 418 ? 356 : x - 62;
            echoy_display_set_brightness(&u->display, 20 + (position * 80 + 178) / 356);
            if (u->display_failed) {
                u->display_level = -1;
            }
        } else {
            double value = u->drag_value + (u->drag_y - y) / 180.0;
            if (value < 0) {
                value = 0;
            }
            if (value > 1) {
                value = 1;
            }
            if (u->drag == 1) {
                u->volume = value;
                act(u, ECHOY_ACT_VOLUME, value, now);
            } else if (!capturing(s.phase) && value != u->richness) {
                u->richness = value;
                u->density_dirty = true;
                u->density_due_us = now + 85000;
            }
        }
    }
    if (!pressed) {
        if (u->drag == 2) {
            commit_density(u, now);
        }
        u->drag = 0;
    }
    u->pressed = pressed;
    refresh(u, now, true);
}
static void event(esp_gsp_handle_t gsp, const esp_gsp_event_t *e, void *ctx)
{
    (void)gsp;
    echoy_ui_handle_t u = ctx;
    int64_t now = now_us(u);
    echoy_snapshot_t s;
    read_state(u, &s);
    echoy_display_touch(&u->display, now);
    if (gsp_echoy_event_is_recover_click(e)) {
        recover(u);
        refresh(u, now, true);
        return;
    }
    if (u->condition != ECHOY_NORMAL || u->removed) {
        return;
    }
    echoy_strip_state_t strip = echoy_strip_compute(
        &s, current_plan(u)->count != 0,
        u->density_dirty || (u->platform.read_control && u->external.preparing));
    if (gsp_echoy_event_is_record_click(e)) {
        if ((u->route == HOME && strip.record_enabled) && act(u, ECHOY_ACT_RECORD, 0, now)) {
            select_route(u, HOME);
        }
    } else if (gsp_echoy_event_is_strip_action_click(e)) {
        if (u->route == HOME && strip.single_enabled) {
            if (strip.mode == ECHOY_STRIP_CANCEL) {
                act(u, ECHOY_ACT_CANCEL_RECORD, 0, now);
            } else if (strip.mode == ECHOY_STRIP_PAUSE) {
                act(u, ECHOY_ACT_PAUSE, 0, now);
            }
        }
    } else if (gsp_echoy_event_is_transport_click(e)) {
        if (u->route == HOME && strip.play_enabled) {
            act(u, ECHOY_ACT_TRANSPORT, 0, now);
        }
    } else if (!capturing(s.phase)) {
        if (gsp_echoy_event_is_role_click(e)) {
            select_route(u, ROLE);
        } else if (gsp_echoy_event_is_settings_click(e)) {
            select_route(u, SETTINGS);
        } else if (gsp_echoy_event_is_display_click(e)) {
            select_route(u, DISPLAY);
        } else if (gsp_echoy_event_is_auto_dim_click(e)) {
            u->display.auto_dim = !u->display.auto_dim;
            u->display_level = -1;
        } else if (gsp_echoy_event_is_guide_click(e)) {
            select_route(u, GUIDE);
        } else if (gsp_echoy_event_is_info_click(e)) {
            select_route(u, INFO);
        } else if (gsp_echoy_event_is_back_click(e)) {
            select_route(u, u->route == DISPLAY || u->route == GUIDE || u->route == INFO
                                ? SETTINGS : HOME);
        } else if (gsp_echoy_event_is_prev_role_click(e)) {
            act(u, ECHOY_ACT_ROLE, previous_role[s.role], now);
        } else if (gsp_echoy_event_is_next_role_click(e)) {
            act(u, ECHOY_ACT_ROLE, next_role[s.role], now);
        } else if (gsp_echoy_event_is_phrase_1_click(e)) {
            act(u, ECHOY_ACT_PHRASE, 1, now);
        } else if (gsp_echoy_event_is_phrase_2_click(e)) {
            act(u, ECHOY_ACT_PHRASE, 2, now);
        } else if (gsp_echoy_event_is_phrase_3_click(e)) {
            act(u, ECHOY_ACT_PHRASE, 3, now);
        } else if (gsp_echoy_event_is_lang_zh_click(e) || gsp_echoy_event_is_lang_en_click(e)) {
            u->english = gsp_echoy_event_is_lang_en_click(e);
            if (u->platform.perform_action) {
                act(u, ECHOY_ACT_LANGUAGE, u->english, now);
            }
            u->frames_dirty = true;
        } else if (gsp_echoy_event_is_audition_click(e)) {
            if (u->route == ROLE && s.phase >= ECHOY_PREPARING) {
                act(u, ECHOY_ACT_PAUSE, 0, now);
            } else {
                if (u->route != ROLE) {
                    select_route(u, HOME);
                }
                act(u, ECHOY_ACT_AUDITION, 0, now);
            }
        }
    }
    u->frames_dirty = true;
    refresh(u, now, true);
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
    echoy_display_init(&u->display, now_us(u));
    u->display_level = -1;
    u->volume = .65;
    u->richness = .5;
    u->battery = platform->battery_percent;
    u->strip_mode = u->strip_permissions = -1;
    u->strip_border_rgb = UINT32_MAX;
    u->reduced_motion = platform->reduced_motion;
    for (unsigned i = 0; i < 4; i++) {
        u->strip_glyph_alpha[i] = u->strip_glyph_locale[i] = -1;
    }
    u->last_hit_us = -1000000;
    u->last_phase = u->last_route = u->last_condition = u->last_english = -1;
    u->last_step = 16;
    u->last_gallery_revision = u->last_committed = UINT32_MAX;
    u->last_capture_mask = UINT16_MAX;
    u->last_scale = 65536;
    u->last_fade = 0;
    atomic_init(&u->error, ESP_GSP_OK);
    atomic_init(&u->image_error, GSP_OK);
    atomic_init(&u->images_pending, 0);
    u->art_workspace = echoy_art_workspace_create();
    u->model = echoy_model_create();
    u->view = echoy_view_cache_create(gsp);
    if (!u->model || !u->view || !u->art_workspace) {
        echoy_model_delete(u->model);
        echoy_view_cache_delete(u->view);
        echoy_art_workspace_delete(u->art_workspace);
        free(u);
        return ESP_GSP_ERR_NO_MEM;
    }
    if (platform->initial_seed) {
        echoy_model_load_seed(u->model, platform->initial_seed, now_us(u), false);
    }
    if (platform->initial_condition != ECHOY_NORMAL) {
        set_condition(u, platform->initial_condition);
    }
    refresh(u, now_us(u), true);
    esp_gsp_err_t err = atomic_load(&u->error);
    for (unsigned i = 0; i < 2 && err == ESP_GSP_OK; i++) {
        err = esp_gsp_canvas_set_draw_cb(
            gsp, canvases[i], i == 1 ? echoy_feedback_draw : echoy_art_draw, &u->frames[i]);
        u->canvas_registered[i] = err == ESP_GSP_OK;
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
            *out = u;
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
    for (unsigned i = 0; i < 2; i++) {
        if (u->canvas_registered[i]) {
            err = esp_gsp_canvas_stop(u->gsp, canvases[i]);
            if (err != ESP_GSP_OK) {
                return err;
            }
            u->canvas_registered[i] = false;
        }
    }
    err = esp_gsp_flush(u->gsp, 1000);
    if (err != ESP_GSP_OK) {
        return err;
    }
    if (atomic_load_explicit(&u->images_pending, memory_order_acquire)) {
        return ESP_GSP_ERR_INVALID_STATE;
    }
    echoy_model_delete(u->model);
    echoy_view_cache_delete(u->view);
    echoy_art_workspace_delete(u->art_workspace);
    free(u);
    return ESP_GSP_OK;
}
esp_gsp_err_t echoy_ui_get_error(echoy_ui_handle_t u)
{
    return u ? (esp_gsp_err_t)atomic_load(&u->error) : ESP_GSP_ERR_INVALID_ARG;
}

void echoy_ui_release_closed(echoy_ui_handle_t u)
{
    if (!u) {
        return;
    }
    echoy_model_delete(u->model);
    echoy_view_cache_delete(u->view);
    echoy_art_workspace_delete(u->art_workspace);
    free(u);
}
