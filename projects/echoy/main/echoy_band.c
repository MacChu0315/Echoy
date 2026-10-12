#include "echoy_band.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
#define N ECHOY_ARRANGE_TILE_COUNT
#define BAR INT64_C(2500000)
#define FADE INT64_C(8000)
#define RECORDS 8
#define CUES 96
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
    echoy_audio_cue_t kind;
    int64_t at;
    bool used, guide;
};
struct voice {
    echoy_model_handle_t model;
    struct bank *prepared;
    struct playback play[RECORDS];
    uint16_t observed_seed;
    double volume, smoothed_volume;
    unsigned battery;
    echoy_condition_t condition;
    bool failed, automatic_preview;
};
struct echoy_band_job {
    echoy_arrange_job_t arrangement;
    struct bank *banks[N];
    bool ok, staged, calibrated;
};
struct echoy_band {
    echoy_arrange_t *arrange;
    struct voice voices[N];
    echoy_audio_buffer_t *cues[3];
    struct cue scheduled[CUES];
    echoy_band_job_t *ready;
    uint64_t issued;
    bool english, reduced_motion;
    bool upstream_connected;
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
static void stop_voice(echoy_band_t *b, int id, int64_t now)
{
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
            b->scheduled[i].used = false;
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
static bool cue(echoy_band_t *b, int id, echoy_audio_cue_t kind, int64_t at, bool guide)
{
    for (unsigned i = 0; i < CUES; i++) {
        if (!b->scheduled[i].used) {
            b->scheduled[i] =
                (struct cue){.tile = id, .kind = kind, .at = at, .used = true, .guide = guide};
            return true;
        }
    }
    return false;
}
static void cancel_auto(echoy_band_t *b, int except, int64_t now)
{
    for (int i = 0; i < N; i++) {
        if (i != except && b->voices[i].automatic_preview) {
            echoy_snapshot_t s;
            echoy_model_get_snapshot(b->voices[i].model, &s);
            if (s.phase == ECHOY_PREPARING) {
                echoy_model_pause(b->voices[i].model);
                echoy_arrange_mute(b->arrange, i);
                stop_voice(b, i, now);
            }
            b->voices[i].automatic_preview = false;
        }
    }
}
static bool set_seed(echoy_band_t *b, int id, uint16_t bits)
{
    echoy_seed_note_t notes[16];
    size_t n = 0;
    for (unsigned i = 0; i < 16; i++) {
        if (bits & (1u << i)) {
            notes[n++] = (echoy_seed_note_t){.step = i, .velocity = .74, .kind = ECHOY_KICK};
        }
    }
    return echoy_arrange_set_seed(b->arrange, id, notes, n);
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
            (t->current.valid && t->current.source != ECHOY_TILE_NONE) || wanted_parent(b, i),
            c->origin_us, c->started);
    }
}
echoy_band_t *echoy_band_create(void)
{
    echoy_band_t *b = calloc(1, sizeof(*b));
    if (!b) {
        return NULL;
    }
    b->issued = UINT64_MAX;
    b->arrange = echoy_arrange_create();
    if (!b->arrange) {
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
        echoy_model_select_role(b->voices[i].model, echoy_arrange_get_tile(b->arrange, i)->role, 0);
    }
    for (unsigned i = 0; i < 3; i++) {
        if (echoy_audio_render_cue((echoy_audio_cue_t)i, &b->cues[i]) != ECHOY_AUDIO_OK) {
            echoy_band_delete(b);
            return NULL;
        }
    }
    sync_plans(b);
    return b;
}
void echoy_band_delete(echoy_band_t *b)
{
    if (!b) {
        return;
    }
    echoy_band_discard_job(b->ready);
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
    echoy_arrange_delete(b->arrange);
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

bool echoy_band_set_upstream(echoy_band_t *b, bool connected, const echoy_plan_t *plan,
                             bool started, int64_t origin, bool playing, int64_t now)
{
    if (!b || now < 0 || (started && origin < 0)) return false;
    if (!connected) {
        if (!b->upstream_connected) return true;
        if (!echoy_band_set_edges(b, NULL, 0, now) ||
            !echoy_arrange_set_present(b->arrange, ECHOY_TILE_B, false)) return false;
        b->upstream_connected = false;
        return true;
    }
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
    if (!b || now < 0 || !echoy_arrange_set_edges(b->arrange, edges, count)) {
        return false;
    }
    cancel_auto(b, -1, now);
    for (int i = 0; i < N; i++) {
        echoy_snapshot_t s;
        echoy_model_get_snapshot(b->voices[i].model, &s);
        const echoy_arrange_tile_t *t = echoy_arrange_get_tile(b->arrange, i);
        bool following =
            wanted_parent(b, i) || (t->current.valid && t->current.source != ECHOY_TILE_NONE);
        if (following && capture(s.phase)) {
            stop_voice(b, i, now);
        }
        echoy_model_set_following(b->voices[i].model, following);
    }
    return true;
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
    echoy_arrange_patch_t patch = {0};
    bool ok = false;
    switch (action) {
    case ECHOY_ACT_CANCEL_RECORD:
        ok = echoy_model_cancel_record(v->model);
        if (ok) {
            v->automatic_preview = false;
            stop_voice(b, id, now);
            echoy_arrange_mute(b->arrange, id);
        }
        return ok;
    case ECHOY_ACT_RECORD:
        ok = echoy_model_record(v->model, now);
        if (ok) {
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
        if (!echoy_model_tap(v->model, now)) {
            return false;
        }
        return cue(b, id, ECHOY_AUDIO_TAP, (now > b->render_until ? now : b->render_until) + 3000,
                   false);
    case ECHOY_ACT_PAUSE:
        v->automatic_preview = false;
        echoy_model_pause(v->model);
        echoy_arrange_mute(b->arrange, id);
        stop_voice(b, id, now);
        return true;
    case ECHOY_ACT_TRANSPORT:
        v->automatic_preview = false;
        ok = echoy_model_transport(v->model, now);
        if (ok) {
            echoy_model_get_snapshot(v->model, &s);
            if (s.phase == ECHOY_IDLE) {
                echoy_arrange_mute(b->arrange, id);
                stop_voice(b, id, now);
            }
        }
        return ok;
    case ECHOY_ACT_AUDITION:
        v->automatic_preview = false;
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
        v->automatic_preview = true;
        v->observed_seed = 5;
        return set_seed(b, id, 5);
    default:
        return false;
    }
    if (s.phase == ECHOY_PREVIEW && now < s.start_us) {
        stop_voice(b, id, now);
    }
    cancel_auto(b, -1, now);
    return echoy_arrange_configure(b->arrange, id, &patch);
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
    if (!j || j->ok) {
        return false;
    }
    for (int i = 0; i < N; i++) {
        if ((j->arrangement.changed_mask & (1u << i)) && j->arrangement.plans[i].valid) {
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
bool echoy_band_calibrate_job(echoy_band_job_t *j, const float role_gains[4])
{
    if (!j || !j->ok || j->staged || j->calibrated || !role_gains) return false;
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
    if (j->arrangement.revision != echoy_arrange_revision(b->arrange)) {
        echoy_band_discard_job(j);
        return false;
    }
    if (!j->ok) {
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
    b->ready = j;
    echoy_band_tick(b, now);
    return true;
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
            v->observed_seed = s.seed;
            if (s.seed && !set_seed(b, i, s.seed)) {
                v->failed = true;
            }
        }
    }
    if (b->ready && b->ready->arrangement.revision != echoy_arrange_revision(b->arrange)) {
        echoy_band_discard_job(b->ready);
        b->ready = NULL;
    }
    if (b->ready && !b->ready->staged) {
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
    if (b->ready && b->ready->staged && echoy_arrange_commit(b->arrange, now, &commit)) {
        for (int i = 0; i < N; i++) {
            if (commit.changed_mask & (1u << i)) {
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
        sync_plans(b);
        echoy_band_discard_job(b->ready);
        b->ready = NULL;
    }
    const echoy_arrange_pending_t *pending = echoy_arrange_get_pending(b->arrange);
    for (int i = 0; i < N; i++) {
        struct voice *v = &b->voices[i];
        echoy_snapshot_t s;
        echoy_model_get_snapshot(v->model, &s);
        if (pending && (pending->job.changed_mask & (1u << i))) {
            echoy_model_set_external_pending(v->model, true, pending->at_us);
        }
        if (s.phase != ECHOY_PREPARING || s.plan_pending || !v->prepared ||
            v->condition != ECHOY_NORMAL) {
            continue;
        }
        const echoy_arrange_tile_t *t = echoy_arrange_get_tile(b->arrange, i);
        int64_t at = now + 50000;
        if (s.continuous) {
            if (!echoy_arrange_enable_loop(b->arrange, i, now, &at)) {
                continue;
            }
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
            v->automatic_preview = false;
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
    out->preparation_failed = v->failed;
    return true;
}
void echoy_band_render(echoy_band_t *b, int64_t at, float *out, size_t frames)
{
    if (!b || !out || at < 0) {
        return;
    }
    memset(out, 0, frames * sizeof(*out));
    const double smooth = exp(-1.0 / (.015 * ECHOY_AUDIO_SAMPLE_RATE));
    for (size_t k = 0; k < frames; k++) {
        int64_t t = at + (int64_t)(k * 1000000 / ECHOY_AUDIO_SAMPLE_RATE);
        double sum = 0;
        const echoy_arrange_pending_t *pending = echoy_arrange_get_pending(b->arrange);
        if (pending && t >= pending->at_us) {
            echoy_band_tick(b, t);
        }
        for (int i = 0; i < N; i++) {
            struct voice *v = &b->voices[i];
            double sample = 0;
            v->smoothed_volume = v->volume + (v->smoothed_volume - v->volume) * smooth;
            for (unsigned j = 0; j < RECORDS; j++) {
                struct playback *p = &v->play[j];
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
            for (unsigned j = 0; j < CUES; j++) {
                struct cue *c = &b->scheduled[j];
                if (!c->used || c->tile != i || t < c->at) {
                    continue;
                }
                const echoy_audio_buffer_t *buffer = b->cues[c->kind];
                uint64_t pos = ((uint64_t)(t - c->at) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
                if (pos >= buffer->frames) {
                    c->used = false;
                    continue;
                }
                sample += buffer->samples[pos];
            }
            sum += sample * v->smoothed_volume;
        }
        out[k] = (float)(sum * .45);
    }
    b->render_until = at + (int64_t)(frames * 1000000 / ECHOY_AUDIO_SAMPLE_RATE);
}
