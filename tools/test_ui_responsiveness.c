/* Real UI scheduling with a controlled controller and asynchronous image sink.
 * Keep controller, renderer and GSP boundaries explicit; no device timing claim. */
#include <assert.h>
#include "../projects/echoy/main/echoy_ui.c"

static echoy_control_view_t owner;
static int64_t test_now;
static unsigned reads, encodes, submissions, selected_phrase;
static unsigned invalidations, volume_actions;
static int volume_knob_y;
static bool role_visible;
static esp_gsp_image_complete_cb_t pending_complete;
static void *pending_ctx;
static uint16_t pending_bind;
static const void *pending_data, *retained_images[7];
static unsigned submitted_role[16], submitted_phrase[16], submitted_bind[16];
static unsigned image_slot(uint16_t bind)
{
    static const uint16_t binds[] = {
        GSP_ECHOY_BIND_ROLE_PREV_ART, GSP_ECHOY_BIND_ROLE_MAIN_ART, GSP_ECHOY_BIND_ROLE_NEXT_ART,
        GSP_ECHOY_BIND_STRIP_RECORD_GLYPH, GSP_ECHOY_BIND_STRIP_PLAY_GLYPH,
        GSP_ECHOY_BIND_STRIP_CANCEL_GLYPH, GSP_ECHOY_BIND_STRIP_PAUSE_GLYPH};
    for (unsigned i = 0; i < 7; i++) if (bind == binds[i]) return i;
    assert(!"Unknown image bind");
    return 0;
}

esp_gsp_err_t echoy_view_show(echoy_view_cache_t v, const char *name, bool shown)
{
    (void)v;
    if (!strcmp(name, "role_page")) role_visible = shown;
    for (unsigned i = 1; i <= 3; i++) {
        char key[32];
        snprintf(key, sizeof(key), "phrase_%u_selected", i);
        if (!strcmp(name, key) && shown) selected_phrase = i;
    }
    return ESP_GSP_OK;
}
#define VIEW_STUB(name, type) \
    esp_gsp_err_t name(echoy_view_cache_t v, const char *key, type x) \
    { (void)v; (void)key; (void)x; return ESP_GSP_OK; }
VIEW_STUB(echoy_view_enable, bool)
VIEW_STUB(echoy_view_text, const char *)
VIEW_STUB(echoy_view_color, uint32_t)
VIEW_STUB(echoy_view_value, int32_t)
esp_gsp_err_t echoy_view_y(echoy_view_cache_t v, const char *name, int32_t value)
{ (void)v; if (!strcmp(name, "volume_knob")) volume_knob_y = value; return ESP_GSP_OK; }
VIEW_STUB(echoy_view_x, int32_t)
VIEW_STUB(echoy_view_opacity, uint8_t)
VIEW_STUB(echoy_view_scale, uint32_t)

