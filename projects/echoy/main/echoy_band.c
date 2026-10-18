#include "echoy_band.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "esp_cpu.h"
#endif
#define N ECHOY_ARRANGE_TILE_COUNT
#define BAR INT64_C(2500000)
#define FADE INT64_C(8000)
#define RECORDS 8
#define CUES 96
#define PREVIEW_FIRST_US INT64_C(350000)
#define PREVIEW_MIN_US INT64_C(80000)
#define PREVIEW_MAX_US INT64_C(1200000)
#define PREVIEW_GRAIN_GAP_US INT64_C(350000)
#define PREVIEW_FADE_FRAMES 384u
_Static_assert(PREVIEW_MAX_US * ECHOY_AUDIO_SAMPLE_RATE / 1000000 <=
               ECHOY_AUDIO_LOOP_FRAMES, "Preview windows wrap at most once");
struct bank {
    unsigned refs;
    echoy_audio_loops_t *loops;
};
struct playback {
    struct bank *bank;
    int64_t at, stop;
    bool loop, fade;
};
struct cue {
    int tile;
    const echoy_audio_buffer_t *buffer;
    int64_t at, stop_at;
    size_t offset,frames;
    bool used, guide,pad_preview;
};
struct voice {
    echoy_model_handle_t model;
    struct bank *prepared;
    struct playback play[RECORDS];
    uint16_t observed_seed;
    double volume, smoothed_volume;
    unsigned battery;
    echoy_condition_t condition;
    bool failed, automatic_preview,recorded_reply_pending;
    uint32_t preview_rng;
    int64_t preview_tap_us;
    int64_t preview_next_at;
    unsigned preview_tap_role;
    bool preview_has_tap;
    uint8_t preview_order[4][ECHOY_PREVIEW_COUNT],preview_left[4],preview_last[4];
};
struct echoy_band_job {
    echoy_arrange_job_t arrangement;
    uint32_t generation;
    uint8_t inaudible_mask;
    struct bank *banks[N];
    bool ok, staged, calibrated, bass_cut_attempted, speaker_voicing_attempted, handed_off;
};
struct queued_action { echoy_action_t action; double value; };
struct cooperation {
    /* Cold musical snapshots must not exhaust internal RAM used by PCM/Wi-Fi. */
    echoy_plan_t upstream, candidate, accepted_upstream;
    bool accepted_upstream_valid;
    bool upstream_valid, upstream_started, upstream_playing;
    int64_t upstream_origin;
    bool candidate_exists, candidate_ready, candidate_failed, candidate_affects;
    uint64_t candidate_revision, settled_revision;
    bool active, follower, recovering, hold, incoming;
    bool peer_looping, align_boundary;
    uint32_t local_start_serial, local_edit_serial;
    uint32_t generation, session, offered, armed, authorized, committed, failed, aborted, last_transaction;
    uint64_t bound_revision, armed_revision;
    int64_t armed_at;
    struct queued_action queued[32];
    unsigned queued_count;
    bool seed_queued;
    uint16_t queued_seed;
};
struct echoy_band {
    echoy_arrange_t *arrange;
    struct voice voices[N];
    echoy_audio_buffer_t *cues[3];
    echoy_audio_preview_pool_t previews[4];
    struct cue scheduled[CUES];
    echoy_band_job_t *ready;
    uint64_t issued;
    uint32_t job_generation;
    bool english, reduced_motion;
    bool upstream_connected;
    bool upstream_requested, deferred[N];
    size_t requested_count;
    echoy_arrange_edge_t requested[ECHOY_ARRANGE_MAX_EDGES];
    struct cooperation *cooperation;
    bool profiled;
    echoy_band_profile_t profile;
    int64_t render_until;
    /* Owner-only scratch: keep the 7 KiB empty plan off embedded task stacks. */
    echoy_plan_t empty_plan;
};
static bool valid_tile(int id)
{
    return id >= 0 && id < N;
}
static void release(struct bank *p)
{
    if (p && --p->refs == 0) {
        echoy_audio_free_loops(p->loops);
        free(p);
    }
}
static struct bank *retain(struct bank *p)
{
    if (p) {
        ++p->refs;
    }
    return p;
}
void echoy_band_discard_job(echoy_band_job_t *j)
{
    if (j) {
        for (int i = 0; i < N; i++) {
            release(j->banks[i]);
        }
        free(j);
    }
}
static bool capture(echoy_phase_t p)
{
    return p == ECHOY_COUNT_IN || p == ECHOY_RECORDING;
}
static void finish_cue(struct cue *c)
{
    memset(c,0,sizeof(*c));
}
static void stop_voice(echoy_band_t *b, int id, int64_t now)
{
    b->voices[id].preview_has_tap=false;
    b->voices[id].preview_next_at=0;
    for (unsigned i = 0; i < RECORDS; i++) {
        if (b->voices[id].play[i].bank) {
            struct playback *p = &b->voices[id].play[i];
            if (!p->stop || now < p->stop) {
                p->stop = now > 0 ? now : 1;
            }
        }
    }
    for (unsigned i = 0; i < CUES; i++) {
        if (b->scheduled[i].used && b->scheduled[i].tile == id) {
            finish_cue(&b->scheduled[i]);
        }
    }
}
static void reap(struct voice *v, int64_t now)
{
    for (unsigned i = 0; i < RECORDS; i++) {
        struct playback *p = &v->play[i];
        if (!p->bank) {
            continue;
        }
        if ((p->stop && now >= p->stop + FADE) || (!p->loop && now >= p->at + BAR)) {
            release(p->bank);
            memset(p, 0, sizeof(*p));
        }
    }
}
static bool play_voice(echoy_band_t *b, int id, struct bank *bank, int64_t at, bool loop,
                       bool crossfade)
{
    struct voice *v = &b->voices[id];
    reap(v, at - FADE);
    unsigned slot = RECORDS;
    for (unsigned i = 0; i < RECORDS; i++) {
        if (!v->play[i].bank) {
            slot = i;
            break;
        }
    }
    if (slot == RECORDS || !bank) {
        return false;
    }
    for (unsigned i = 0; i < RECORDS; i++) {
        if (v->play[i].bank && !v->play[i].stop) {
            v->play[i].stop = at;
        }
    }
    v->play[slot] =
        (struct playback){.bank = retain(bank), .at = at, .loop = loop, .fade = crossfade};
    return true;
}
static bool schedule_cue(echoy_band_t *b, int id, const echoy_audio_buffer_t *buffer, int64_t at, bool guide)
{
    for (unsigned i = 0; i < CUES; i++) {
        if (!b->scheduled[i].used) {
            b->scheduled[i] =
                (struct cue){.tile = id, .buffer = buffer, .at = at, .used = true, .guide = guide};
            return true;
        }
    }
    return false;
}
static uint32_t preview_random(struct voice *v)
{
    uint32_t x=v->preview_rng;x^=x<<13;x^=x>>17;x^=x<<5;
    return v->preview_rng=x;
}
static bool schedule_preview(echoy_band_t *b,int id,unsigned role,int64_t now,int64_t at)
{
    struct voice *v=&b->voices[id];
    /* Ignore excess input, without shortening sound or advancing random/time
     * history. The actual scheduled onset also guards a backed-up input queue. */
    if(at<v->preview_next_at)return true;
    unsigned slot=CUES;
    for(unsigned i=0;i<CUES;i++)if(!b->scheduled[i].used){slot=i;break;}
    /* When input timestamps queue before one output block, supersede an older
     * pending local preview rather than making a simultaneous burst. */
    if(slot==CUES)for(unsigned i=0;i<CUES;i++){
        struct cue *c=&b->scheduled[i];
        if(!c->pad_preview || c->tile!=id)continue;
        bool ended=at>=c->at && at-c->at>=(int64_t)c->frames*1000000/ECHOY_AUDIO_SAMPLE_RATE;
        bool faded=c->stop_at && at>=c->stop_at && at-c->stop_at>=16000;
        if(c->at>=at || ended || faded){slot=i;break;}
    }
    if(slot==CUES)return false;
    int64_t duration=PREVIEW_FIRST_US;bool granular=false;
    if(v->preview_has_tap && v->preview_tap_role==role && now>=v->preview_tap_us){
        int64_t gap=now-v->preview_tap_us;
        duration=gap>=1500000 ? PREVIEW_MAX_US : gap*4/5;
        if(duration<PREVIEW_MIN_US)duration=PREVIEW_MIN_US;
        granular=gap<=PREVIEW_GRAIN_GAP_US;
    }
    if(!v->preview_rng){
        v->preview_rng=(uint32_t)now^(uint32_t)((uint64_t)now>>32)^((id+1u)*UINT32_C(0x9e3779b9));
        if(!v->preview_rng)v->preview_rng=1;
    }
    uint8_t *order=v->preview_order[role];
    if(!v->preview_left[role]){
        for(unsigned i=0;i<ECHOY_PREVIEW_COUNT;i++)order[i]=(uint8_t)i;
        for(unsigned i=ECHOY_PREVIEW_COUNT-1;i;i--){
            unsigned j=preview_random(v)%(i+1);uint8_t swap=order[i];order[i]=order[j];order[j]=swap;
        }
        if(order[ECHOY_PREVIEW_COUNT-1]==v->preview_last[role]){
            uint8_t swap=order[0];order[0]=order[ECHOY_PREVIEW_COUNT-1];order[ECHOY_PREVIEW_COUNT-1]=swap;
        }
        v->preview_left[role]=ECHOY_PREVIEW_COUNT;
    }
    unsigned choice=order[--v->preview_left[role]];v->preview_last[role]=(uint8_t)choice;
    const echoy_audio_preview_pool_t *pool=&b->previews[role];
    size_t frames=(size_t)(duration*ECHOY_AUDIO_SAMPLE_RATE/1000000);
    if(granular && frames>pool->short_frames[choice])frames=pool->short_frames[choice];
    for(unsigned i=0;i<CUES;i++){
        struct cue *c=&b->scheduled[i];
        if(!c->used || !c->pad_preview || c->tile!=id)continue;
        if(c->at>=at)finish_cue(c);
        else if(!c->stop_at || at<c->stop_at)c->stop_at=at;
    }
    b->scheduled[slot]=(struct cue){.tile=id,.buffer=pool->phrase,.offset=pool->source_frames[choice],
                                  .frames=frames,.at=at,.used=true,.pad_preview=true};
    v->preview_tap_us=now;v->preview_tap_role=role;v->preview_has_tap=true;
    v->preview_next_at=at+ECHOY_PREVIEW_MIN_INTERVAL_US;
    return true;
}
static bool cue(echoy_band_t *b, int id, echoy_audio_cue_t kind, int64_t at, bool guide)
{ return schedule_cue(b,id,b->cues[kind],at,guide); }
static void cancel_auto(echoy_band_t *b, int except, int64_t now)
{
    for (int i = 0; i < N; i++) {
        if (i != except && !b->deferred[i] && b->voices[i].automatic_preview) {
            echoy_snapshot_t s;
            echoy_model_get_snapshot(b->voices[i].model, &s);
            if (s.phase == ECHOY_PREPARING) {
                echoy_model_pause(b->voices[i].model);
                echoy_arrange_mute(b->arrange, i);
                stop_voice(b, i, now);
            }
            b->voices[i].automatic_preview = false;
            b->voices[i].recorded_reply_pending = false;
        }
    }
}
static bool set_seed(echoy_band_t *b, int id, uint16_t bits)
{
    struct cooperation *c=b->cooperation;
    if (id==ECHOY_TILE_A && c->active && c->align_boundary && c->armed &&
        c->committed!=c->armed) {
        /* Recording may finish while a downstream-only edit has frozen this
         * owner's unchanged bank. Publish that result after the boundary. */
        c->queued_seed=bits; c->seed_queued=true;
        return true;
    }
    echoy_seed_note_t notes[16];
    size_t n = 0;
    for (unsigned i = 0; i < 16; i++) {
        if (bits & (1u << i)) {
            notes[n++] = (echoy_seed_note_t){.step = i, .velocity = .74, .kind = ECHOY_KICK};
        }
    }
    bool ok=echoy_arrange_set_seed(b->arrange,id,notes,n);
    if (ok && id==ECHOY_TILE_A && !++b->cooperation->local_edit_serial)
        ++b->cooperation->local_edit_serial;
    return ok;
}
static bool wanted_parent(echoy_band_t *b, int id)
{
    size_t n = 0;
    const echoy_arrange_edge_t *e = echoy_arrange_get_edges(b->arrange, true, &n);
    for (size_t i = 0; i < n; i++) {
        if (e[i].right == id) {
            return true;
        }
    }
    return false;
}
static void sync_plans(echoy_band_t *b)
{
    for (int i = 0; i < N; i++) {
        const echoy_arrange_tile_t *t = echoy_arrange_get_tile(b->arrange, i);
        const echoy_arrange_clock_t *c = echoy_arrange_get_clock(b->arrange, t->clock_id);
        b->empty_plan.role = t->role;
        b->empty_plan.phrase = t->phrase;
        b->empty_plan.density = t->density;
        b->empty_plan.duration = 2.5;
        echoy_model_apply_external(
            b->voices[i].model, t->current.valid ? &t->current.music : &b->empty_plan,
            (uint32_t)echoy_arrange_revision(b->arrange),
            !b->deferred[i] && ((t->current.valid && t->current.source != ECHOY_TILE_NONE) || wanted_parent(b, i)),
            c->origin_us, c->started);
    }
}
echoy_band_t *echoy_band_create_configured(bool speaker,const float gains[4])
{
    echoy_band_t *b = calloc(1, sizeof(*b));
    if (!b) {
        return NULL;
    }
#ifdef ESP_PLATFORM
    ESP_LOGI("echoy_band", "scheduler bytes=%u internal=%u default_internal_largest=%u",
             (unsigned)sizeof(*b), (unsigned)esp_ptr_internal(b),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT));
