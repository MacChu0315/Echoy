#include "echoy_model.h"
#include <stdlib.h>

#define BEAT_US INT64_C(625000)
#define STEP_US INT64_C(156250)
#define BAR_US (4 * BEAT_US)
#define RECORD_LEAD_US INT64_C(60000)
#define OUTPUT_LEAD_US INT64_C(50000)
#define RESUME_LEAD_US INT64_C(100000)
struct echoy_model {
    echoy_snapshot_t state;
};

static bool capturing(const echoy_snapshot_t *s)
{
    return s->phase == ECHOY_COUNT_IN || s->phase == ECHOY_RECORDING;
}

echoy_model_handle_t echoy_model_create(void)
{
    return calloc(1, sizeof(struct echoy_model));
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
void echoy_model_pause(echoy_model_handle_t m)
{
    if (!m) {
        return;
    }
    echoy_snapshot_t *s = &m->state;
    s->generation++;
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
    if (s->fault || s->following) {
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
    if (s->phase == ECHOY_COUNT_IN && now >= s->capture_at_us) {
        s->phase = ECHOY_RECORDING;
    }
    if (s->phase == ECHOY_RECORDING && now >= s->capture_at_us + BAR_US) {
        if (s->pending_seed && !s->following) {
            s->seed = s->pending_seed;
            s->pending_seed = 0;
            s->phase = ECHOY_PREPARING;
            s->continuous = false;
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
    s->notice = ECHOY_NOTICE_NONE;
    if (s->phase != ECHOY_IDLE) {
        echoy_model_pause(m);
        return true;
    }
    if (!s->seed) {
        return false;
    }
    s->generation++;
    s->continuous = true;
    s->phase = ECHOY_PREPARING;
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
    s->start_us = now + OUTPUT_LEAD_US;
    if (s->continuous) {
        if (!s->clock_started) {
            s->clock_origin_us = s->start_us;
            s->clock_started = true;
        } else {
            const int64_t delta = now + RESUME_LEAD_US - s->clock_origin_us;
            const int64_t bars = delta > 0 ? (delta + BAR_US - 1) / BAR_US : 0;
            s->start_us = s->clock_origin_us + bars * BAR_US;
        }
        s->phase = ECHOY_PLAYING;
    } else {
        s->phase = ECHOY_PREVIEW;
    }
    return true;
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
