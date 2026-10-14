/* Exercise the shipped status renderer with deterministic owner snapshots.
 * Native GSP pointer/screens are checked separately by check_cooperation_ui.py. */
#include <assert.h>
#include "../projects/echoy/main/echoy_ui.c"

struct test_value {
    char name[64], text[192];
    bool visible, enabled;
};
struct echoy_view_cache {
    struct test_value values[32];
    unsigned count;
};
static struct test_value *value(echoy_view_cache_t view, const char *name)
{
    for (unsigned i = 0; i < view->count; i++) {
        if (!strcmp(view->values[i].name, name)) {
            return &view->values[i];
        }
    }
    assert(view->count < 32);
    struct test_value *v = &view->values[view->count++];
    snprintf(v->name, sizeof(v->name), "%s", name);
    return v;
}
esp_gsp_err_t echoy_view_show(echoy_view_cache_t view, const char *name, bool visible)
{
    value(view, name)->visible = visible;
    return ESP_GSP_OK;
}
esp_gsp_err_t echoy_view_enable(echoy_view_cache_t view, const char *name, bool enabled)
{
    value(view, name)->enabled = enabled;
    return ESP_GSP_OK;
}
esp_gsp_err_t echoy_view_text(echoy_view_cache_t view, const char *name, const char *text)
{
    snprintf(value(view, name)->text, sizeof(value(view, name)->text), "%s", text);
    return ESP_GSP_OK;
}
esp_gsp_err_t echoy_view_value(echoy_view_cache_t view, const char *name, int32_t number)
{
    (void)view;
    (void)name;
    assert(number >= 0 && number <= 100);
    return ESP_GSP_OK;
}
static bool read_control(void *ctx, echoy_control_view_t *out)
{
    (void)ctx;
    (void)out;
    return true;
}
static const char *render(struct echoy_ui *ui, int64_t now)
{
    memset(ui->view, 0, sizeof(*ui->view));
    show_home_state(ui, &ui->external.state, now);
    bool cap = capturing(ui->external.state.phase);
    char name[64];
    snprintf(name, sizeof(name), "%s%s", cap ? "capture_status" : "status",
             ui->english ? "_en" : "");
    assert(value(ui->view, name)->visible);
    assert(value(ui->view, "status")->visible == !cap);
    assert(value(ui->view, "capture_status")->visible == (cap && !ui->english));
    assert(value(ui->view, "capture_status_en")->visible == (cap && ui->english));
    const char *text = value(ui->view, name)->text;
    assert(text[0]);
    printf("%d\t%s\n", ui->english, text);
    return text;
}
static void expect(struct echoy_ui *ui, int64_t now, const char *zh, const char *en)
{
    ui->english = false;
    assert(!strcmp(render(ui, now), zh));
    ui->english = true;
    assert(!strcmp(render(ui, now), en));
}
int main(void)
{
    struct echoy_view_cache cache = {0};
    struct echoy_ui *ui = calloc(1, sizeof(*ui));
    assert(ui);
    ui->view = &cache;
    ui->platform.read_control = read_control;
    echoy_control_view_t *v = &ui->external;
    v->state.role = ECHOY_PULSE;
    v->state.phrase = 1;
    expect(ui, 0, "中央试奏 · 录制才保存", "Tap to try · Record to keep");
    v->cooperation = ECHOY_COOP_CONNECTED;
    expect(ui, 0, "中央试奏 · 录制才保存 · 已连接", "Tap to try · Record to keep · Connected");
    v->cooperation = ECHOY_COOP_WAITING;
    v->state.following = true;
    ui->record_hint_until_us = 3000000;
    expect(ui, 0, "已连接 · 等待左侧录制", "Connected · Waiting for left recording");
    assert(!value(&cache, "record_hint_popup")->visible);
    v->plan.count = 1;
    expect(ui, 0, "已连接 · 等待左侧乐句", "Connected · Waiting for left phrase");
    v->cooperation = ECHOY_COOP_FOLLOWING;
    expect(ui, 0, "跟随左侧", "Following left");
    v->state.phase = ECHOY_PLAYING;
    expect(ui, 0, "跟随左侧", "Following left");
    v->state.plan_pending = true;
    expect(ui, 0, "下一小节生效 · 跟随左侧", "Changes at the next bar · Following left");
    v->state.plan_pending = false;
    v->preparing = true;
    expect(ui, 0, "准备新的声音 · 跟随左侧", "Preparing a new sound · Following left");
    v->preparing = false;
    v->state.phase = ECHOY_IDLE;
    v->cooperation = ECHOY_COOP_RECOVERING;
    expect(ui, 0, "连接恢复中", "Reconnecting");
    v->preparation_failed = true;
    expect(ui, 0, "新声音没有准备好，原来的继续 · 连接恢复中",
           "Preparation failed; current sound continues · Reconnecting");
    v->preparation_failed = false;
    v->state.notice = ECHOY_NOTICE_CANCELLED;
    v->cooperation = ECHOY_COOP_WAITING;
    expect(ui, 0, "已取消，原节奏保留 · 已连接 · 等待左侧乐句",
           "Cancelled; original rhythm kept · Connected · Waiting for left phrase");
    v->state.notice = ECHOY_NOTICE_EMPTY;
    expect(ui, 0, "没有录入，原节奏保留 · 已连接 · 等待左侧乐句",
           "No notes captured; original kept · Connected · Waiting for left phrase");
    v->state.notice = ECHOY_NOTICE_NONE;
    v->cooperation = ECHOY_COOP_DEFERRED;
    v->state.phase = ECHOY_COUNT_IN;
    v->state.capture_at_us = 3000000;
    expect(ui, 0, "预备-3 · 录完后加入", "Ready · 3 · Join after recording");
    expect(ui, 1000000, "预备-2 · 录完后加入", "Ready · 2 · Join after recording");
    expect(ui, 2000000, "预备-1 · 录完后加入", "Ready · 1 · Join after recording");
    v->state.phase = ECHOY_RECORDING;
    for (int beat = 1; beat <= 4; beat++) {
        char zh[96], en[96];
        snprintf(zh, sizeof(zh), "录入 · %d / 4 · 录完后加入", beat);
        snprintf(en, sizeof(en), "Recording · %d / 4 · Join after recording", beat);
        expect(ui, 3000000 + (beat - 1) * BEAT_US, zh, en);
    }
    v->state.phase = ECHOY_PREPARING;
    v->preparing = true;
    expect(ui, 6000000, "准备新的声音 · 录完后加入", "Preparing a new sound · Join after recording");
    v->preparing = false;
    v->state.phase = ECHOY_PREVIEW;
    v->state.start_us = 7000000;
    expect(ui, 6000000, "等共同拍点试听 · 结束后加入", "Audition at the shared beat · Join when done");
    expect(ui, 7000000, "试听一次 · 结束自动停 · 结束后加入", "One reply · Stops at the end · Join when done");
    /* Notices must not hide progress even if an older owner notice overlaps. */
    v->state.phase = ECHOY_RECORDING;
    v->state.notice = ECHOY_NOTICE_CANCELLED;
    v->preparation_failed = true;
    expect(ui, 3000000, "录入 · 1 / 4 · 录完后加入", "Recording · 1 / 4 · Join after recording");
    v->state.phase = ECHOY_IDLE;
    v->measurement_active = v->measurement_microphone = true;
    expect(ui, 3000000, "麦克风采集中 · 点暂停停止", "Microphone active · Pause to stop");
    assert(!value(&cache, "role")->enabled && !value(&cache, "settings")->enabled);
    assert(!value(&cache, "record_hint_popup")->visible);
    assert(!value(&cache, "touch_hint_group")->visible);
    v->measurement_microphone = false;
    expect(ui, 3000000, "声音测量 · 点暂停停止", "Sound measurement · Pause to stop");
    free(ui);
    return 0;
}