#endif
    b->issued = UINT64_MAX;
#ifdef ESP_PLATFORM
    b->cooperation = heap_caps_calloc(1, sizeof(*b->cooperation), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    b->cooperation = calloc(1, sizeof(*b->cooperation));
#endif
    b->arrange = echoy_arrange_create();
    if (!b->arrange || !b->cooperation) {
        echoy_arrange_delete(b->arrange);
        free(b->cooperation);
        free(b);
        return NULL;
    }
    for (int i = 0; i < N; i++) {
        b->voices[i].model = echoy_model_create_managed();
        b->voices[i].volume = b->voices[i].smoothed_volume = .65;
        b->voices[i].battery = 72;
        if (!b->voices[i].model) {
            echoy_band_delete(b);
            return NULL;
        }
#ifdef ESP_PLATFORM
        ESP_LOGI("echoy_band", "ECHOY_MODEL_STORAGE tile=%c bytes=%u internal=%u external=%u "
                              "internal_free=%u internal_largest=%u",
                 'A' + i, (unsigned)heap_caps_get_allocated_size(b->voices[i].model),
                 (unsigned)esp_ptr_internal(b->voices[i].model),
                 (unsigned)esp_ptr_external_ram(b->voices[i].model),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
        echoy_model_select_role(b->voices[i].model, echoy_arrange_get_tile(b->arrange, i)->role, 0);
    }
    for (unsigned i = 0; i < 3; i++) {
        if (echoy_audio_render_cue((echoy_audio_cue_t)i, &b->cues[i]) != ECHOY_AUDIO_OK) {
            echoy_band_delete(b);
            return NULL;
        }
    }
    if (!echoy_band_prepare_previews(b,speaker,gains)) {
        echoy_band_delete(b);return NULL;
    }
    sync_plans(b);
    return b;
}
echoy_band_t *echoy_band_create(void)
{ return echoy_band_create_configured(false,NULL); }
bool echoy_band_prepare_previews(echoy_band_t *b, bool speaker, const float gains[4])
{
    if (!b || b->render_until) return false;
    for (unsigned i=0;i<CUES;i++) if(b->scheduled[i].used)return false;
    echoy_audio_preview_pool_t prepared[4]={0};
    for(unsigned role=0;role<4;role++) {
        if(echoy_audio_render_preview_pool((echoy_role_t)role,speaker,gains?gains[role]:1,
                                      &prepared[role])!=ECHOY_AUDIO_OK) {
            for(unsigned i=0;i<4;i++)echoy_audio_free_preview_pool(&prepared[i]);
            return false;
        }
    }
    for(unsigned role=0;role<4;role++) {
        echoy_audio_free_preview_pool(&b->previews[role]);b->previews[role]=prepared[role];
        for(unsigned id=0;id<N;id++){
            b->voices[id].preview_left[role]=0;b->voices[id].preview_last[role]=ECHOY_PREVIEW_COUNT;
            b->voices[id].preview_has_tap=false;
        }
    }
    return true;
}
void echoy_band_delete(echoy_band_t *b)
{
    if (!b) {
        return;
    }
    echoy_band_discard_job(b->ready);
    for(unsigned i=0;i<CUES;i++)finish_cue(&b->scheduled[i]);
    for (int i = 0; i < N; i++) {
        for (unsigned j = 0; j < RECORDS; j++) {
            release(b->voices[i].play[j].bank);
        }
        release(b->voices[i].prepared);
        echoy_model_delete(b->voices[i].model);
    }
    for (unsigned i = 0; i < 3; i++) {
        echoy_audio_free_buffer(b->cues[i]);
    }
    for(unsigned i=0;i<4;i++)echoy_audio_free_preview_pool(&b->previews[i]);
    echoy_arrange_delete(b->arrange);
    free(b->cooperation);
    free(b);
}
const echoy_arrange_t *echoy_band_arrangement(const echoy_band_t *b)
{
    return b ? b->arrange : NULL;
}
static bool sync_clock(echoy_band_t *b, echoy_tile_id_t clock, int64_t origin, bool started, int64_t now)
{
    const echoy_arrange_clock_t *old = echoy_arrange_get_clock(b->arrange, clock);
    if (!old || origin < 0) return false;
    int64_t delta = 0;
    if (old->started && started) {
        /* Equivalent origins separated by whole bars must not move playback. */
        delta = (origin - old->origin_us) % BAR;
        if (delta > BAR / 2) delta -= BAR;
        if (delta < -BAR / 2) delta += BAR;
    }
    if (!echoy_arrange_clock_sync(b->arrange, clock, origin, started, now)) return false;
    for (int i = 0; i < N; ++i) {
        if (echoy_arrange_get_tile(b->arrange, i)->clock_id != clock) continue;
        struct voice *v = &b->voices[i];
        for (unsigned j = 0; j < RECORDS; ++j) {
            struct playback *p = &v->play[j];
            if (p->bank && p->loop) {
                bool running = p->at <= now;
                p->at += delta;
                /* Keep an active loop active: represent its corrected phase
                 * using an earlier equivalent bar, not a new future start. */
                if (running && p->at > now) p->at -= BAR;
            }
        }
        echoy_model_sync_clock(v->model, origin, started);
        const echoy_arrange_pending_t *pending = echoy_arrange_get_pending(b->arrange);
        if (pending && (pending->job.changed_mask & (1u << i)))
            echoy_model_set_external_pending(v->model, true, pending->at_us);
    }
    return true;
}

bool echoy_band_establish_clock(echoy_band_t *b, int64_t now)
{
    if (!b || now < 0 || now > INT64_MAX - 150000) return false;
    echoy_tile_id_t clock = echoy_arrange_get_tile(b->arrange, ECHOY_TILE_A)->clock_id;
    const echoy_arrange_clock_t *c = echoy_arrange_get_clock(b->arrange, clock);
    return c->started || sync_clock(b, clock, now + 150000, true, now);
}

static bool join_busy(echoy_band_t *b, int id)
{
    echoy_snapshot_t s;
    echoy_model_get_snapshot(b->voices[id].model, &s);
    return capture(s.phase) || s.phase == ECHOY_PREVIEW ||
           (s.phase == ECHOY_PREPARING && !s.continuous);
}
static bool joint_hold(const echoy_band_t *b)
{
    const struct cooperation *c=b->cooperation;
    return c->active && !b->deferred[ECHOY_TILE_A] && (c->hold || c->incoming);
}
static bool retained_start(const echoy_band_t *b,int id,int64_t now)
{
    const struct cooperation *c=b->cooperation;
    const echoy_arrange_tile_t *t=echoy_arrange_get_tile(b->arrange,id);
    return id==ECHOY_TILE_A && c->active && c->align_boundary && c->armed &&
        c->committed!=c->armed && now<c->armed_at && b->voices[id].prepared &&
        t->current.valid && t->current.music.count;
}
static void unarm(echoy_band_t *b)
{
    struct cooperation *c=b->cooperation;
    echoy_arrange_unstage(b->arrange);
    if (b->ready) b->ready->staged=false;
    c->armed=c->authorized=0;
    c->armed_revision=0;
    c->armed_at=0;
}
static void discard_candidate(echoy_band_t *b, bool failed)
{
    struct cooperation *c=b->cooperation;
    unarm(b);
    ++b->job_generation;
    echoy_band_discard_job(b->ready);
    b->ready=NULL;
    c->candidate_ready=false;
    c->candidate_failed=failed;
    c->candidate_exists=failed;
    b->issued=echoy_arrange_revision(b->arrange);
    for (int i=0;i<N;++i) {
        echoy_model_set_external_pending(b->voices[i].model,false,0);
        if (failed) {
            echoy_snapshot_t s;
            echoy_model_get_snapshot(b->voices[i].model,&s);
            if (s.phase==ECHOY_PREPARING) {
                echoy_model_pause(b->voices[i].model);
                b->voices[i].automatic_preview=false;b->voices[i].recorded_reply_pending=false;
            }
        }
        if (i==ECHOY_TILE_A && failed) b->voices[i].failed=true;
    }
}
static void replay_actions(echoy_band_t *b, int64_t now)
{
    struct cooperation *c=b->cooperation;
    /* No user action can enqueue concurrently with the owner. */
    unsigned n=c->queued_count;
    c->queued_count=0;
    for (unsigned i=0;i<n;++i)
        echoy_band_action(b,ECHOY_TILE_A,c->queued[i].action,c->queued[i].value,now);
    if (c->seed_queued) {
        uint16_t seed=c->queued_seed; c->seed_queued=false;
        if (set_seed(b,ECHOY_TILE_A,seed))
            echoy_model_set_external_pending(b->voices[ECHOY_TILE_A].model,true,0);
        else b->voices[ECHOY_TILE_A].failed=true;
    }
}
static bool apply_requested_edges(echoy_band_t *b, int64_t now)
{
    echoy_arrange_edge_t effective[ECHOY_ARRANGE_MAX_EDGES];
    size_t count = 0;
    bool deferred[N] = {0};
    if (b->upstream_requested && !b->upstream_connected && join_busy(b, ECHOY_TILE_A))
        deferred[ECHOY_TILE_A] = true;
    for (size_t j = 0; j < b->requested_count; ++j) {
        echoy_arrange_edge_t e = b->requested[j];
        if (!echoy_arrange_get_tile(b->arrange,e.left)->present ||
            !echoy_arrange_get_tile(b->arrange,e.right)->present) continue;
        if (!wanted_parent(b,e.right) && join_busy(b,e.right)) {
            deferred[e.right] = true;
        } else effective[count++] = e;
    }
    memcpy(b->deferred,deferred,sizeof(deferred));
    bool parents[N];
    for (int i=0;i<N;++i) parents[i]=wanted_parent(b,i);
    uint64_t before = echoy_arrange_revision(b->arrange);
    if (!echoy_arrange_set_edges(b->arrange,effective,count)) return false;
    for (int i=0;i<N;++i) if (parents[i] && !wanted_parent(b,i)) {
        echoy_arrange_release(b->arrange,i);
        const echoy_arrange_tile_t *t=echoy_arrange_get_tile(b->arrange,i);
        const echoy_arrange_clock_t *clock=echoy_arrange_get_clock(b->arrange,t->clock_id);
        echoy_model_set_following(b->voices[i].model,false);
        echoy_model_sync_clock(b->voices[i].model,clock->origin_us,clock->started);
    }
    if (before != echoy_arrange_revision(b->arrange)) cancel_auto(b,-1,now);
    for (int i=0;i<N;++i) {
        bool following=!b->deferred[i] && wanted_parent(b,i);
        echoy_model_set_following(b->voices[i].model,following);
    }
    return true;
}

bool echoy_band_set_upstream(echoy_band_t *b, bool connected, const echoy_plan_t *plan,
                             bool started, int64_t origin, bool playing, int64_t now)
{
    if (!b || now < 0 || (started && origin < 0)) return false;
    struct cooperation *c=b->cooperation;
    if (!connected) {
        b->upstream_requested=false;
        b->deferred[ECHOY_TILE_A]=false;
        c->upstream_valid=false;
        if (!b->upstream_connected) return true;
        if (!echoy_band_set_edges(b, NULL, 0, now) ||
            !echoy_arrange_set_present(b->arrange, ECHOY_TILE_B, false)) return false;
        b->upstream_connected = false;
        return true;
    }
    b->upstream_requested=true;
    if (plan) { c->upstream=*plan; c->upstream_valid=true; }
    c->upstream_started=started; c->upstream_origin=origin; c->upstream_playing=playing;
    if (!b->upstream_connected && join_busy(b,ECHOY_TILE_A)) {
        b->deferred[ECHOY_TILE_A]=true;
        return true;
    }
    if (c->upstream_valid) plan=&c->upstream;
    b->deferred[ECHOY_TILE_A]=false;
    if (!b->upstream_connected) {
        /* An earlier peer's partial transfer cannot become the new upstream. */
        b->empty_plan = (echoy_plan_t){.role=ECHOY_PULSE,.phrase=1,.density=.5,.duration=2.5};
        if (!echoy_arrange_import(b->arrange, ECHOY_TILE_B, plan ? plan : &b->empty_plan) ||
            !echoy_arrange_set_present(b->arrange, ECHOY_TILE_B, true)) return false;
        const echoy_arrange_edge_t edge = {ECHOY_TILE_B,ECHOY_TILE_A};
        if (!echoy_band_set_edges(b, &edge, 1, now)) return false;
        b->upstream_connected = true;
    } else if (plan && !echoy_arrange_import(b->arrange, ECHOY_TILE_B, plan)) {
        return false;
    }
    if (!echoy_arrange_peer_loop(b->arrange, ECHOY_TILE_B, playing)) return false;
    return !started || sync_clock(b, ECHOY_TILE_B, origin, true, now);
}
bool echoy_band_set_edges(echoy_band_t *b, const echoy_arrange_edge_t *edges, size_t count,
                          int64_t now)
{
    if (!b || now < 0 || !echoy_arrange_edges_valid(edges, count)) {
        return false;
    }
    if (count) memcpy(b->requested,edges,count*sizeof(*edges));
    b->requested_count=count;
    return apply_requested_edges(b,now);
}
bool echoy_band_link_read(echoy_band_t *b, echoy_link_view_t *out)
{
    if (!b || !out) return false;
    const struct cooperation *c=b->cooperation;
    const echoy_arrange_tile_t *t=echoy_arrange_get_tile(b->arrange,ECHOY_TILE_A);
    const echoy_arrange_clock_t *clock=echoy_arrange_get_clock(b->arrange,t->clock_id);
    uint64_t rev=echoy_arrange_revision(b->arrange);
    memset(out,0,sizeof(*out));
    out->available=t->present && b->voices[0].condition==ECHOY_NORMAL;
    out->connection_generation=c->generation;
    out->session=c->session;
    out->deferred=b->deferred[0];
    out->following=b->upstream_connected;
    out->clock_started=clock->started;
    out->local_start_serial=c->local_start_serial;
    out->local_edit_serial=c->local_edit_serial;
    out->busy=join_busy(b,ECHOY_TILE_A);
    out->clock_origin_us=clock->origin_us;
    out->looping=t->loop;
    out->current=t->current.valid ? t->current.music :
        (echoy_plan_t){.role=t->role,.phrase=t->phrase,.density=t->density,.duration=2.5};
    out->requested_revision=rev;
    out->proposal_pending=rev!=c->settled_revision || c->candidate_exists;
    if (c->candidate_exists && c->candidate_revision==rev) {
        out->proposal_compiled=true;
        out->proposal=c->candidate;
        out->proposal_revision=rev;
        out->proposal_ready=c->candidate_ready;
        out->proposal_failed=c->candidate_failed;
    }
    out->offered_transaction=c->offered;
    out->offered_revision=c->follower ? rev : c->bound_revision;
    bool matching=c->follower || c->bound_revision==rev;
    out->offer_ready=c->offered && !out->deferred && out->available &&
        ((matching && c->candidate_exists && c->candidate_revision==rev && c->candidate_ready) ||
         (!c->follower && !c->bound_revision && !out->proposal_pending));
    out->offer_failed=c->offered && (c->failed==c->offered ||
        (matching && c->candidate_revision==rev && c->candidate_failed));
    out->armed_transaction=c->armed;
    out->authorized_transaction=c->authorized;
    out->armed_revision=c->armed_revision;
    out->armed_at_us=c->armed_at;
    out->committed_transaction=c->committed;
    out->failed_transaction=c->failed;
    return true;
}
bool echoy_band_link_update(echoy_band_t *b, const echoy_link_update_t *u, int64_t now)
{
    if (!b || !u || now<0 || (u->clock_started && u->clock_origin_us<0)) return false;
    struct cooperation *c=b->cooperation;
    if (c->generation && u->connection_generation!=c->generation &&
        (int32_t)(u->connection_generation-c->generation)<=0) return false;
    bool failed_held_source=u->command==ECHOY_LINK_ABORT && u->failed && !u->following &&
        u->attached && c->active && !c->follower && u->session==c->session &&
        c->hold && !b->deferred[0] && c->candidate_exists &&
        c->candidate_revision==u->revision &&
        !(c->authorized && c->committed!=c->authorized);
    bool new_session=u->attached && (!c->active || u->session!=c->session || u->following!=c->follower);
    bool leaving=c->active && (!u->attached || new_session);
    if (leaving) {
        bool was_follower=c->follower;
        if (was_follower && c->incoming && !b->deferred[0]) {
            discard_candidate(b,false);
            if (u->attached) {
                /* New radio-session admission may overwrite an unconsumed
                 * ABORT. Later edits must use the committed upstream, never
                 * the proposal that belonged to the abandoned session. */
                b->empty_plan=(echoy_plan_t){.role=ECHOY_PULSE,.phrase=1,.density=.5,.duration=2.5};
                const echoy_plan_t *old=c->accepted_upstream_valid ? &c->accepted_upstream : &b->empty_plan;
                echoy_arrange_import(b->arrange,ECHOY_TILE_B,old);
                c->upstream=*old; c->upstream_valid=true;
                b->issued=echoy_arrange_revision(b->arrange);
            }
        } else unarm(b);
        c->offered=c->committed=c->failed=c->aborted=c->last_transaction=0;
        c->incoming=c->hold=false;
        c->active=false;
        if (was_follower && (!u->attached || !u->following)) {
            echoy_band_set_upstream(b,false,NULL,false,0,false,now);
            c->accepted_upstream_valid=false;
        }
        replay_actions(b,now);
    }
    c->generation=u->connection_generation;
    c->session=u->session;
    c->active=u->attached;
    c->follower=u->following;
    c->recovering=u->recovering;
    c->hold=u->hold_changes;
    c->peer_looping=u->attached && u->peer_looping;
    if (!u->attached) return true;
    bool clock_current=!u->align_boundary || !c->committed ||
        u->clock_committed_transaction==c->committed ||
        (int32_t)(u->clock_committed_transaction-c->committed)>0;
    if (u->align_boundary && c->armed && c->committed!=c->armed &&
        u->clock_committed_transaction==c->armed && now<c->armed_at)
        clock_current=false;
    bool acknowledged=u->clock_start_ack==c->local_start_serial && clock_current;
    if (!u->following) {
        if (!echoy_band_establish_clock(b,now)) return false;
    } else {
        /* Contact feedback is immediate. Only OFFER may replace its musical
         * payload; repeated clock/status snapshots cannot replay an old plan. */
        if (!echoy_band_set_upstream(b,true,NULL,u->clock_started && (!u->clock_sync || acknowledged),
                                    u->clock_origin_us,u->looping,now))
            return false;
    }
    if (u->clock_sync && acknowledged && !b->deferred[0]) {
        echoy_tile_id_t clock=echoy_arrange_get_tile(b->arrange,ECHOY_TILE_A)->clock_id;
        if (!sync_clock(b,clock,u->clock_origin_us,u->clock_started,now)) return false;
    }
    if (u->command==ECHOY_LINK_NONE) return true;
    if (!u->transaction) return false;
    if (u->transaction==c->aborted) {
        if (u->command!=ECHOY_LINK_ABORT) return false;
        if (u->failed) c->failed=u->transaction;
        return true;
    }
    if (c->last_transaction && u->transaction!=c->last_transaction &&
        (int32_t)(u->transaction-c->last_transaction)<=0) return false;
    if (u->command==ECHOY_LINK_BEGIN || u->command==ECHOY_LINK_OFFER) {
        if (c->offered==u->transaction) return true;
        if (c->authorized && c->committed!=c->authorized) return false;
        if (u->command==ECHOY_LINK_BEGIN &&
            (u->following || (u->revision && u->revision!=echoy_arrange_revision(b->arrange))))
            return false;
        if (u->command==ECHOY_LINK_OFFER && (!u->following || !u->valid)) return false;
        unarm(b);
        c->offered=u->transaction;
        c->last_transaction=u->transaction;
        c->failed=0;
        c->bound_revision=u->revision;
        if (u->command==ECHOY_LINK_BEGIN && c->candidate_failed) {
            /* A fresh recovery transaction retries this failed immutable job;
             * normal ready/armed revisions keep their existing preparation. */
            b->issued=UINT64_MAX;
            c->candidate_ready=c->candidate_failed=false;
        }
        if (u->command==ECHOY_LINK_OFFER) {
            c->incoming=true;
            if (!b->deferred[0]) discard_candidate(b,false);
            if (!echoy_band_set_upstream(b,true,&u->plan,
                                         u->clock_started && (!u->clock_sync || acknowledged),u->clock_origin_us,
                                         u->looping,now)) return false;
            if (!b->deferred[0]) b->issued=UINT64_MAX;
        }
        return true;
    }
    if (u->command==ECHOY_LINK_ABORT && u->transaction!=c->offered) {
        /* META may arrive without a complete OFFER. Acknowledge that aborted
         * transaction without cancelling an older authorized commit or the
         * independent recording/preparation of a deferred receiver. */
        c->aborted=c->last_transaction=u->transaction;
        if (u->failed) c->failed=u->transaction;
        /* BEGIN can be overwritten in the latest slot by its failed ABORT.
         * That leader candidate was already held for the joint transaction. */
        if (failed_held_source) discard_candidate(b,true);
        return true;
    }
    if (u->transaction!=c->offered) return false;
    if (u->command==ECHOY_LINK_ABORT) {
        if (c->committed==c->offered) return true;
        c->aborted=u->transaction;
        c->failed=u->failed ? c->offered : 0;
        if (b->deferred[0]) {
            /* A recording is outside the joint transaction until its reply
             * ends. Cancelling the network offer must not cancel that job. */
            c->incoming=false;
            c->offered=0;
            c->upstream_valid=false;
        } else if (u->failed || c->follower) {
            discard_candidate(b,u->failed);
            if (c->follower) {
                b->empty_plan=(echoy_plan_t){.role=ECHOY_PULSE,.phrase=1,.density=.5,.duration=2.5};
                const echoy_plan_t *old=c->accepted_upstream_valid ? &c->accepted_upstream : &b->empty_plan;
                echoy_arrange_import(b->arrange,ECHOY_TILE_B,old);
                c->upstream=*old; c->upstream_valid=true;
                b->issued=echoy_arrange_revision(b->arrange);
                c->candidate_exists=false;
            }
            c->incoming=false;
        } else {
            unarm(b);
            c->offered=0;
        }
        replay_actions(b,now);
        return true;
    }
    uint64_t revision=c->follower ? echoy_arrange_revision(b->arrange) : c->bound_revision;
    if (u->revision!=revision || c->failed==c->offered || b->deferred[0]) return false;
    if (u->command==ECHOY_LINK_ARM) {
        if (c->armed) return c->armed==u->transaction && c->armed_at==u->commit_at_us &&
                            c->armed_revision==u->revision;
        if (u->commit_at_us<=now) return false;
        bool no_change=!c->follower && !revision && !c->candidate_exists &&
                       echoy_arrange_revision(b->arrange)==c->settled_revision;
        if (!no_change && (!b->ready || !c->candidate_ready ||
            b->ready->arrangement.revision!=revision ||
            !echoy_arrange_stage_at(b->arrange,&b->ready->arrangement,u->commit_at_us))) return false;
        if (!no_change) b->ready->staged=true;
        c->armed=u->transaction; c->armed_revision=revision; c->armed_at=u->commit_at_us;
        c->align_boundary=u->align_boundary;
        return true;
    }
    if (u->command==ECHOY_LINK_COMMIT) {
        if (c->authorized==u->transaction) return true;
        if (c->armed!=u->transaction || c->armed_at!=u->commit_at_us || now>=c->armed_at) return false;
        c->authorized=u->transaction;
        return true;
    }
    return false;
}
bool echoy_band_action(echoy_band_t *b, echoy_tile_id_t id, echoy_action_t action, double value,
                       int64_t now)
{
    if (!b || !valid_tile(id) || now < 0 || !isfinite(value)) {
        return false;
    }
    if (action == ECHOY_ACT_REDUCED_MOTION) {
        if (value != 0 && value != 1) {
            return false;
        }
        b->reduced_motion = value != 0;
        return true;
    }
    struct voice *v = &b->voices[id];
    echoy_snapshot_t s;
    echoy_model_get_snapshot(v->model, &s);
    const echoy_arrange_tile_t *t = echoy_arrange_get_tile(b->arrange, id);
    if (action != ECHOY_ACT_PRESENT && action != ECHOY_ACT_CONDITION &&
        (!t->present || v->condition != ECHOY_NORMAL)) {
        return false;
    }
    struct cooperation *cooperation=b->cooperation;
    if (id==ECHOY_TILE_A && cooperation->armed && cooperation->committed!=cooperation->armed &&
        (action==ECHOY_ACT_ROLE || action==ECHOY_ACT_PHRASE || action==ECHOY_ACT_DENSITY)) {
        if ((action==ECHOY_ACT_ROLE && (value<0 || value>ECHOY_HARMONY || value!=floor(value))) ||
            (action==ECHOY_ACT_PHRASE && (value<1 || value>3 || value!=floor(value)))) return false;
        unsigned n=cooperation->queued_count;
        if (n && action==ECHOY_ACT_DENSITY && cooperation->queued[n-1].action==action) --n;
        if (n>=sizeof(cooperation->queued)/sizeof(*cooperation->queued)) return false;
        cooperation->queued[n]=(struct queued_action){action,value};
        cooperation->queued_count=n+1;
        return true;
    }
    echoy_arrange_patch_t patch = {0};
    bool ok = false;
    switch (action) {
    case ECHOY_ACT_CANCEL_RECORD:
        ok = echoy_model_cancel_record(v->model);
        if (ok) {
            v->automatic_preview = false; v->recorded_reply_pending=false;
            stop_voice(b, id, now);
            echoy_arrange_mute(b->arrange, id);
            if (b->deferred[id]) echoy_band_tick(b, now);
        }
        return ok;
    case ECHOY_ACT_RECORD:
        ok = echoy_model_record(v->model, now);
        if (ok) {
            v->automatic_preview=false;v->recorded_reply_pending=false;
            stop_voice(b, id, now);
            echoy_arrange_mute(b->arrange, id);
            echoy_model_get_snapshot(v->model, &s);
            if (s.phase == ECHOY_COUNT_IN) {
                for (int i = 0; i < ECHOY_COUNT_IN_STEPS; i++) {
                    if (!cue(b, id, ECHOY_AUDIO_GUIDE,
                             s.capture_at_us - (ECHOY_COUNT_IN_STEPS - i) * ECHOY_COUNT_IN_STEP_US,
                             true)) {
                        ok = false;
                    }
                }
                for (int i = 0; i < 4; i++) {
                    if (!cue(b, id, i == 0 ? ECHOY_AUDIO_CAPTURE_START : ECHOY_AUDIO_GUIDE,
                             s.capture_at_us + i * INT64_C(625000), true)) {
                        ok = false;
                    }
                }
            }
        }
        return ok;
    case ECHOY_ACT_TAP:
        if(!capture(s.phase) && value!=0){
            if(value<1 || value>4 || value!=floor(value))return false;
            /* An accepted UI input may outlive its visible role in the queue. */
            if(value!=s.role+1)return true;
        }
        if (!echoy_model_tap(v->model, now)) {
            return false;
        }
        if(!capture(s.phase))return schedule_preview(b,id,s.role,now,
                                                (now>b->render_until?now:b->render_until)+3000);
        return cue(b,id,ECHOY_AUDIO_TAP,(now>b->render_until?now:b->render_until)+3000,false);
    case ECHOY_ACT_PAUSE:
        v->automatic_preview = false; v->recorded_reply_pending=false;
        echoy_model_pause(v->model);
        echoy_arrange_mute(b->arrange, id);
        stop_voice(b, id, now);
        if (b->deferred[id]) echoy_band_tick(b, now);
        return true;
    case ECHOY_ACT_TRANSPORT:
        v->automatic_preview = false; v->recorded_reply_pending=false;
        if (s.phase==ECHOY_IDLE && s.plan_pending && retained_start(b,id,now))
            echoy_model_set_external_pending(v->model,false,0);
        ok = echoy_model_transport(v->model, now);
        if (s.plan_pending) echoy_model_set_external_pending(v->model,true,s.apply_at_us);
        if (ok) {
            echoy_model_get_snapshot(v->model, &s);
            if (s.phase == ECHOY_IDLE) {
                echoy_arrange_mute(b->arrange, id);
                stop_voice(b, id, now);
            }
        }
        return ok;
    case ECHOY_ACT_AUDITION:
        v->automatic_preview = false; v->recorded_reply_pending=false;
        ok = echoy_model_audition(v->model, now);
        if (ok) {
            echoy_arrange_mute(b->arrange, id);
            stop_voice(b, id, now);
        }
        return ok;
    case ECHOY_ACT_ROLE:
        if (value < 0 || value > 3 || value != floor(value) ||
            !echoy_model_select_role(v->model, (echoy_role_t)value, now)) {
            return false;
        }
        patch.set_role = true;
        patch.role = (echoy_role_t)value;
        break;
    case ECHOY_ACT_PHRASE:
        if (value < 1 || value > 3 || value != floor(value) ||
            !echoy_model_select_phrase(v->model, (unsigned)value, now)) {
            return false;
        }
        patch.set_phrase = true;
        patch.phrase = (unsigned)value;
        break;
    case ECHOY_ACT_DENSITY:
        if (!echoy_model_set_density(v->model, value, now)) {
            return false;
        }
        patch.set_density = true;
        patch.density = value;
        break;
    case ECHOY_ACT_LANGUAGE:
        if (value != 0 && value != 1) {
            return false;
        }
        b->english = value != 0;
        return true;
    case ECHOY_ACT_VOLUME:
        v->volume = fmax(0, fmin(1, value));
        return true;
    case ECHOY_ACT_CONDITION:
        if (value < 0 || value > ECHOY_BATTERY_EMPTY || value != floor(value)) {
            return false;
        }
        v->condition = (echoy_condition_t)value;
        v->battery = v->condition == ECHOY_BATTERY_EMPTY ? 0 : v->battery ? v->battery : 72;
        echoy_model_set_fault(v->model, v->condition != ECHOY_NORMAL);
        echoy_arrange_mute(b->arrange, id);
        stop_voice(b, id, now);
        return true;
    case ECHOY_ACT_PRESENT: {
        if (value != 0 && value != 1) {
            return false;
        }
        if (!echoy_arrange_set_present(b->arrange, id, value != 0)) {
            return false;
        }
        echoy_model_pause(v->model);
        echoy_arrange_mute(b->arrange, id);
        stop_voice(b, id, now);
        if (value) {
            v->condition = ECHOY_NORMAL;
            if (!v->battery) {
                v->battery = 72;
            }
            echoy_model_set_fault(v->model, false);
        }
        size_t n;
        const echoy_arrange_edge_t *e = echoy_arrange_get_edges(b->arrange, true, &n);
        echoy_arrange_edge_t copy[2];
        if (n) {
            memcpy(copy, e, n * sizeof(*e));
        }
        return echoy_band_set_edges(b, copy, n, now);
    }
    case ECHOY_ACT_EXAMPLE:
        if (s.following || capture(s.phase)) {
            return false;
        }
        if (!echoy_model_load_seed(v->model, 5, now, true)) {
            return false;
        }
        stop_voice(b, id, now);
        echoy_arrange_mute(b->arrange, id);
        cancel_auto(b, id, now);
        v->automatic_preview = true;v->recorded_reply_pending=false;
        v->observed_seed = 5;
        return set_seed(b, id, 5);
    default:
        return false;
    }
    if (s.phase == ECHOY_PREVIEW && now < s.start_us) {
        stop_voice(b, id, now);
    }
    cancel_auto(b, -1, now);
    ok=echoy_arrange_configure(b->arrange,id,&patch);
    if (ok && action==ECHOY_ACT_ROLE && value!=s.role)
        v->preview_has_tap=false;
    if (ok && id==ECHOY_TILE_A && !++cooperation->local_edit_serial)
        ++cooperation->local_edit_serial;
    return ok;
}
echoy_band_job_t *echoy_band_take_job(echoy_band_t *b)
{
    if (!b) {
        return NULL;
    }
    uint64_t rev = echoy_arrange_revision(b->arrange);
    if (rev == b->issued) {
        return NULL;
    }
    echoy_band_job_t *j = calloc(1, sizeof(*j));
    if (!j) {
        return NULL;
    }
    if (!echoy_arrange_compile(b->arrange, &j->arrangement)) {
        free(j);
        return NULL;
    }
    b->issued = rev;
    j->generation=b->job_generation;
    /* Physical B carries the upstream phrase and clock, never local audio.
     * Snapshot the identity here; the immutable worker must not read band. */
    j->inaudible_mask=b->upstream_connected ? (1u << ECHOY_TILE_B) : 0;
    struct cooperation *c=b->cooperation;
    c->candidate=j->arrangement.plans[ECHOY_TILE_A].music;
    c->candidate_revision=rev;
    c->candidate_exists=true;
    c->candidate_ready=c->candidate_failed=false;
    c->candidate_affects=(j->arrangement.changed_mask & 1)!=0;
    echoy_band_discard_job(b->ready);
    b->ready = NULL;
    for (int i = 0; i < N; i++) {
        b->voices[i].failed = false;
        if (j->arrangement.changed_mask & (1u << i)) {
            echoy_model_set_external_pending(b->voices[i].model, true, 0);
        }
    }
    return j;
}
bool echoy_band_prepare_job(echoy_band_job_t *j)
{
    if (!j || j->ok || j->bass_cut_attempted || j->speaker_voicing_attempted || j->handed_off) {
        return false;
    }
    for (int i = 0; i < N; i++) {
        if ((j->arrangement.changed_mask & (1u << i)) &&
            !(j->inaudible_mask & (1u << i)) && j->arrangement.plans[i].valid) {
            const echoy_arrange_plan_t *p = &j->arrangement.plans[i];
            struct bank *k = calloc(1, sizeof(*k));
            if (!k) {
                return false;
            }
            k->refs = 1;
            j->banks[i] = k;
            if (echoy_audio_prepare(&p->music,
                                    p->source == ECHOY_TILE_NONE && !p->detached &&
                                        p->music.role == ECHOY_PULSE && p->music.phrase == 1,
                                    &k->loops) != ECHOY_AUDIO_OK) {
                return false;
            }
        }
    }
    j->ok = true;
    return true;
}
bool echoy_band_cut_harmony_bass_job(echoy_band_job_t *j)
{
    if (!j || !j->ok || j->staged || j->calibrated || j->bass_cut_attempted ||
        j->speaker_voicing_attempted || j->handed_off) return false;
    j->bass_cut_attempted = true;
    for (int i = 0; i < N; ++i) {
        if (!j->banks[i] || j->arrangement.plans[i].music.role != ECHOY_HARMONY) continue;
        echoy_audio_loops_t *loops = j->banks[i]->loops;
        if (!echoy_audio_cut_harmony_bass(loops->samples[ECHOY_AUDIO_FULL], loops->frames)) {
            /* Earlier banks may already be processed. None can be published. */
            j->ok = false;
            return false;
        }
    }
    return true;
}
bool echoy_band_voice_for_speaker_job(echoy_band_job_t *j)
{
    if (!j || !j->ok || j->staged || j->calibrated || j->handed_off ||
        j->bass_cut_attempted || j->speaker_voicing_attempted) return false;
    j->speaker_voicing_attempted = true;
    /* Check every bank before modifying any. The worker cannot alter a bank
     * retained by a prepared/live voice or another consumer. */
    for (int i = 0; i < N; ++i) {
        if (j->banks[i] && (j->banks[i]->refs != 1 || !j->banks[i]->loops)) {
            j->ok = false;
            return false;
        }
    }
    for (int i = 0; i < N; ++i) {
        if (j->banks[i] && !echoy_audio_voice_for_speaker(
                &j->arrangement.plans[i].music, j->banks[i]->loops)) {
            /* A preceding bank may have succeeded; reject the whole job. */
            j->ok = false;
            return false;
        }
    }
    return true;
}
bool echoy_band_calibrate_job(echoy_band_job_t *j, const float role_gains[4])
{
    if (!j || !j->ok || j->staged || j->calibrated || j->handed_off || !role_gains) return false;
    for (unsigned role=0; role<4; ++role)
        if (!isfinite(role_gains[role]) || role_gains[role] < 0 || role_gains[role] > 4)
            return false;
    for (int i=0; i<N; ++i) {
        if (!j->banks[i]) continue;
        echoy_audio_loops_t *loops=j->banks[i]->loops;
        float gain=role_gains[j->arrangement.plans[i].music.role];
#ifdef ESP_PLATFORM
        float peak_before = 0, peak_after = 0;
#endif
        for (size_t at=0; at<loops->frames; ++at) {
#ifdef ESP_PLATFORM
            peak_before = fmaxf(peak_before, fabsf(loops->samples[ECHOY_AUDIO_FULL][at]));
#endif
            loops->samples[ECHOY_AUDIO_FULL][at] *= gain;
#ifdef ESP_PLATFORM
            peak_after = fmaxf(peak_after, fabsf(loops->samples[ECHOY_AUDIO_FULL][at]));
#endif
        }
#ifdef ESP_PLATFORM
        ESP_LOGI("echoy_balance", "role=%u gain_milli=%u before_peak_micro=%u after_peak_micro=%u",
                 (unsigned)j->arrangement.plans[i].music.role, (unsigned)(gain * 1000),
                 (unsigned)(peak_before * 1000000), (unsigned)(peak_after * 1000000));
#endif
    }
    j->calibrated=true;
    return true;
}
bool echoy_band_finish_job(echoy_band_t *b, echoy_band_job_t *j, int64_t now)
{
    if (!b || !j || now < 0) {
        echoy_band_discard_job(j);
        return false;
    }
    if (j->arrangement.revision != echoy_arrange_revision(b->arrange) ||
        j->generation != b->job_generation) {
        echoy_band_discard_job(j);
        return false;
    }
    if (!j->ok) {
        b->cooperation->candidate_failed=true;
        for (int i = 0; i < N; i++) {
            if (j->arrangement.changed_mask & (1u << i)) {
                b->voices[i].failed = true;
                echoy_model_set_external_pending(b->voices[i].model, false, 0);
                echoy_snapshot_t s;
                echoy_model_get_snapshot(b->voices[i].model, &s);
                if (s.phase == ECHOY_PREPARING) {
                    echoy_model_pause(b->voices[i].model);
                }
            }
        }
        echoy_band_discard_job(j);
        return false;
    }
    echoy_band_discard_job(b->ready);
    j->handed_off = true;
    b->ready = j;
    b->cooperation->candidate_ready=true;
    echoy_band_tick(b, now);
    return true;
}
static void align_group_boundary(echoy_band_t *b, int64_t at, int64_t now,
                                  unsigned replaced)
{
    struct cooperation *c=b->cooperation;
    if (!c->align_boundary) return;
    echoy_tile_id_t clock=echoy_arrange_get_tile(b->arrange,ECHOY_TILE_A)->clock_id;
    const echoy_arrange_clock_t *old=echoy_arrange_get_clock(b->arrange,clock);
    bool changed=(at-old->origin_us)%BAR!=0;
    if (!echoy_arrange_clock_sync(b->arrange,clock,at,true,now)) return;
    for (int i=0;i<N;++i) {
        if (echoy_arrange_get_tile(b->arrange,i)->clock_id!=clock) continue;
        struct voice *v=&b->voices[i];
        echoy_snapshot_t s;
        echoy_model_get_snapshot(v->model,&s);
        /* A paused owner stays paused; recording/one-shot replies keep their
         * own timeline. Only continuous playback adopts the common phase. */
        if (changed && !(replaced & (1u<<i)) && s.continuous &&
            s.phase==ECHOY_PLAYING && v->prepared &&
            !play_voice(b,i,v->prepared,at,true,true)) v->failed=true;
        echoy_model_sync_clock(v->model,at,true);
    }
}

static void start_local_record_reply(echoy_band_t *b,int64_t now)
{
    struct cooperation *c=b->cooperation;struct voice *v=&b->voices[ECHOY_TILE_A];
    if(!joint_hold(b) || !c->align_boundary || c->follower || b->upstream_connected || c->peer_looping || c->seed_queued ||
       !b->ready || !c->candidate_ready || c->candidate_failed || v->failed ||
       !v->automatic_preview || !v->recorded_reply_pending || v->condition!=ECHOY_NORMAL) return;
    const echoy_arrange_plan_t *plan=&b->ready->arrangement.plans[ECHOY_TILE_A];
    if(!plan->valid || plan->source!=ECHOY_TILE_NONE || !plan->music.count) return;
    struct bank *bank=(b->ready->arrangement.changed_mask&1u) ? b->ready->banks[ECHOY_TILE_A] : v->prepared;
    if(!bank)return;
    echoy_snapshot_t s;echoy_model_get_snapshot(v->model,&s);
    int64_t at=now+50000;
    if(!echoy_model_start_local_preview(v->model,&plan->music,s.generation,at))return;
    if(!play_voice(b,ECHOY_TILE_A,bank,at,false,false)) {
        echoy_model_pause(v->model);v->failed=true;
    }
    v->automatic_preview=false;v->recorded_reply_pending=false;
}
void echoy_band_tick(echoy_band_t *b, int64_t now)
{
    if (!b || now < 0) {
        return;
    }
    for (int i = 0; i < N; i++) {
        struct voice *v = &b->voices[i];
        echoy_snapshot_t before;
        echoy_model_get_snapshot(v->model, &before);
        echoy_model_tick(v->model, now);
        echoy_snapshot_t s;
        echoy_model_get_snapshot(v->model, &s);
        reap(v, now);
        if (s.seed != v->observed_seed || (capture(before.phase) && s.phase == ECHOY_PREPARING)) {
            cancel_auto(b, i, now);
            v->automatic_preview = true;
            v->recorded_reply_pending=capture(before.phase) && s.phase==ECHOY_PREPARING;
            v->observed_seed = s.seed;
            if (s.seed && !set_seed(b, i, s.seed)) {
                v->failed = true;
            }
        }
    }
    if (b->upstream_requested && !b->upstream_connected && !join_busy(b,ECHOY_TILE_A)) {
        struct cooperation *c=b->cooperation;
        echoy_band_set_upstream(b,true,c->upstream_valid ? &c->upstream : NULL,
                                c->upstream_started,c->upstream_origin,c->upstream_playing,now);
    }
    apply_requested_edges(b,now);
    struct cooperation *c=b->cooperation;
    if (c->armed && c->committed!=c->armed && c->authorized!=c->armed && now>=c->armed_at) {
        c->failed=c->armed;
        c->recovering=true;
        discard_candidate(b,true);
        replay_actions(b,now);
    }
    if (b->ready && b->ready->arrangement.revision != echoy_arrange_revision(b->arrange)) {
        echoy_band_discard_job(b->ready);
        b->ready = NULL;
    }
    start_local_record_reply(b,now);
    if (b->ready && !b->ready->staged && !joint_hold(b)) {
        bool preview = false;
        for (int i = 0; i < N; i++) {
            echoy_snapshot_t s;
            echoy_model_get_snapshot(b->voices[i].model, &s);
            if ((b->ready->arrangement.changed_mask & (1u << i)) && s.phase == ECHOY_PREVIEW) {
                preview = true;
            }
        }
        if (!preview && echoy_arrange_stage(b->arrange, &b->ready->arrangement, now)) {
            b->ready->staged = true;
        }
    }
    echoy_arrange_commit_t commit;
    bool authorized=!joint_hold(b) || (c->armed && c->authorized==c->armed && c->committed!=c->armed);
    if (b->ready && b->ready->staged && authorized && echoy_arrange_commit(b->arrange, now, &commit)) {
        for (int i = 0; i < N; i++) {
            /* Identity can become inaudible without changing its music.
             * Drop any previous prepared bank while retaining the full plan. */
            if ((commit.changed_mask | b->ready->inaudible_mask) & (1u << i)) {
                struct voice *v = &b->voices[i];
                echoy_snapshot_t s;
                echoy_model_get_snapshot(v->model, &s);
                release(v->prepared);
                v->prepared = retain(b->ready->banks[i]);
                if (s.phase == ECHOY_PLAYING && v->prepared &&
                    !play_voice(b, i, v->prepared, commit.at_us, true, true)) {
                    v->failed = true;
                }
            }
        }
        if (c->armed && c->authorized==c->armed && c->committed!=c->armed)
            align_group_boundary(b,commit.at_us,now,commit.changed_mask | b->ready->inaudible_mask);
        sync_plans(b);
        echoy_band_discard_job(b->ready);
        b->ready = NULL;
        c->settled_revision=commit.revision;
        c->candidate_exists=c->candidate_ready=c->candidate_failed=false;
        if (c->armed && c->authorized==c->armed && c->committed!=c->armed) {
            c->committed=c->armed;
            c->incoming=false;
            if (c->follower && c->upstream_valid) {
                c->accepted_upstream=c->upstream;
                c->accepted_upstream_valid=true;
            }
            replay_actions(b,now);
        }
    }
    if (!b->ready && c->armed && !c->armed_revision && c->authorized==c->armed && now>=c->armed_at &&
        c->committed!=c->armed) {
        align_group_boundary(b,c->armed_at,now,0);
        c->committed=c->armed;
        replay_actions(b,now);
    }
    const echoy_arrange_pending_t *pending = echoy_arrange_get_pending(b->arrange);
    for (int i = 0; i < N; i++) {
        struct voice *v = &b->voices[i];
        echoy_snapshot_t s;
        echoy_model_get_snapshot(v->model, &s);
        if (pending && (pending->job.changed_mask & (1u << i))) {
            echoy_model_set_external_pending(v->model, true, pending->at_us);
        }
        bool retained=s.continuous && s.plan_pending && retained_start(b,i,now);
        if (s.phase != ECHOY_PREPARING || (s.plan_pending && !retained) || !v->prepared ||
            v->condition != ECHOY_NORMAL) {
            continue;
        }
        const echoy_arrange_tile_t *t = echoy_arrange_get_tile(b->arrange, i);
        int64_t at = now + 50000;
        if (s.continuous) {
            bool running=c->active && !b->deferred[i] && c->peer_looping;
            for (int j=0;j<N;++j) {
                const echoy_arrange_tile_t *q=echoy_arrange_get_tile(b->arrange,j);
                if (q->loop && q->clock_id==t->clock_id) running=true;
            }
            if (!echoy_arrange_enable_loop_shared(b->arrange, i, now, running, &at)) {
                continue;
            }
            if (i==ECHOY_TILE_A && !running && !++c->local_start_serial) ++c->local_start_serial;
        } else {
            bool running = false;
            for (int j = 0; j < N; j++) {
                const echoy_arrange_tile_t *q = echoy_arrange_get_tile(b->arrange, j);
                if (q->loop && q->clock_id == t->clock_id) {
                    running = true;
                }
            }
            if (running) {
                const echoy_arrange_clock_t *c = echoy_arrange_get_clock(b->arrange, t->clock_id);
                int64_t delta = now + 60000 - c->origin_us;
                at = c->origin_us + ((delta + BAR - 1) / BAR) * BAR;
            }
        }
        const echoy_arrange_clock_t *c = echoy_arrange_get_clock(b->arrange, t->clock_id);
        echoy_model_apply_external(
            v->model, &t->current.music, (uint32_t)echoy_arrange_revision(b->arrange),
            t->current.source != ECHOY_TILE_NONE || wanted_parent(b, i), c->origin_us, c->started);
        if (play_voice(b, i, v->prepared, at, s.continuous, false)) {
            echoy_model_start_external(v->model, s.generation, at);
            if (retained) echoy_model_set_external_pending(v->model,true,s.apply_at_us);
            v->automatic_preview = false; v->recorded_reply_pending=false;
        } else {
            v->failed = true;
        }
    }
}
bool echoy_band_read(echoy_band_t *b, echoy_tile_id_t id, echoy_control_view_t *out)
{
    if (!b || !out || !valid_tile(id)) {
        return false;
    }
    struct voice *v = &b->voices[id];
    const echoy_arrange_tile_t *t = echoy_arrange_get_tile(b->arrange, id);
    memset(out, 0, sizeof(*out));
    echoy_model_get_snapshot(v->model, &out->state);
    out->state.current_playable=retained_start(b,id,b->render_until);
    out->plan = *echoy_model_get_plan(v->model);
    memcpy(out->memories, t->memories, sizeof(out->memories));
    out->state.role = t->role;
    out->state.phrase = t->phrase;
    out->state.density = t->density;
    out->state.revision = (uint32_t)echoy_arrange_revision(b->arrange);
    out->english = b->english;
    out->reduced_motion = b->reduced_motion;
    out->present = t->present;
    out->volume = v->volume;
    out->battery = v->battery;
    out->tile = id;
    out->source = t->current.valid ? t->current.source : ECHOY_TILE_NONE;
    out->clock = t->clock_id;
    out->condition = v->condition;
    out->preparing = out->state.plan_pending && out->state.apply_at_us == 0;
    if(echoy_model_local_preview(v->model)) {
        /* Existing reply UI describes the sound already being heard. Group
         * readiness remains visible through link_read, never this UI copy. */
        out->state.plan_pending=false;out->state.apply_at_us=0;out->preparing=false;
    }
    out->preparation_failed = v->failed;
    bool incoming=false,outgoing=false;
    for (size_t j=0;j<b->requested_count;++j) {
        echoy_arrange_edge_t e=b->requested[j];
        if (!echoy_arrange_get_tile(b->arrange,e.left)->present ||
            !echoy_arrange_get_tile(b->arrange,e.right)->present) continue;
        incoming |= e.right==id;
        outgoing |= e.left==id;
    }
    if (id==ECHOY_TILE_A && b->upstream_requested) incoming=true;
    if (b->deferred[id]) out->cooperation=ECHOY_COOP_DEFERRED;
    else if (id==ECHOY_TILE_A && b->cooperation->recovering) out->cooperation=ECHOY_COOP_RECOVERING;
    else if (incoming) out->cooperation=t->current.valid && t->current.source!=ECHOY_TILE_NONE
                                        ? ECHOY_COOP_FOLLOWING : ECHOY_COOP_WAITING;
    else if (outgoing || (id==ECHOY_TILE_A && b->cooperation->active))
        out->cooperation=ECHOY_COOP_CONNECTED;
    return true;
}
struct active_mix {
    uint8_t play[N][RECORDS], cues[N][CUES];
    uint8_t play_count[N], cue_count[N];
};
static void active_mix_refresh(const echoy_band_t *b, struct active_mix *active)
{
    memset(active->play_count, 0, sizeof(active->play_count));
    memset(active->cue_count, 0, sizeof(active->cue_count));
    for (int i = 0; i < N; ++i)
        for (unsigned j = 0; j < RECORDS; ++j)
            if (b->voices[i].play[j].bank)
                active->play[i][active->play_count[i]++] = (uint8_t)j;
    for (unsigned j = 0; j < CUES; ++j) {
        const struct cue *c = &b->scheduled[j];
        if (c->used && valid_tile(c->tile))
            active->cues[c->tile][active->cue_count[c->tile]++] = (uint8_t)j;
    }
}
void echoy_band_render(echoy_band_t *b, int64_t at, float *out, size_t frames)
{
    if (!b || !out || at < 0) {
        return;
    }
    memset(out, 0, frames * sizeof(*out));
#ifdef ESP_PLATFORM
    bool trace = !b->profiled;
    uint32_t mark = esp_cpu_get_cycle_count();
#define PROFILE_PART(field) do { if (trace) { uint32_t next=esp_cpu_get_cycle_count(); \
    b->profile.field += next-mark; mark=next; } } while (0)
