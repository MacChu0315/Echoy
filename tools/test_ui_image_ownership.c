/* Exercise the real encoders and UI image submissions against a lifecycle sink.
 * Completion and input release are deliberately separate events. */
#include <assert.h>
#include "../projects/echoy/main/echoy_ui.c"

enum allocation_kind { TEMPORARY, IMAGE_STORAGE };
static struct allocation {
    void *data;
    size_t size;
    enum allocation_kind kind;
    bool freed, accepted;
} allocations[64];
static unsigned allocation_count, storage_calls;
static bool tracking, storage_fail, submit_fail;
static struct request {
    const void *data;
    size_t size;
    uint16_t bind;
    esp_gsp_image_options_t options;
    bool complete, released;
} requests[16];
static unsigned request_count;
void *__real_malloc(size_t size);
void __real_free(void *data);

static void *allocate(size_t size, enum allocation_kind kind)
{
    void *data = __real_malloc(size);
    assert(data && allocation_count < 64);
    allocations[allocation_count++] = (struct allocation){data, size, kind, false, false};
    return data;
}
void *__wrap_malloc(size_t size)
{
    return tracking ? allocate(size, TEMPORARY) : __real_malloc(size);
}
static struct allocation *find_allocation(const void *data)
{
    for (unsigned i = allocation_count; i > 0; i--)
        if (allocations[i - 1].data == data) return &allocations[i - 1];
    return NULL;
}
void __wrap_free(void *data)
{
    struct allocation *allocation = find_allocation(data);
    if (allocation) {
        assert(!allocation->freed && "Image storage freed twice");
        allocation->freed = true;
    }
    __real_free(data);
}
static void *ui_storage_alloc(void *ctx, size_t size)
{
    assert(ctx == &storage_calls);
    storage_calls++;
    return storage_fail ? NULL : allocate(size, IMAGE_STORAGE);
}
esp_gsp_err_t echoy_view_show(echoy_view_cache_t view, const char *name, bool visible)
{ (void)view; (void)name; (void)visible; return ESP_GSP_OK; }
esp_gsp_err_t esp_gsp_set_image_ex(esp_gsp_handle_t gsp, uint16_t bind, const void *data,
                                  size_t size, const esp_gsp_image_options_t *options)
{
    (void)gsp;
    assert(options->ownership == ESP_GSP_IMAGE_TAKE && "No retained COPY back into default heap");
    assert(!options->on_release && "Storage is free-compatible; no UI lifetime in release callback");
    assert(size > 22 && !memcmp(data, "qoif", 4));
    struct allocation *allocation = find_allocation(data);
    assert(allocation && !allocation->freed);
    if (storage_calls) {
        assert(allocation->kind == IMAGE_STORAGE && allocation->size == size);
        for (unsigned i = 0; i < allocation_count; i++)
            assert(allocations[i].kind != TEMPORARY || allocations[i].freed);
    } else assert(allocation->kind == TEMPORARY);
    if (submit_fail) return ESP_GSP_ERR_TIMEOUT;
    allocation->accepted = true;
    assert(request_count < 16);
    requests[request_count++] = (struct request){data, size, bind, *options, false, false};
    return ESP_GSP_OK;
}
static bool perform_action(void *ctx, echoy_action_t action, double value, int64_t at)
{ (void)ctx; (void)action; (void)value; (void)at; return true; }
static int64_t clock_now(void *ctx) { (void)ctx; return 1000000; }
static void complete(unsigned index, gsp_err_t status)
{
    struct request *request = &requests[index];
    assert(!request->complete && !request->released);
    assert(!find_allocation(request->data)->freed);
    request->options.on_complete(NULL, request->bind, status, request->options.complete_ctx);
    request->complete = true;
    assert(!find_allocation(request->data)->freed && "Completion is not input release");
}
static void release_input(unsigned index)
{
    struct request *request = &requests[index];
    assert(request->complete && !request->released);
    free((void *)request->data);
    request->released = true;
}
static void shutdown_images(void)
{
    for (unsigned i = 0; i < request_count; i++) {
        if (!requests[i].complete) complete(i, GSP_ERR_CANCELLED);
        if (!requests[i].released) release_input(i);
    }
}
static void all_released(void)
{
    for (unsigned i = 0; i < allocation_count; i++) assert(allocations[i].freed);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    bool pc = !strncmp(argv[1], "pc-", 3);
    bool strip = strstr(argv[1], "strip") != NULL;
    bool decode_fail = strstr(argv[1], "decode-failure") != NULL;
    storage_fail = strstr(argv[1], "allocation-failure") != NULL;
    submit_fail = strstr(argv[1], "submission-failure") != NULL;
    struct echoy_ui *u = calloc(1, sizeof(*u));
    assert(u);
    u->platform = (echoy_platform_t){.ctx=&storage_calls, .now_us=clock_now,
        .perform_action=perform_action, .ui_storage_alloc=pc ? NULL : ui_storage_alloc};
    u->art_workspace = echoy_art_workspace_create();
    assert(u->art_workspace);
    u->gallery_dirty = true;
    for (unsigned i = 0; i < 3; i++) {
        assert(echoy_compose_mask(0x44a0, i, 1, .5, &u->gallery[i]));
        u->frames[i + 2] = (echoy_art_frame_t){.plan=&u->gallery[i], .role=i,
            .workspace=u->art_workspace, .width=i==1 ? 206 : 73, .height=i==1 ? 101 : 48,
            .background=i==1 ? 0xe5e7de : 0xecefe4};
    }
    tracking = true;
    if (strip) {
        unsigned count = storage_fail || submit_fail ? 1 : 4;
        for (unsigned i = 0; i < count; i++) strip_glyph(u, i, 255, 0xa36144);
    } else publish_gallery(u, 1000000);
    if (storage_fail || submit_fail) {
        assert(request_count == 0 && atomic_load(&u->images_pending) == 0);
        assert(atomic_load(&u->error) == (storage_fail ? ESP_GSP_ERR_NO_MEM : ESP_GSP_ERR_TIMEOUT));
        assert(storage_calls == (pc ? 0u : 1u));
        all_released();
    } else {
        assert(request_count == (strip ? 4u : 1u));
        assert(atomic_load(&u->images_pending) == request_count);
        for (unsigned i = 0; i < request_count; i++)
            complete(i, decode_fail ? GSP_ERR_INVALID_FORMAT : GSP_OK);
        assert(atomic_load(&u->images_pending) == 0);
        if (strip) {
            strip_glyph(u, 0, 128, 0xa36144);
            assert(request_count == 5 && requests[0].bind == requests[4].bind);
            complete(4, GSP_OK);
            release_input(0); /* Replacement releases the old retained source. */
            assert(!find_allocation(requests[4].data)->freed);
            strip_glyph(u, 1, 128, 0xa36144); /* Shutdown cancels this pending input. */
            assert(request_count == 6 && atomic_load(&u->images_pending) == 1);
        }
        shutdown_images();
        assert(atomic_load(&u->images_pending) == 0);
        assert(atomic_load(&u->image_error) == (decode_fail ? GSP_ERR_INVALID_FORMAT : GSP_OK));
        all_released();
    }
    tracking = false;
    echoy_art_workspace_delete(u->art_workspace);
    free(u);
    puts("PASS");
    return 0;
}
