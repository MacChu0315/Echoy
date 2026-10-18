/* Real UI + real producer thread; only GSP and the slow raster boundary are
 * controlled. A held worker must never hold up input or change its timestamps. */
#define main creation_fixture_main
#include "test_ui_creation.c"
#undef main
#include <errno.h>
#include <pthread.h>
#include <time.h>

static pthread_mutex_t draw_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t draw_changed=PTHREAD_COND_INITIALIZER;
static bool hold_draw;
static unsigned draw_calls, tap_actions;
static echoy_role_t last_draw_role;
static float last_draw_energy;
static bool last_draw_active;
static int64_t last_tap_at;
static double last_tap_value;

void __real_echoy_art_draw(const esp_gsp_canvas_surface_t *, void *);
void __wrap_echoy_art_draw(const esp_gsp_canvas_surface_t *surface, void *ctx)
{
    const echoy_art_frame_t *frame=ctx;
    if (frame->width!=300 || frame->height!=204) {
        __real_echoy_art_draw(surface,ctx);
        return;
    }
    assert(!pthread_mutex_lock(&draw_lock));
    ++draw_calls;
    last_draw_role=frame->role;
    last_draw_active=frame->active;
    last_draw_energy=frame->energy;
    pthread_cond_broadcast(&draw_changed);
    while (hold_draw) assert(!pthread_cond_wait(&draw_changed,&draw_lock));
    pthread_mutex_unlock(&draw_lock);
    /* A recognizable completed frame makes accepted old-role output visible
     * to this test without recreating the rasterizer. Producer pixel fidelity
     * is covered separately against the real existing C rasterizer. */
    for (unsigned y=0;y<surface->height;++y) {
        uint16_t *row=(uint16_t *)((uint8_t *)surface->pixels+y*surface->stride_bytes);
        for (unsigned x=0;x<surface->width;++x) row[x]=(uint16_t)(frame->role+1);
    }
}
static void set_hold(bool value)
{
    assert(!pthread_mutex_lock(&draw_lock));
    hold_draw=value;
    pthread_cond_broadcast(&draw_changed);
    pthread_mutex_unlock(&draw_lock);
}
static void wait_for_draw(void)
{
    struct timespec deadline;
    assert(!clock_gettime(CLOCK_REALTIME,&deadline));
    deadline.tv_sec+=5;
    assert(!pthread_mutex_lock(&draw_lock));
    while (!draw_calls) assert(pthread_cond_timedwait(&draw_changed,&draw_lock,&deadline)!=ETIMEDOUT);
    pthread_mutex_unlock(&draw_lock);
}
static bool async_action(void *ctx, echoy_action_t action, double value, int64_t at)
{
    (void)ctx;
    if (action==ECHOY_ACT_TAP) { ++tap_actions; last_tap_at=at; last_tap_value=value; }
    if (action==ECHOY_ACT_VOLUME) owner.volume=value;
    return true;
}
static echoy_ui_handle_t create_ui(void)
{
    owner=(echoy_control_view_t){.present=true,.volume=.65,.battery=72,
        .state={.role=ECHOY_PULSE,.phrase=1,.density=.5,.seed=9},.memories={1,1,1,1}};
    assert(echoy_compose_mask(9,ECHOY_PULSE,1,.5,&owner.plan));
    echoy_platform_t platform={.now_us=clock_now,.read_control=read_owner,
        .perform_action=async_action,.ui_storage_alloc=storage_alloc};
    echoy_ui_handle_t ui=NULL;
    assert(echoy_ui_create((esp_gsp_handle_t)1,&platform,&ui)==ESP_GSP_OK && ui);
    return ui;
}
static void pump_until_published(echoy_ui_handle_t ui, unsigned previous)
{
    for (unsigned i=0;i<5000;++i) {
        tick(NULL,ui);
        if (stream_submissions>previous) return;
        struct timespec yield={.tv_nsec=1000000};
        nanosleep(&yield,NULL);
    }
    assert(!"Current home frame was never published");
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if (!strcmp(argv[1],"startup-fallback")) {
        fail_producer_storage=true;
        echoy_ui_handle_t ui=create_ui();
        assert(!ui->home_producer && main_draw_callbacks==1 && feedback_draw_callbacks==1);
        assert(echoy_ui_delete(ui)==ESP_GSP_OK);
        close_gsp();
        puts("PASS explicit allocation-failure fallback");
        return 0;
    }
    set_hold(true);
    echoy_ui_handle_t ui=create_ui();
    assert(ui->home_producer && main_draw_callbacks==0 && feedback_draw_callbacks==1);
    wait_for_draw();
    uint32_t epoch=ui->home_epoch;
    if (!strcmp(argv[1],"role-preview-gate")) {
        set_hold(false);pump_until_published(ui,0);
        assert(act(ui,ECHOY_ACT_ROLE,ECHOY_VOICE,test_now));
        /* Owner cache may still describe the old role after queuing the edit. */
        pointer(NULL,220,200,true,ui);pointer(NULL,220,200,false,ui);
        assert(tap_actions==1 && last_tap_value==ECHOY_PULSE+1);
        test_now+=100000;
        owner.state.role=ECHOY_VOICE;owner.state.plan_pending=true;
        tick(NULL,ui);
        pointer(NULL,220,200,true,ui);pointer(NULL,220,200,false,ui);
        assert(tap_actions==1); /* Selected role changed; committed plan has not. */
        set_hold(true);
        assert(echoy_compose_mask(9,ECHOY_VOICE,1,.5,&owner.plan));
        owner.state.plan_pending=false;++owner.state.committed_revision;tick(NULL,ui);
        pointer(NULL,220,200,true,ui);pointer(NULL,220,200,false,ui);
        assert(tap_actions==1 && !art_visible);
        unsigned before=stream_submissions;set_hold(false);pump_until_published(ui,before);
        assert(art_visible && stream_marker==ECHOY_VOICE+1);
        pointer(NULL,220,200,true,ui);pointer(NULL,220,200,false,ui);
        assert(tap_actions==2 && last_tap_value==ECHOY_VOICE+1);
        for(unsigned i=0;i<99;i++){
            test_now+=1000;pointer(NULL,220,200,true,ui);pointer(NULL,220,200,false,ui);
        }
        assert(tap_actions==2);
        ++test_now; /* Still before the100ms boundary. */
        owner.state.phase=ECHOY_RECORDING;
        for(unsigned i=0;i<4;i++){
            test_now+=1000;pointer(NULL,220,200,true,ui);pointer(NULL,220,200,false,ui);
        }
        assert(tap_actions==6 && last_tap_value==0); /* Capture bypasses both guards. */
        owner.state.phase=ECHOY_IDLE;
        assert(act(ui,ECHOY_ACT_PAUSE,0,test_now));
        pointer(NULL,220,200,true,ui);pointer(NULL,220,200,false,ui);
        assert(tap_actions==7); /* Explicit Pause clears the preview input gate. */
    } else if (!strcmp(argv[1],"input-during-draw")) {
        set_hold(false);pump_until_published(ui,0);
        unsigned before=stream_submissions;
        set_hold(true);ui->frames_dirty=true;tick(NULL,ui);
        for (unsigned i=0;i<10;++i) {
            test_now+=100000;
            pointer(NULL,220,200,true,ui);
            pointer(NULL,220,200,false,ui);
            tick(NULL,ui);
        }
        assert(tap_actions==10 && last_tap_at==test_now && stream_submissions==before);
        assert(main_invalidations==0 && feedback_invalidations>0 && ui->home_epoch==epoch);
        set_hold(false);
        pump_until_published(ui,before);
        assert(art_visible && ui->home_published_epoch==epoch);
    } else if (!strcmp(argv[1],"continuous-progress")) {
        owner.state.phase=ECHOY_PLAYING;
        owner.state.start_us=test_now;
        tick(NULL,ui);
        epoch=ui->home_epoch;
        for (unsigned i=0;i<20;++i) {
            test_now+=40000;
            tick(NULL,ui);
            assert(ui->home_epoch==epoch && "Progress/energy must not invalidate in-flight work");
        }
        set_hold(false);
        pump_until_published(ui,0);
        assert(ui->home_published_epoch==epoch && art_visible);
    } else if (!strcmp(argv[1],"role-replaces-inflight")) {
        owner.state.role=ECHOY_VOICE;
        owner.state.phrase=2;
        ++owner.state.committed_revision;
        assert(echoy_compose_mask(0x88,ECHOY_VOICE,2,.5,&owner.plan));
        tick(NULL,ui);
        assert(ui->home_epoch!=epoch && !art_visible);
        set_hold(false);
        pump_until_published(ui,0);
        assert(stream_marker==ECHOY_VOICE+1 && ui->home_published_role==ECHOY_VOICE && art_visible);
    } else if (!strcmp(argv[1],"static-final-queue-full")) {
        owner.state.phase=ECHOY_PLAYING;
        owner.state.start_us=test_now;
        tick(NULL,ui);
        owner.state.phase=ECHOY_IDLE;
        test_now+=1000000;
        tick(NULL,ui);
        epoch=ui->home_epoch;
        stream_full=true;
        set_hold(false);
        for (unsigned i=0;i<100;++i) {
            tick(NULL,ui);
            struct timespec yield={.tv_nsec=1000000}; nanosleep(&yield,NULL);
        }
        assert(!stream_submissions && !atomic_load(&ui->error) && ui->home_epoch==epoch);
        stream_full=false;
        pump_until_published(ui,0);
        assert(ui->home_published_epoch==epoch);
        pthread_mutex_lock(&draw_lock);
        assert(!last_draw_active && last_draw_energy==0);
        pthread_mutex_unlock(&draw_lock);
    } else if (!strcmp(argv[1],"route-role-visibility")) {
        set_hold(false);
        pump_until_published(ui,0);
        assert(art_visible);
        set_hold(true);
        select_route(ui,ROLE);
        refresh(ui,test_now,true);
        owner.state.role=ECHOY_HARMONY;
        ++owner.state.committed_revision;
        assert(echoy_compose_mask(0x88,ECHOY_HARMONY,1,.5,&owner.plan));
        select_route(ui,HOME);
        refresh(ui,test_now,true);
        assert(!art_visible && ui->home_waiting_visibility);
        unsigned before=stream_submissions;
        set_hold(false);
        pump_until_published(ui,before);
        assert(art_visible && stream_marker==ECHOY_HARMONY+1);
    } else if (!strcmp(argv[1],"worker-stop-retry")) {
        assert(echoy_ui_delete(ui)==ESP_GSP_ERR_TIMEOUT && ui->home_producer);
        assert(atomic_load(&ui->home_closing) && !model_deletes);
        set_hold(false);
        assert(echoy_ui_delete(ui)==ESP_GSP_OK);
        close_gsp();
        puts("PASS worker timeout retained context for retry");
        return 0;
    } else if (!strcmp(argv[1],"release-closed-retry")) {
        close_gsp();
        echoy_ui_release_closed(ui);
        assert(ui->home_producer && atomic_load(&ui->home_closing));
        set_hold(false);
        echoy_ui_release_closed(ui);
        puts("PASS already-closed GSP does not free a live worker context");
        return 0;
    } else if (!strcmp(argv[1],"active-owner-close-retry")) {
        assert(home_enter(ui));
        assert(echoy_ui_delete(ui)==ESP_GSP_ERR_INVALID_STATE && ui->home_producer);
        home_leave(ui);
        assert(!home_enter(ui));
        set_hold(false);
        assert(echoy_ui_delete(ui)==ESP_GSP_OK);
        close_gsp();
        puts("PASS lifecycle excludes producer calls already in progress");
        return 0;
    } else if (!strcmp(argv[1],"canvas-stop-retry")) {
        set_hold(false);
        pump_until_published(ui,0);
        fail_stream_stop=true;
        assert(echoy_ui_delete(ui)==ESP_GSP_ERR_TIMEOUT && stream_release && ui->home_producer);
        fail_stream_stop=false;
        assert(echoy_ui_delete(ui)==ESP_GSP_OK && !stream_release);
        close_gsp();
        puts("PASS failed canvas stop retains GSP-borrowed storage");
        return 0;
    } else if (!strcmp(argv[1],"publish-error")) {
        stream_error=ESP_GSP_ERR_INVALID_STATE;
        set_hold(false);
        for (unsigned i=0;i<5000 && !atomic_load(&ui->error);++i) {
            tick(NULL,ui);
            struct timespec yield={.tv_nsec=1000000}; nanosleep(&yield,NULL);
        }
        assert(atomic_load(&ui->error)==ESP_GSP_ERR_INVALID_STATE && !stream_submissions);
        assert(ui->condition==ECHOY_AUDIO_FAULT);
        assert(echoy_ui_delete(ui)==ESP_GSP_OK);
        close_gsp();
        puts("PASS immediate publication errors remain explicit");
        return 0;
    } else assert(!"Unknown async UI case");
    assert(!main_invalidations && !atomic_load(&ui->error));
    assert(echoy_ui_delete(ui)==ESP_GSP_OK);
    close_gsp();
    puts("PASS");
    return 0;
}
