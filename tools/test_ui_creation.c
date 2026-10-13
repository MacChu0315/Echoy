/* Real UI/model/cache construction with only GSP presentation substituted. */
#include <assert.h>
#include "../projects/echoy/main/echoy_ui.c"

static unsigned model_creates, model_deletes, storage_calls, images, timers;
static unsigned main_draw_callbacks, feedback_draw_callbacks;
static unsigned main_invalidations, feedback_invalidations, stream_submissions, stream_stops;
static bool stream_full, fail_stream_stop, fail_producer_storage;
static esp_gsp_err_t stream_error;
static const uint16_t *stream_pixels;
static esp_gsp_canvas_release_cb_t stream_release;
static void *stream_release_ctx;
static bool art_visible = true;
static uint16_t stream_marker;
static int64_t test_now = 1000000;
static size_t storage_size;
static bool fail_model, fail_storage, fail_timer, fail_flush;
static void *retained[32];
static unsigned retained_count;
static echoy_control_view_t owner;
echoy_model_handle_t __real_echoy_model_create(void);
void __real_echoy_model_delete(echoy_model_handle_t);
echoy_model_handle_t __wrap_echoy_model_create(void)
{
    model_creates++;
    return fail_model ? NULL : __real_echoy_model_create();
}
void __wrap_echoy_model_delete(echoy_model_handle_t model)
{
    if (model) model_deletes++;
    __real_echoy_model_delete(model);
}
static void *storage_alloc(void *ctx, size_t size)
{
    (void)ctx;
    storage_calls++;
    if (size > storage_size) storage_size = size;
    if (fail_storage) return NULL;
    if (fail_producer_storage && size > 60000) return NULL;
    void *p = malloc(size);
    assert(p);
    memset(p, 0xa5, size); /* Cache creation must initialize non-calloc storage. */
    return p;
}
static bool read_owner(void *ctx, echoy_control_view_t *view)
{ (void)ctx; *view = owner; return true; }
static bool perform_action(void *ctx, echoy_action_t action, double value, int64_t now)
{ (void)ctx; (void)action; (void)value; (void)now; return true; }
static int64_t clock_now(void *ctx) { (void)ctx; return test_now; }