esp_gsp_err_t esp_gsp_canvas_invalidate(esp_gsp_handle_t gsp, uint16_t bind)
{ (void)gsp; (void)bind; invalidations++; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_canvas_try_push(esp_gsp_handle_t gsp, uint16_t bind,
    const void *pixels, size_t stride, esp_gsp_canvas_release_cb_t release, void *ctx)
{
    (void)gsp; (void)bind; (void)pixels; (void)stride; (void)release; (void)ctx;
    assert(!"Scheduling-only fixture does not create a home producer");
    return ESP_GSP_ERR_INVALID_STATE;
}
esp_gsp_err_t esp_gsp_component_set_property(esp_gsp_handle_t gsp, gsp_component_key_t key,
                                           gsp_property_key_t property, const gsp_value_t *v)
{ (void)gsp; (void)key; (void)property; (void)v; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_component_set_property_color_rgb888(esp_gsp_handle_t gsp,
    gsp_component_key_t key, gsp_property_key_t property, uint32_t rgb)
{ (void)gsp; (void)key; (void)property; (void)rgb; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_set_tint_rgb888(esp_gsp_handle_t gsp, uint16_t bind, uint32_t rgb)
{ (void)gsp; (void)bind; (void)rgb; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_set_opacity(esp_gsp_handle_t gsp, uint16_t bind, uint8_t opacity)
{ (void)gsp; (void)bind; (void)opacity; return ESP_GSP_OK; }

bool echoy_strip_glyph_qoi(bool english, unsigned kind, uint8_t alpha, uint32_t color,
                           uint8_t **out, size_t *size)
{
    (void)english; (void)kind; (void)alpha; (void)color;
    *out = calloc(1, 2); *size = 2;
    return *out != NULL;
}
bool echoy_art_encode_qoi(const echoy_art_frame_t *frame, uint8_t **out, size_t *size)
{
    assert(role_visible && selected_phrase == owner.state.phrase);
    encodes++;
    *out = malloc(2); *size = 2;
    assert(*out);
    (*out)[0] = frame->role;
    (*out)[1] = frame->plan->phrase;
    return true;
}
esp_gsp_err_t esp_gsp_set_image_ex(esp_gsp_handle_t gsp, uint16_t bind, const void *data,
                                  size_t size, const esp_gsp_image_options_t *options)
{
    (void)size;
    assert(options->ownership == ESP_GSP_IMAGE_TAKE);
    if (bind != GSP_ECHOY_BIND_ROLE_PREV_ART && bind != GSP_ECHOY_BIND_ROLE_MAIN_ART &&
        bind != GSP_ECHOY_BIND_ROLE_NEXT_ART) {
        options->on_complete(gsp, bind, GSP_OK, options->complete_ctx);
        unsigned slot = image_slot(bind);
        free((void *)retained_images[slot]);
        retained_images[slot] = data;
        return ESP_GSP_OK;
    }
    assert(submissions < 16);
    submitted_bind[submissions] = bind;
    submitted_role[submissions] = ((const uint8_t *)data)[0];
    submitted_phrase[submissions++] = ((const uint8_t *)data)[1];
    pending_complete = options->on_complete;
    pending_ctx = options->complete_ctx;
    pending_bind = bind;
    pending_data = data;
    return ESP_GSP_OK;
}
static void complete_image(void)
{
    assert(pending_complete);
    esp_gsp_image_complete_cb_t callback = pending_complete;
    pending_complete = NULL;
    callback(NULL, pending_bind, GSP_OK, pending_ctx);
    unsigned slot = image_slot(pending_bind);
    free((void *)retained_images[slot]);
    retained_images[slot] = pending_data;
}
static bool read_owner(void *ctx, echoy_control_view_t *out)
{
    (void)ctx;
    reads++;
    *out = owner;
    return true;
}
static int64_t clock_now(void *ctx) { (void)ctx; return test_now; }
static bool set_volume(void *ctx, echoy_action_t action, double value, int64_t at)
{
    (void)ctx;
    assert(action == ECHOY_ACT_VOLUME && at == test_now);
    owner.volume = value;
    volume_actions++;
    return true;
}
static struct echoy_ui *setup(void)
{
    struct echoy_ui *u = calloc(1, sizeof(*u));
    assert(u);
    owner = (echoy_control_view_t){.present = true, .volume = .65, .battery = 72,
        .state = {.role = ECHOY_PULSE, .phrase = 2, .density = .5, .seed = 5, .revision = 1},
        .memories = {2, 1, 1, 1}};
    assert(echoy_compose_mask(5, ECHOY_PULSE, 2, .5, &owner.plan));
    u->platform.read_control = read_owner;
    u->platform.now_us = clock_now;
    u->last_gallery_revision = UINT32_MAX;
    u->last_route = -1;
    u->strip_mode = u->strip_permissions = -1;
    echoy_display_init(&u->display, 0);
    reads = encodes = submissions = selected_phrase = 0;
    role_visible = false;
    pending_complete = NULL;
    test_now = 1000000;
    return u;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    struct echoy_ui *u = setup();
    if (!strcmp(argv[1], "idle-pointer-art") || !strcmp(argv[1], "volume-pointer-art") ||
        !strcmp(argv[1], "capture-dirty-art")) {
        u->canvas_registered[0] = u->canvas_registered[1] = true;
        u->route = HOME;
        if (!strcmp(argv[1], "capture-dirty-art")) {
            owner.state.phase = ECHOY_RECORDING;
            owner.state.capture_at_us = test_now;
            owner.state.pending_seed = 1;
        }
        refresh(u, test_now, true);
        invalidations = 0;
        if (!strcmp(argv[1], "idle-pointer-art")) {
            for (unsigned i = 0; i < 20; i++) {
                test_now += 2000;
                pointer(NULL, 230 + i, 70, i < 10, u);
            }
            assert(invalidations == 0 && "Unchanged pointer samples must not repaint home art");
        } else if (!strcmp(argv[1], "volume-pointer-art")) {
            u->platform.perform_action = set_volume;
            pointer(NULL, 48, 200, true, u);
            test_now += 1000;
            pointer(NULL, 48, 180, true, u);
            assert(volume_actions == 2 && owner.volume > .76 && owner.volume < .77);
            assert(volume_knob_y == 148 - (int)lround(owner.volume * 111));
            pointer(NULL, 48, 180, false, u);
            assert(invalidations == 0 && "Slider feedback must not force unchanged home art");
        } else {
            owner.state.pending_seed |= 4;
            test_now += 1000;
            tick(NULL, u);
            assert(invalidations == 2 && "A newly recorded cell must paint without waiting for the animation interval");
            assert(u->last_capture_mask == 5);
        }
    } else if (!strcmp(argv[1], "measurement-pending-density")) {
        u->route = HOME;
        refresh(u, test_now, true);
        u->density_dirty = true;
        u->density_due_us = test_now - 1;
        u->richness = .9;
        u->drag = 2;
        u->platform.perform_action = set_volume; /* Rejects an unexpected density action. */
        owner.measurement_active = owner.measurement_microphone = true;
        tick(NULL, u);
        assert(!u->density_dirty && !u->drag && u->richness == owner.state.density);
        assert(!volume_actions && "A stale delayed drag must not cancel measurement");
    } else if (!strcmp(argv[1], "single-read")) {
        tick(NULL, u);
        assert(reads == 1 && "A tick must read the controller snapshot once");
    } else if (!strcmp(argv[1], "feedback-first")) {
        u->route = ROLE;
        refresh(u, test_now, true);
        assert(selected_phrase == 2 && role_visible);
        assert(encodes == 0 && "Return the input callback before gallery work");
        test_now += DRAW_US / 2;
        tick(NULL, u);
        assert(encodes == 0 && "Leave one display interval for selection feedback");
        test_now += DRAW_US;
        tick(NULL, u);
        assert(encodes == 1 && submissions == 1);
        assert(submitted_bind[0] == GSP_ECHOY_BIND_ROLE_MAIN_ART);
        tick(NULL, u);
        assert(encodes == 1 && "Do not encode again while an image is pending");
        complete_image();
    } else if (!strcmp(argv[1], "hover-progress") || !strcmp(argv[1], "drag-progress")) {
        u->route = ROLE;
        refresh(u, test_now, true);
        bool pressed = !strcmp(argv[1], "drag-progress");
        for (unsigned i = 0; i < 25; i++) {
            test_now += 20000;
            unsigned before = encodes;
            pointer(NULL, 220 + i, 190 + i, pressed, u);
            assert(encodes == before && "Input callbacks must never encode gallery images");
            tick(NULL, u);
            if (pending_complete) complete_image();
        }
        assert(encodes == 3 && "Continuous pointer samples must not starve gallery work");
        pointer(NULL, 245, 215, false, u);
    } else if (!strcmp(argv[1], "quick-role-return")) {
        u->route = ROLE;
        refresh(u, test_now, true);
        test_now += DRAW_US;
        tick(NULL, u);
        assert(encodes == 1);
        complete_image();
        select_route(u, HOME);
        refresh(u, test_now, true);
        test_now += 5000;
        select_route(u, ROLE);
        refresh(u, test_now, true);
        assert(role_visible && encodes == 1);
        test_now += 10000;
        pointer(NULL, 240, 190, false, u);
        tick(NULL, u);
        assert(encodes == 1 && "A real page re-entry must yield for its controls to paint");
        test_now += 25000;
        pointer(NULL, 241, 191, false, u);
        tick(NULL, u);
        assert(encodes == 2 && "Hover must not extend the page-entry deadline");
        complete_image();
    } else if (!strcmp(argv[1], "latest-wins") || !strcmp(argv[1], "latest-after-last")) {
        u->route = ROLE;
        refresh(u, test_now, true);
        test_now += DRAW_US;
        tick(NULL, u);
        assert(submissions == 1);
        if (!strcmp(argv[1], "latest-after-last")) {
            for (unsigned i = 0; i < 2; i++) {
                complete_image();
                test_now += DRAW_US;
                tick(NULL, u);
            }
            assert(submissions == 3 && u->gallery_inflight);
        }
        unsigned previous = submissions;
        owner.state.role = ECHOY_VOICE;
        owner.state.phrase = 3;
        owner.memories[ECHOY_VOICE] = 3;
        owner.state.revision = 2;
        refresh(u, test_now, true);
        assert(selected_phrase == 3 && submissions == previous);
        complete_image();
        for (unsigned i = 0; i < 3; i++) {
            test_now += DRAW_US;
            tick(NULL, u);
            assert(submissions == previous + i + 1 && "Submit no more than one image per tick");
            complete_image();
        }
        test_now += DRAW_US;
        tick(NULL, u);
        assert(submissions == previous + 3);
        assert(submitted_bind[previous] == GSP_ECHOY_BIND_ROLE_MAIN_ART);
        assert(submitted_role[previous] == ECHOY_VOICE && submitted_phrase[previous] == 3);
        assert(submitted_bind[previous + 1] == GSP_ECHOY_BIND_ROLE_PREV_ART &&
               submitted_role[previous + 1] == ECHOY_PULSE);
        assert(submitted_bind[previous + 2] == GSP_ECHOY_BIND_ROLE_NEXT_ART &&
               submitted_role[previous + 2] == ECHOY_DUST);
        assert(!u->gallery_dirty && !u->gallery_inflight && u->gallery_submitted_revision == 2);
        assert(atomic_load(&u->images_pending) == 0);
    } else {
        assert(!"Unknown test");
    }
    assert(!atomic_load(&u->error));
    for (unsigned i = 0; i < 7; i++) free((void *)retained_images[i]);
    free(u);
    puts("PASS");
    return 0;
}
