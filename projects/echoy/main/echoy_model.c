#include "echoy_model.h"
#include <math.h>
#include <stdlib.h>

#define BEAT_US INT64_C(625000)
#define STEP_US INT64_C(156250)
#define BAR_US (4 * BEAT_US)
#define RECORD_LEAD_US INT64_C(60000)
#define OUTPUT_LEAD_US INT64_C(50000)
#define RESUME_LEAD_US INT64_C(100000)
#define PLAN_LEAD_US INT64_C(45000)
#define PLAN_RUNNING_LEAD_US INT64_C(120000)
struct echoy_model {
    echoy_snapshot_t state;
    echoy_plan_t current, staged;
    unsigned memories[4];
};

static bool capturing(const echoy_snapshot_t *s)
{
    return s->phase == ECHOY_COUNT_IN || s->phase == ECHOY_RECORDING;
}
static int64_t next_bar(const echoy_snapshot_t *s, int64_t now, int64_t lead)
{
    const int64_t delta = now + lead - s->clock_origin_us;
    return s->clock_origin_us + (delta > 0 ? (delta + BAR_US - 1) / BAR_US : 0) * BAR_US;
}
static void commit_pending(echoy_model_handle_t m, int64_t now)
{
    if (m->state.plan_pending && now >= m->state.apply_at_us) {
        m->current = m->staged;
        m->state.plan_pending = false;
        m->state.committed_revision = m->state.revision;
    }
}
static bool stage(echoy_model_handle_t m, int64_t now)
{
    echoy_snapshot_t *s = &m->state;
    if (!echoy_compose_mask(s->seed, s->role, s->phrase, s->density, &m->staged)) {
        return false;
    }
    ++s->revision;
    if (!s->seed) {
        m->current = m->staged;
        s->plan_pending = false;
        s->committed_revision = s->revision;
        return true;
    }
    s->apply_at_us = now + PLAN_LEAD_US;
    if (s->phase == ECHOY_PLAYING) {
        s->apply_at_us = next_bar(s, now, PLAN_RUNNING_LEAD_US);
    }
    if (s->phase == ECHOY_PREVIEW && s->start_us + BAR_US > s->apply_at_us) {
        s->apply_at_us = s->start_us + BAR_US;
    }
    s->plan_pending = true;
    return true;
}
echoy_model_handle_t echoy_model_create(void)
{
    echoy_model_handle_t m = calloc(1, sizeof(*m));
    if (m) {
        m->state.role = ECHOY_PULSE;
        m->state.phrase = 1;
        m->state.density = .5;
        for (unsigned i = 0; i < 4; ++i) {
            m->memories[i] = i == ECHOY_HARMONY ? 2 : 1;
        }
        if (!echoy_compose_mask(0, ECHOY_PULSE, 1, .5, &m->current)) {
            free(m);
            return NULL;
        }
    }
    return m;
}
void echoy_model_delete(echoy_model_handle_t m)
{
    free(m);
}
void echoy_model_get_snapshot(echoy_model_handle_t m, echoy_snapshot_t *out)
{
    if (m && out) {
        *out = m->state;
    }
}
const echoy_plan_t *echoy_model_get_plan(echoy_model_handle_t m)
{
    return m ? &m->current : NULL;
}
unsigned echoy_model_get_role_phrase(echoy_model_handle_t m, echoy_role_t role)
{
    return m && role >= ECHOY_PULSE && role <= ECHOY_HARMONY ? m->memories[role] : 0;
}
void echoy_model_pause(echoy_model_handle_t m)
{
    if (!m) {
        return;
    }
    echoy_snapshot_t *s = &m->state;
    ++s->generation;
    s->phase = ECHOY_IDLE;
    s->continuous = false;
    s->pending_seed = 0;
    s->capture_at_us = 0;
}
bool echoy_model_record(echoy_model_handle_t m, int64_t now)
{
    if (!m || now < 0) {
        return false;
    }
    echoy_snapshot_t *s = &m->state;
    if (capturing(s)) {
        echoy_model_pause(m);
        s->notice = ECHOY_NOTICE_CANCELLED;
        return true;
    }
    commit_pending(m, now);
    if (s->fault || s->following || s->plan_pending || s->phase == ECHOY_PREPARING) {
        return false;
    }
    echoy_model_pause(m);
    s->notice = ECHOY_NOTICE_NONE;
    s->capture_at_us = now + RECORD_LEAD_US + 2 * BEAT_US;
    s->phase = ECHOY_COUNT_IN;
    return true;
}
bool echoy_model_tap(echoy_model_handle_t m, int64_t now)
{
    if (!m || now < 0 || m->state.fault) {
        return false;
    }
    echoy_snapshot_t *s = &m->state;
    const int64_t relative = now - s->capture_at_us;
    if (capturing(s) && relative >= 0 && relative < BAR_US) {
        unsigned step = (unsigned)((relative + STEP_US / 2) / STEP_US);
        if (step > 15) {
            step = 15;
        }
        s->pending_seed |= (uint16_t)(1u << step);
        s->phase = ECHOY_RECORDING;
    }
    return true;
}
void echoy_model_tick(echoy_model_handle_t m, int64_t now)
{
    if (!m || now < 0) {
        return;
    }
    echoy_snapshot_t *s = &m->state;
    commit_pending(m, now);
    if (s->phase == ECHOY_COUNT_IN && now >= s->capture_at_us) {
        s->phase = ECHOY_RECORDING;
    }
    if (s->phase == ECHOY_RECORDING && now >= s->capture_at_us + BAR_US) {
        if (s->pending_seed && !s->following) {
            const uint16_t old = s->seed;
            s->seed = s->pending_seed;
            s->pending_seed = 0;
            s->phase = ECHOY_PREPARING;
            s->continuous = false;
            if (!stage(m, now)) {
                s->seed = old;
                echoy_model_set_fault(m, true);
            }
        } else {
            echoy_model_pause(m);
            s->notice = ECHOY_NOTICE_EMPTY;
        }
    }
    if (s->phase == ECHOY_PREVIEW && now >= s->start_us + BAR_US) {
        echoy_model_pause(m);
    }
}
bool echoy_model_transport(echoy_model_handle_t m, int64_t now)
{
    if (!m || now < 0 || m->state.fault || capturing(&m->state)) {
        return false;
    }
    echoy_snapshot_t *s = &m->state;
    commit_pending(m, now);
    s->notice = ECHOY_NOTICE_NONE;
    if (s->phase != ECHOY_IDLE) {
        echoy_model_pause(m);
        return true;
    }
    if (!m->current.count || s->plan_pending) {
        return false;
    }
    ++s->generation;
    s->continuous = true;
    s->phase = ECHOY_PREPARING;
    return true;
}
bool echoy_model_audition(echoy_model_handle_t m, int64_t now)
{
    if (!m || now < 0 || m->state.fault || capturing(&m->state)) {
        return false;
    }
    commit_pending(m, now);
    if (m->state.plan_pending || !m->current.count) {
        return false;
    }
    echoy_model_pause(m);
    m->state.notice = ECHOY_NOTICE_NONE;
    m->state.phase = ECHOY_PREPARING;
    return true;
}
bool echoy_model_prepare_done(echoy_model_handle_t m, uint32_t generation, int64_t now)
{
    if (!m || now < 0) {
        return false;
    }
    echoy_snapshot_t *s = &m->state;
    if (s->generation != generation || s->phase != ECHOY_PREPARING || s->fault) {
        return false;
    }
    commit_pending(m, now);
    if (s->plan_pending || !m->current.count) {
        return false;
    }
    s->start_us = now + OUTPUT_LEAD_US;
    if (s->continuous) {
        if (!s->clock_started) {
            s->clock_origin_us = s->start_us;
            s->clock_started = true;
        } else {
            s->start_us = next_bar(s, now, RESUME_LEAD_US);
        }
        s->phase = ECHOY_PLAYING;
    } else {
        s->phase = ECHOY_PREVIEW;
    }
    return true;
}
static bool can_configure(echoy_model_handle_t m, int64_t now)
{
    if (!m || now < 0 || m->state.fault || capturing(&m->state)) {
        return false;
    }
    commit_pending(m, now);
    if (m->state.phase == ECHOY_PREVIEW && now < m->state.start_us) {
        echoy_model_pause(m);
    }
    return true;
}
bool echoy_model_select_role(echoy_model_handle_t m, echoy_role_t role, int64_t now)
{
    if (role < ECHOY_PULSE || role > ECHOY_HARMONY || !can_configure(m, now)) {
        return false;
    }
    m->memories[m->state.role] = m->state.phrase;
    m->state.role = role;
    m->state.phrase = m->memories[role];
    m->state.notice = ECHOY_NOTICE_NONE;
    return stage(m, now);
}
bool echoy_model_select_phrase(echoy_model_handle_t m, unsigned phrase, int64_t now)
{
    if (phrase < 1 || phrase > 3 || !can_configure(m, now)) {
        return false;
    }
    m->state.phrase = phrase;
    m->memories[m->state.role] = phrase;
    m->state.notice = ECHOY_NOTICE_NONE;
    return stage(m, now);
}
bool echoy_model_set_density(echoy_model_handle_t m, double density, int64_t now)
{
    if (!can_configure(m, now)) {
        return false;
    }
    m->state.density = !isfinite(density) ? .5 : density < 0 ? 0 : density > 1 ? 1 : density;
    m->state.notice = ECHOY_NOTICE_NONE;
    return stage(m, now);
}
bool echoy_model_load_seed(echoy_model_handle_t m, uint16_t seed, int64_t now, bool preview)
{
    if (!m || !seed || now < 0 || m->state.fault || m->state.following || capturing(&m->state)) {
        return false;
    }
    echoy_model_pause(m);
    m->state.seed = seed;
    m->state.notice = ECHOY_NOTICE_NONE;
    if (preview) {
        m->state.phase = ECHOY_PREPARING;
    }
    return stage(m, now);
}
void echoy_model_set_following(echoy_model_handle_t m, bool following)
{
    if (!m) {
        return;
    }
    if (following && capturing(&m->state)) {
        echoy_model_pause(m);
        m->state.notice = ECHOY_NOTICE_CANCELLED;
    }
    m->state.following = following;
}
void echoy_model_set_fault(echoy_model_handle_t m, bool fault)
{
    if (!m) {
        return;
    }
    echoy_model_pause(m);
    m->state.fault = fault;
}