#define SETTER(name, key_type, value_type) \
    esp_gsp_err_t name(esp_gsp_handle_t g, key_type k, value_type v) \
    { (void)g; (void)k; (void)v; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_component_set_visible(esp_gsp_handle_t g, gsp_component_key_t key, bool value)
{
    (void)g;
    /* Use the actual compiled component capabilities. A named resource-only
     * image is not a visibility target (the native failure was NOT_FOUND). */
    for (size_t i=0;i<sizeof(gsp_obj_entries_echoy)/sizeof(gsp_obj_entries_echoy[0]);++i) {
        const gsp_component_entry_t *entry=&gsp_obj_entries_echoy[i];
        if (entry->key!=key) continue;
        for (unsigned p=0;p<entry->prop_count;++p) {
            if (entry->props[p].key!=GSP_ECHOY_PROP_KEY_VISIBLE) continue;
            if (key==GSP_ECHOY_OBJ_KEY_HOME_ART) art_visible=value;
            return ESP_GSP_OK;
        }
        return ESP_GSP_ERR_NOT_FOUND;
    }
    return ESP_GSP_ERR_NOT_FOUND;
}
SETTER(esp_gsp_component_set_enabled, gsp_component_key_t, bool)
SETTER(esp_gsp_component_set_color_rgb888, gsp_component_key_t, uint32_t)
SETTER(esp_gsp_component_set_value, gsp_component_key_t, int32_t)
SETTER(esp_gsp_component_set_text, gsp_component_key_t, const char *)
SETTER(esp_gsp_set_opacity, uint16_t, uint8_t)
esp_gsp_err_t esp_gsp_component_set_property(esp_gsp_handle_t g, gsp_component_key_t k,
                                           gsp_property_key_t p, const gsp_value_t *v)
{ (void)g; (void)k; (void)p; (void)v; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_component_set_property_color_rgb888(esp_gsp_handle_t g,
    gsp_component_key_t k, gsp_property_key_t p, uint32_t v)
{ (void)g; (void)k; (void)p; (void)v; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_canvas_invalidate(esp_gsp_handle_t g, uint16_t bind)
{
    (void)g;
    if (bind==GSP_ECHOY_BIND_ART) ++main_invalidations;
    if (bind==GSP_ECHOY_BIND_FEEDBACK) ++feedback_invalidations;
    return ESP_GSP_OK;
}
esp_gsp_err_t esp_gsp_canvas_set_draw_cb(esp_gsp_handle_t g, uint16_t bind,
                                       esp_gsp_canvas_draw_cb_t cb, void *ctx)
{
    (void)g; (void)ctx;
    if (bind == GSP_ECHOY_BIND_ART && cb) ++main_draw_callbacks;
    if (bind == GSP_ECHOY_BIND_FEEDBACK && cb) ++feedback_draw_callbacks;
    return ESP_GSP_OK;
}
esp_gsp_err_t esp_gsp_canvas_stop(esp_gsp_handle_t g, uint16_t bind)
{
    (void)g;
    if (fail_stream_stop) return ESP_GSP_ERR_TIMEOUT;
    if (bind==GSP_ECHOY_BIND_ART) {
        ++stream_stops;
        if (stream_release) stream_release(stream_release_ctx);
        stream_release=NULL; stream_release_ctx=NULL; stream_pixels=NULL;
    }
    return ESP_GSP_OK;
}
esp_gsp_err_t esp_gsp_canvas_try_push(esp_gsp_handle_t g, uint16_t bind,
    const void *pixels, size_t stride, esp_gsp_canvas_release_cb_t release, void *ctx)
{
    (void)g;
    assert(bind==GSP_ECHOY_BIND_ART && pixels && stride==600 && release);
    if (stream_full) return ESP_GSP_ERR_TIMEOUT;
    if (stream_error) return stream_error;
    if (stream_release) stream_release(stream_release_ctx);
    stream_pixels=pixels; stream_marker=stream_pixels[0];
    stream_release=release; stream_release_ctx=ctx; ++stream_submissions;
    return ESP_GSP_OK;
}
esp_gsp_err_t esp_gsp_on_event(esp_gsp_handle_t g, esp_gsp_event_cb_t cb, void *ctx)
{ (void)g; (void)cb; (void)ctx; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_set_pointer_observer(esp_gsp_handle_t g,
                                         esp_gsp_pointer_observer_cb_t cb, void *ctx)
{ (void)g; (void)cb; (void)ctx; return ESP_GSP_OK; }
void *esp_gsp_timer_create(esp_gsp_handle_t g, uint32_t ms, esp_gsp_timer_cb_t cb, void *ctx)
{
    (void)g; (void)ms; (void)cb; (void)ctx;
    if (fail_timer) return NULL;
    timers++;
    return (void *)2;
}
esp_gsp_err_t esp_gsp_timer_delete(esp_gsp_handle_t g, void *timer)
{ (void)g; assert(timer == (void *)2 && timers); timers--; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_flush(esp_gsp_handle_t g, uint32_t ms)
{ (void)g; (void)ms; return fail_flush ? ESP_GSP_ERR_TIMEOUT : ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_set_image_ex(esp_gsp_handle_t g, uint16_t bind, const void *data,
                                  size_t size, const esp_gsp_image_options_t *options)
{
    assert(size > 22 && !memcmp(data, "qoif", 4));
    assert(options->ownership == ESP_GSP_IMAGE_TAKE && !options->on_release);
    assert(retained_count < 32);
    retained[retained_count++] = (void *)data;
    images++;
    options->on_complete(g, bind, GSP_OK, options->complete_ctx);
    return ESP_GSP_OK;
}
static void close_gsp(void)
{
    if (stream_release) stream_release(stream_release_ctx);
    stream_release=NULL; stream_release_ctx=NULL; stream_pixels=NULL;
    for (unsigned i = 0; i < retained_count; i++) free(retained[i]);
    retained_count = 0;
    timers = 0;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "legacy-cache")) {
        echoy_view_cache_t cache = echoy_view_cache_create((esp_gsp_handle_t)1);
        assert(cache && !storage_calls);
        assert(echoy_view_text(cache, "status", "Legacy caller") == ESP_GSP_OK);
        echoy_view_cache_delete(cache);
        puts("PASS");
        return 0;
    }
    bool external = strstr(argv[1], "external") != NULL;
    bool read = external || !strcmp(argv[1], "read-only");
    bool action = external || !strcmp(argv[1], "action-only");
    bool custom_storage = external || !strncmp(argv[1], "storage-", 8);
    fail_model = strstr(argv[1], "model-failure") != NULL;
    fail_storage = strstr(argv[1], "storage-failure") != NULL;
    fail_timer = strstr(argv[1], "timer-failure") != NULL;
    bool retry_close = strstr(argv[1], "close-retry") != NULL;
    bool closed = strstr(argv[1], "already-closed") != NULL;
    owner = (echoy_control_view_t){.present=true, .volume=.65, .battery=72,
        .state={.role=ECHOY_PULSE, .phrase=1, .density=.5, .seed=9}, .memories={1,1,1,1}};
    assert(echoy_compose_mask(9, ECHOY_PULSE, 1, .5, &owner.plan));
    echoy_platform_t platform = {.now_us=clock_now, .initial_seed=5,
        .read_control=read ? read_owner : NULL,
        .perform_action=action ? perform_action : NULL,
        .ui_storage_alloc=custom_storage ? storage_alloc : NULL};
    echoy_ui_handle_t ui = NULL;
    esp_gsp_err_t result = echoy_ui_create((esp_gsp_handle_t)1, &platform, &ui);
    assert(model_creates == (external ? 0u : 1u) && "External UI must not create a spare model");
    if (fail_storage) {
        assert(result == ESP_GSP_ERR_NO_MEM && !ui);
        assert(storage_calls == 1 && storage_size > 14000 && images == 0);
    } else if (fail_timer || (fail_model && !external)) {
        assert(result == ESP_GSP_ERR_NO_MEM && !ui && !timers);
    } else {
        assert(result == ESP_GSP_OK && ui);
        if (!strcmp(argv[1], "external-home-async")) {
            assert(main_draw_callbacks == 0 && "Expensive home art must not run on the GSP input/render owner");
            assert(feedback_draw_callbacks == 1 && "Small feedback row keeps its synchronous callback");
        }
        assert((ui->model == NULL) == external);
        if (custom_storage) assert(storage_size > 14000 && "The view cache must use the storage allocator");
        echoy_snapshot_t state;
        read_state(ui, &state);
        assert(state.seed == (read ? 9 : 5));
        if (ui->model) {
            echoy_model_get_snapshot(ui->model, &state);
            assert(state.seed == 5); /* Single/no callback compatibility. */
        }
        if (!strcmp(argv[1], "external-error")) {
            error_at(ui, ESP_GSP_ERR_INVALID_STATE, "injected external error", 0);
            assert(echoy_ui_get_error(ui) == ESP_GSP_ERR_INVALID_STATE);
        }
        if (closed) {
            close_gsp();
            echoy_ui_release_closed(ui);
        } else {
            if (retry_close) {
                fail_flush = true;
                assert(echoy_ui_delete(ui) == ESP_GSP_ERR_TIMEOUT);
                assert(model_deletes == 0);
                fail_flush = false;
            }
            assert(echoy_ui_delete(ui) == ESP_GSP_OK);
        }
    }
    close_gsp();
    assert(!timers && model_deletes == ((!external && !fail_model) ? 1u : 0u));
    puts("PASS");
    return 0;
}