#else
#define PROFILE_PART(field) ((void)0)
#endif
    const double smooth = exp(-1.0 / (.015 * ECHOY_AUDIO_SAMPLE_RATE));
    /* The owner serializes actions around blocks. Preserve slot order exactly,
     * but do not scan 96 empty cues for every voice and every sample. A commit
     * inside this block can replace playback banks, so refresh after ticking. */
    struct active_mix active;
    active_mix_refresh(b, &active);
    const echoy_arrange_pending_t *pending = echoy_arrange_get_pending(b->arrange);
    const struct cooperation *group=b->cooperation;
    int64_t group_at=group->align_boundary && group->armed &&
        group->authorized==group->armed && group->committed!=group->armed ? group->armed_at : 0;
    for (size_t k = 0; k < frames; k++) {
        int64_t t = at + (int64_t)(k * 1000000 / ECHOY_AUDIO_SAMPLE_RATE);
        double sum = 0;
        if ((pending && t >= pending->at_us) || (group_at && t>=group_at)) {
            if (!b->profiled) ++b->profile.tick_calls;
            echoy_band_tick(b, t);
            active_mix_refresh(b, &active);
            pending = echoy_arrange_get_pending(b->arrange);
            group_at=0;
        }
        PROFILE_PART(clock_cycles);
        for (int i = 0; i < N; i++) {
            struct voice *v = &b->voices[i];
            double sample = 0;
            if (v->smoothed_volume != v->volume)
                v->smoothed_volume = v->volume + (v->smoothed_volume - v->volume) * smooth;
            if (!active.play_count[i] && !active.cue_count[i]) {
                PROFILE_PART(voice_cycles);
                continue;
            }
            for (unsigned j = 0; j < active.play_count[i]; j++) {
                struct playback *p = &v->play[active.play[i][j]];
                if (!p->bank || t < p->at || (p->stop && t >= p->stop + FADE)) {
                    continue;
                }
                uint64_t pos = ((uint64_t)(t - p->at) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
                if (!p->loop && pos >= p->bank->loops->frames) {
                    continue;
                }
                pos %= p->bank->loops->frames;
                double gain = p->fade && t - p->at < FADE ? (t - p->at) / (double)FADE : 1;
                if (p->stop && t >= p->stop) {
                    gain *= 1 - (t - p->stop) / (double)FADE;
                }
                sample += p->bank->loops->samples[ECHOY_AUDIO_FULL][pos] * gain;
            }
            PROFILE_PART(voice_cycles);
            for (unsigned j = 0; j < active.cue_count[i]; j++) {
                struct cue *c = &b->scheduled[active.cues[i][j]];
                if (!c->used || c->tile != i || t < c->at) {
                    continue;
                }
                uint64_t pos = ((uint64_t)(t - c->at) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
                size_t frames=c->pad_preview ? c->frames : c->buffer->frames;
                if (pos >= frames) {
                    finish_cue(c);
                    continue;
                }
                if(c->pad_preview){
                    double gain=pos<48 ? pos/48.0 : pos+PREVIEW_FADE_FRAMES>=frames ?
                                (frames-1-pos)/(double)PREVIEW_FADE_FRAMES : 1;
                    if(c->stop_at && t>=c->stop_at){
                        uint64_t stop=((uint64_t)(t-c->stop_at)*ECHOY_AUDIO_SAMPLE_RATE+500000)/1000000;
                        if(stop>=PREVIEW_FADE_FRAMES){finish_cue(c);continue;}
                        gain*=1-stop/(double)PREVIEW_FADE_FRAMES;
                    }
                    size_t source=c->offset+(size_t)pos;
                    if(source>=c->buffer->frames)source-=c->buffer->frames;
                    sample+=c->buffer->samples[source]*gain;
                }else sample += c->buffer->samples[pos];
            }
            PROFILE_PART(cue_cycles);
            sum += sample * v->smoothed_volume;
            PROFILE_PART(voice_cycles);
        }
        out[k] = (float)(sum * .45);
        PROFILE_PART(voice_cycles);
    }
    b->render_until = at + (int64_t)(frames * 1000000 / ECHOY_AUDIO_SAMPLE_RATE);
    if (frames) b->profiled = true;
#undef PROFILE_PART
}
echoy_band_profile_t echoy_band_render_profile(const echoy_band_t *b)
{
    return b ? b->profile : (echoy_band_profile_t){0};
}
