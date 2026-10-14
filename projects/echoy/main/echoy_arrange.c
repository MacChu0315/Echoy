#include "echoy_arrange.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BAR_SECONDS 2.5
#define STEP_SECONDS (BAR_SECONDS / 16.0)

struct echoy_arrange {
    uint64_t revision;
    size_t edge_count, wanted_count;
    echoy_arrange_edge_t edges[ECHOY_ARRANGE_MAX_EDGES];
    echoy_arrange_edge_t wanted[ECHOY_ARRANGE_MAX_EDGES];
    echoy_arrange_tile_t tiles[ECHOY_ARRANGE_TILE_COUNT];
    bool external[ECHOY_ARRANGE_TILE_COUNT];
    echoy_arrange_plan_t imported[ECHOY_ARRANGE_TILE_COUNT];
    echoy_arrange_clock_t clocks[ECHOY_ARRANGE_TILE_COUNT];
    /* Retain JS clock arithmetic between calls; the public scheduler uses
     * rounded microseconds. This also preserves ceil behavior at boundaries. */
    double origin_seconds[ECHOY_ARRANGE_TILE_COUNT], pending_seconds;
    bool has_pending;
    bool pending_fixed;
    echoy_tile_id_t pending_clock;
    echoy_arrange_pending_t pending;
};

static bool valid_tile(echoy_tile_id_t id) { return id >= ECHOY_TILE_A && id <= ECHOY_TILE_C; }
static bool valid_role(echoy_role_t role) { return role >= ECHOY_PULSE && role <= ECHOY_HARMONY; }
static double minimum(double a, double b) { return a < b ? a : b; }
static double maximum(double a, double b) { return a > b ? a : b; }
static bool microseconds(double seconds, int64_t *out)
{
    long double value = floorl((long double)seconds * 1000000.0L + .5L);
    if (!isfinite(seconds) || value < (long double)INT64_MIN || value > (long double)INT64_MAX) return false;
    *out = (int64_t)value;
    return true;
}

static size_t clean_seed(const echoy_seed_note_t *notes, size_t count,
                         echoy_seed_note_t out[16])
{
    echoy_seed_note_t slots[16] = {0};
    bool used[16] = {false};
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(notes[i].step)) continue;
        double value = maximum(0, minimum(15, notes[i].step));
        double base = floor(value);
        unsigned step = (unsigned)(base + (value - base >= .5 ? 1 : 0));
        echoy_note_kind_t kind = notes[i].kind;
        if (kind < ECHOY_KICK || kind > ECHOY_HAT) kind = ECHOY_KICK;
        double velocity = isfinite(notes[i].velocity) ? notes[i].velocity : .73;
        slots[step] = (echoy_seed_note_t){step, maximum(.16, minimum(.85, velocity)), kind};
        used[step] = true;
    }
    size_t length = 0;
    for (unsigned i = 0; i < 16; ++i) if (used[i]) out[length++] = slots[i];
    return length;
}

static bool contains(const double *values, size_t count, double value)
{
    for (size_t i = 0; i < count; ++i) if (values[i] == value) return true;
    return false;
}

static void insert(double *values, size_t *count, double value)
{
    if (!contains(values, *count, value)) values[(*count)++] = value;
}

static int nearest(int note, int low, int high)
{
    const int scale[] = {50, 53, 55, 57, 58, 60, 62, 65, 67, 69, 70, 72, 74, 77, 79, 81, 82, 84, 86};
    int best = 62;
    for (size_t i = 0; i < sizeof(scale) / sizeof(scale[0]); ++i) {
        int candidate = scale[i];
        if (candidate >= low && candidate <= high && abs(candidate - note) < abs(best - note)) best = candidate;
    }
    return best;
}

static bool note_pitched(const echoy_note_t *note, int *pitch)
{
    if (note->note_count) { *pitch = note->notes[note->note_count - 1]; return true; }
    if (note->kind == ECHOY_TONE || note->kind == ECHOY_GRAIN) { *pitch = note->note; return true; }
    return false;
}

static bool valid_music(const echoy_plan_t *p)
{
    if (!p || !valid_role(p->role) || p->source_count > 16 || p->count > ECHOY_MAX_EVENTS) return false;
    for (size_t i = 0; i < p->count; ++i) {
        if (p->events[i].note_count > 3 || p->events[i].harmony_count > 3) return false;
    }
    return true;
}

bool echoy_arrange_linked(const echoy_plan_t *local, const echoy_plan_t *upstream,
                          echoy_plan_t *out)
{
    if (!out || out == upstream || !valid_music(local) || !valid_music(upstream)) return false;
    if (out != local) *out = *local;
    double used[ECHOY_MAX_EVENTS], left_support[ECHOY_MAX_EVENTS];
    size_t used_count = 0, support_count = 0, echo_count = 0, echo[2] = {0};
    const echoy_note_t *first_anchor = NULL;
    for (size_t i = 0; i < out->count; ++i) {
        if (out->events[i].layer == ECHOY_ANCHOR) insert(used, &used_count, out->events[i].step);
        if (out->events[i].layer == ECHOY_ECHO) {
            if (echo_count < 2) echo[echo_count] = i;
            ++echo_count;
        }
    }
    for (size_t i = 0; i < upstream->count; ++i) {
        if (upstream->events[i].layer == ECHOY_ANCHOR) {
            if (!first_anchor) first_anchor = &upstream->events[i];
        } else insert(left_support, &support_count, upstream->events[i].step);
    }
    if (echo_count == 2) {
        const int shifts[] = {0, 1, -1, 2, -2};
        for (size_t s = 0; s < sizeof(shifts) / sizeof(shifts[0]); ++s) {
            bool fits = true;
            for (size_t i = 0; i < 2; ++i) {
                double step = out->events[echo[i]].step + shifts[s];
                if (step < 0 || step >= 16 || contains(used, used_count, step) ||
                    (shifts[s] != -2 && contains(left_support, support_count, step))) fits = false;
            }
            if (fits) {
                for (size_t i = 0; i < 2; ++i) {
                    out->events[echo[i]].step += shifts[s];
                    insert(used, &used_count, out->events[echo[i]].step);
                }
                break;
            }
        }
    }
    double head = out->source_count ? out->source[0].step : 0;
    for (size_t i = 0; i < out->count; ++i) {
        echoy_note_t *n = &out->events[i];
        if (n->layer == ECHOY_ANCHOR) continue;
        if (n->layer != ECHOY_ECHO) {
            const double choices[] = {n->step, n->step + 1, n->step - 1, n->step + 2, n->step - 2};
            for (size_t j = 0; j < sizeof(choices) / sizeof(choices[0]); ++j) {
                double step = choices[j];
                if (step >= head && step < 16 && !contains(used, used_count, step) &&
                    !contains(left_support, support_count, step)) { n->step = step; break; }
            }
            insert(used, &used_count, n->step);
        }
        n->duration = minimum(n->duration, maximum(.025, BAR_SECONDS - n->step * STEP_SECONDS - .018));
    }
    for (size_t i = 0; i < out->count; ++i) {
        echoy_note_t *n = &out->events[i];
        double step = n->has_source_step ? n->source_step : n->step;
        const echoy_note_t *parent = NULL;
        for (size_t j = 0; j < upstream->count; ++j) {
            if (upstream->events[j].layer == ECHOY_ANCHOR && upstream->events[j].step <= step)
                parent = &upstream->events[j];
        }
        if (!parent) parent = first_anchor;
        int pitch;
        if (!parent || !note_pitched(parent, &pitch)) continue;
        if (out->role == ECHOY_VOICE) {
            int octave = upstream->role == ECHOY_VOICE ? 12 : 0;
            int interval = n->layer == ECHOY_ANCHOR || n->layer == ECHOY_ECHO ? 0 : 2;
            n->note = nearest(pitch + octave + interval, 55, 86);
        } else if (out->role == ECHOY_HARMONY) {
            int harmony[3] = {50, 57, nearest(pitch, 60, 74)};
            if (parent->harmony_count == 3) memcpy(harmony, parent->harmony, sizeof(harmony));
            memcpy(n->harmony, harmony, sizeof(harmony));
            n->harmony_count = 3;
            if (n->note_count == 3) memcpy(n->notes, harmony, sizeof(harmony));
            else for (unsigned j = 0; j < n->note_count; ++j)
                n->notes[j] = harmony[(j + (n->layer == ECHOY_ANCHOR ? 2 : 1)) % 3];
            if (n->note_count) n->note = n->notes[n->note_count - 1];
        } else if (out->role == ECHOY_DUST) n->note = nearest(pitch + 24, 74, 86);
    }
    /* Stable chronological order, with anchors before support at equal steps. */
    for (size_t i = 1; i < out->count; ++i) {
        echoy_note_t value = out->events[i];
        size_t j = i;
        while (j && (out->events[j - 1].step > value.step ||
              (out->events[j - 1].step == value.step && value.layer == ECHOY_ANCHOR &&
               out->events[j - 1].layer != ECHOY_ANCHOR))) {
            out->events[j] = out->events[j - 1];
            --j;
        }
        out->events[j] = value;
    }
    return true;
}

echoy_arrange_t *echoy_arrange_create(void)
{
    echoy_arrange_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    const echoy_role_t roles[] = {ECHOY_PULSE, ECHOY_HARMONY, ECHOY_VOICE};
    for (int i = 0; i < ECHOY_ARRANGE_TILE_COUNT; ++i) {
        echoy_arrange_tile_t *d = &m->tiles[i];
        d->id = d->clock_id = (echoy_tile_id_t)i;
        d->role = roles[i]; d->phrase = 1;
        d->density = .5; d->present = i == ECHOY_TILE_A; d->use_own = true;
        for (int role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) d->memories[role] = 1;
        d->current.source = ECHOY_TILE_NONE;
    }
    return m;
}

void echoy_arrange_delete(echoy_arrange_t *model) { free(model); }
uint64_t echoy_arrange_revision(const echoy_arrange_t *model) { return model ? model->revision : 0; }
const echoy_arrange_tile_t *echoy_arrange_get_tile(const echoy_arrange_t *m, echoy_tile_id_t id)
{ return m && valid_tile(id) ? &m->tiles[id] : NULL; }
const echoy_arrange_clock_t *echoy_arrange_get_clock(const echoy_arrange_t *m, echoy_tile_id_t id)
{ return m && valid_tile(id) ? &m->clocks[id] : NULL; }
const echoy_arrange_pending_t *echoy_arrange_get_pending(const echoy_arrange_t *m)
{ return m && m->has_pending ? &m->pending : NULL; }
const echoy_arrange_edge_t *echoy_arrange_get_edges(const echoy_arrange_t *m, bool wanted, size_t *count)
{
    if (count) *count = m ? (wanted ? m->wanted_count : m->edge_count) : 0;
    return m ? (wanted ? m->wanted : m->edges) : NULL;
}
uint64_t echoy_arrange_invalidate(echoy_arrange_t *m)
{
    if (!m) return 0;
    m->has_pending = false;
    return ++m->revision;
}

bool echoy_arrange_set_seed(echoy_arrange_t *m, echoy_tile_id_t id,
                            const echoy_seed_note_t *notes, size_t count)
{
    if (!m || !valid_tile(id) || (!notes && count)) return false;
    echoy_seed_note_t cleaned[16] = {0};
    size_t length = clean_seed(notes, count, cleaned);
    if (!length) return false;
    echoy_arrange_tile_t *d = &m->tiles[id];
    d->own_count = d->fallback_count = length; d->use_own = true;
    memcpy(d->own_seed, cleaned, length * sizeof(cleaned[0]));
    memcpy(d->fallback, cleaned, length * sizeof(cleaned[0]));
    echoy_arrange_invalidate(m);
    return true;
}

bool echoy_arrange_configure(echoy_arrange_t *m, echoy_tile_id_t id,
                             const echoy_arrange_patch_t *patch)
{
    if (!m || !valid_tile(id) || !patch) return false;
    echoy_arrange_tile_t *d = &m->tiles[id];
    if (patch->set_role) {
        if (!valid_role(patch->role)) return false;
        d->memories[d->role] = d->phrase;
        d->role = patch->role; d->phrase = d->memories[d->role];
    }
    if (patch->set_phrase) {
        if (patch->phrase < 1 || patch->phrase > 3) return false;
        d->phrase = patch->phrase; d->memories[d->role] = patch->phrase;
    }
    if (patch->set_density) d->density = isfinite(patch->density) ? maximum(0, minimum(1, patch->density)) : .5;
    echoy_arrange_invalidate(m);
    return true;
}

bool echoy_arrange_set_present(echoy_arrange_t *m, echoy_tile_id_t id, bool present)
{
    if (!m || !valid_tile(id)) return false;
    m->tiles[id].present = present;
    if (!present) m->tiles[id].loop = false;
    echoy_arrange_invalidate(m);
    return true;
}

bool echoy_arrange_edges_valid(const echoy_arrange_edge_t *edges, size_t count)
{
    if ((!edges && count) || count > ECHOY_ARRANGE_MAX_EDGES) return false;
    echoy_tile_id_t parents[3] = {ECHOY_TILE_NONE, ECHOY_TILE_NONE, ECHOY_TILE_NONE};
    echoy_tile_id_t children[3] = {ECHOY_TILE_NONE, ECHOY_TILE_NONE, ECHOY_TILE_NONE};
    for (size_t i = 0; i < count; ++i) {
        echoy_tile_id_t left = edges[i].left, right = edges[i].right;
        if (!valid_tile(left) || !valid_tile(right) || left == right ||
            parents[right] != ECHOY_TILE_NONE || children[left] != ECHOY_TILE_NONE) return false;
        parents[right] = left; children[left] = right;
    }
    for (int i = 0; i < 3; ++i) {
        unsigned seen = 0;
        echoy_tile_id_t id = (echoy_tile_id_t)i;
        while (parents[id] != ECHOY_TILE_NONE) {
            if (seen & (1u << id)) return false;
            seen |= 1u << id; id = parents[id];
        }
    }
    return true;
}
bool echoy_arrange_set_edges(echoy_arrange_t *m, const echoy_arrange_edge_t *edges, size_t count)
{
    if (!m || !echoy_arrange_edges_valid(edges, count)) return false;
    echoy_arrange_edge_t filtered[ECHOY_ARRANGE_MAX_EDGES];
    size_t length = 0;
    for (size_t i = 0; i < count; ++i) {
        if (m->tiles[edges[i].left].present && m->tiles[edges[i].right].present) filtered[length++] = edges[i];
    }
    bool same = length == m->wanted_count;
    for (size_t i = 0; same && i < length; ++i)
        same = filtered[i].left == m->wanted[i].left && filtered[i].right == m->wanted[i].right;
    if (!same) {
        memcpy(m->wanted, filtered, length * sizeof(filtered[0]));
        m->wanted_count = length; echoy_arrange_invalidate(m);
    }
    return true;
}

bool echoy_arrange_release(echoy_arrange_t *m, echoy_tile_id_t id)
{
    if (!m || !valid_tile(id)) return false;
    echoy_tile_id_t old = m->tiles[id].clock_id;
    m->clocks[id] = m->clocks[old];
    m->origin_seconds[id] = m->origin_seconds[old];
    for (int i = 0; i < ECHOY_ARRANGE_TILE_COUNT; ++i) {
        echoy_tile_id_t root = (echoy_tile_id_t)i;
        for (int depth = 0; depth < ECHOY_ARRANGE_TILE_COUNT; ++depth) {
            echoy_tile_id_t parent = ECHOY_TILE_NONE;
            for (size_t j = 0; j < m->wanted_count; ++j)
                if (m->wanted[j].right == root) parent = m->wanted[j].left;
            if (parent == ECHOY_TILE_NONE) break;
            root = parent;
        }
        if (root == id) m->tiles[i].clock_id = id;
    }
    echoy_arrange_plan_t *p = &m->tiles[id].current;
    if (p->valid && p->source != ECHOY_TILE_NONE) {
        p->source = ECHOY_TILE_NONE;
        p->has_detached = p->detached = true;
    }
    size_t kept = 0;
    for (size_t j = 0; j < m->edge_count; ++j)
        if (m->edges[j].right != id) m->edges[kept++] = m->edges[j];
    m->edge_count = kept;
    echoy_arrange_invalidate(m);
    return true;
}

static bool same_note(const echoy_note_t *a, const echoy_note_t *b)
{
    if (a->step != b->step || a->velocity != b->velocity || a->duration != b->duration ||
        a->max_duration != b->max_duration || a->expression != b->expression ||
        a->kind != b->kind || a->layer != b->layer || a->articulation != b->articulation ||
        a->note != b->note || a->note_count != b->note_count || a->harmony_count != b->harmony_count ||
        a->has_source_step != b->has_source_step || (a->has_source_step && a->source_step != b->source_step)) return false;
    for (unsigned i = 0; i < a->note_count; ++i) if (a->notes[i] != b->notes[i]) return false;
    for (unsigned i = 0; i < a->harmony_count; ++i) if (a->harmony[i] != b->harmony[i]) return false;
    return true;
}

static bool same_signature(const echoy_arrange_plan_t *a, const echoy_arrange_plan_t *b)
{
    if (a->valid != b->valid) return false;
    if (!a->valid) return true;
    if (a->source != b->source || a->music.role != b->music.role || a->music.phrase != b->music.phrase ||
        a->music.density != b->music.density || a->music.count != b->music.count) return false;
    for (size_t i = 0; i < a->music.count; ++i)
        if (!same_note(&a->music.events[i], &b->music.events[i])) return false;
    return true;
}

static uint8_t changes(const echoy_arrange_t *m, const echoy_arrange_job_t *job)
{
    uint8_t mask = 0;
    for (int i = 0; i < 3; ++i) {
        if (!same_signature(&job->plans[i], &m->tiles[i].current) ||
            (job->clock_id[i] != ECHOY_TILE_NONE && job->clock_id[i] != m->tiles[i].clock_id)) mask |= 1u << i;
    }
    return mask;
}

bool echoy_arrange_import(echoy_arrange_t *m, echoy_tile_id_t id, const echoy_plan_t *plan)
{
    if (!m || !valid_tile(id) || !valid_music(plan) || plan->phrase < 1 ||
        plan->phrase > 3 || !isfinite(plan->density) || plan->density < 0 ||
        plan->density > 1 || !isfinite(plan->duration) || plan->duration != BAR_SECONDS ||
        ((plan->source_count == 0) != (plan->count == 0))) return false;
    /* The wire codec validates individual notes before this owner-only call. */
    echoy_arrange_plan_t *p = &m->imported[id];
    bool same = m->external[id] && p->valid == (plan->source_count != 0) &&
                p->music.role == plan->role && p->music.phrase == plan->phrase &&
                p->music.density == plan->density && p->music.source_count == plan->source_count &&
                p->music.count == plan->count;
    for (size_t i = 0; same && i < plan->source_count; ++i)
        same = p->music.source[i].step == plan->source[i].step &&
               p->music.source[i].velocity == plan->source[i].velocity &&
               p->music.source[i].kind == plan->source[i].kind;
    for (size_t i = 0; same && i < plan->count; ++i)
        same = same_note(&p->music.events[i], &plan->events[i]);
    if (same) return true;
    m->external[id] = true;
    p->valid = plan->source_count != 0;
    p->source = ECHOY_TILE_NONE;
    p->music = *plan;
    m->tiles[id].role = plan->role;
    m->tiles[id].phrase = plan->phrase;
    m->tiles[id].density = plan->density;
    echoy_arrange_invalidate(m);
    return true;
}

bool echoy_arrange_clock_sync(echoy_arrange_t *m, echoy_tile_id_t id,
                              int64_t origin_us, bool started, int64_t now_us)
{
    if (!m || !valid_tile(id) || origin_us < 0 || now_us < 0) return false;
    bool phase_changed = (origin_us - m->clocks[id].origin_us) % ECHOY_ARRANGE_BAR_US != 0;
    m->clocks[id] = (echoy_arrange_clock_t){origin_us, started};
    m->origin_seconds[id] = (double)origin_us / 1000000.0;
    if (m->has_pending && !m->pending_fixed && m->pending_clock == id && phase_changed) {
        /* A correction received after synthesis must also move its queued
         * replacement. Choose the nearest new bar, keeping the render lead. */
        double origin = m->origin_seconds[id];
        double old_at = m->pending_seconds;
        double at = origin + floor((old_at - origin) / BAR_SECONDS + .5) * BAR_SECONDS;
        double earliest = (double)now_us / 1000000.0 + .045;
        if (at < earliest) at += ceil((earliest - at) / BAR_SECONDS) * BAR_SECONDS;
        if (!microseconds(at, &m->pending.at_us)) return false;
        m->pending_seconds = at;
    }
    return true;
}

bool echoy_arrange_peer_loop(echoy_arrange_t *m, echoy_tile_id_t id, bool playing)
{
    if (!m || !valid_tile(id) || !m->external[id]) return false;
    m->tiles[id].loop = playing && m->tiles[id].present;
    return true;
}

static bool compile_tile(const echoy_arrange_t *m, echoy_arrange_job_t *job, echoy_tile_id_t id,
                          const echoy_tile_id_t parents[3], bool visited[3], bool visiting[3])
{
    if (visited[id]) return true;
    if (visiting[id]) return false;
    visiting[id] = true;
    const echoy_arrange_tile_t *d = &m->tiles[id];
    echoy_arrange_plan_t *p = &job->plans[id];
    if (!d->present) { visited[id] = true; visiting[id] = false; return true; }
    if (m->external[id]) {
        *p = m->imported[id]; job->clock_id[id] = id;
        visited[id] = true; visiting[id] = false; return true;
    }
    echoy_tile_id_t parent = parents[id];
    if (parent != ECHOY_TILE_NONE && !compile_tile(m, job, parent, parents, visited, visiting)) return false;
    const echoy_arrange_plan_t *up = parent == ECHOY_TILE_NONE ? NULL : &job->plans[parent];
    if (up && !up->valid) up = NULL;
    job->clock_id[id] = up ? job->clock_id[parent] : d->clock_id;
    bool unchanged = d->current.valid && d->current.music.role == d->role &&
        d->current.music.phrase == d->phrase && d->current.music.density == d->density;
    if (up) {
        if (!echoy_compose(up->music.source, up->music.source_count, d->role, d->phrase, d->density, &p->music) ||
            !echoy_arrange_linked(&p->music, &up->music, &p->music)) return false;
        p->has_detached = true; p->detached = false;
    } else if (unchanged && (!d->use_own || d->current.source != ECHOY_TILE_NONE)) {
        *p = d->current; p->has_detached = true; p->detached = true;
    } else {
        if (!echoy_compose(d->use_own ? d->own_seed : d->fallback,
                          d->use_own ? d->own_count : d->fallback_count,
                          d->role, d->phrase, d->density, &p->music)) return false;
    }
    p->valid = p->music.source_count != 0;
    p->source = up ? parent : ECHOY_TILE_NONE;
    visited[id] = true; visiting[id] = false;
    return true;
}

bool echoy_arrange_compile(const echoy_arrange_t *m, echoy_arrange_job_t *job)
{
    if (!m || !job) return false;
    memset(job, 0, sizeof(*job)); job->revision = m->revision;
    echoy_tile_id_t parents[3] = {ECHOY_TILE_NONE, ECHOY_TILE_NONE, ECHOY_TILE_NONE};
    bool visited[3] = {false}, visiting[3] = {false};
    job->edge_count = m->wanted_count;
    memcpy(job->edges, m->wanted, m->wanted_count * sizeof(m->wanted[0]));
    for (size_t i = 0; i < m->wanted_count; ++i) parents[m->wanted[i].right] = m->wanted[i].left;
    for (int i = 0; i < 3; ++i) { job->clock_id[i] = ECHOY_TILE_NONE; job->plans[i].source = ECHOY_TILE_NONE; }
    for (int i = 0; i < 3; ++i)
        if (!compile_tile(m, job, (echoy_tile_id_t)i, parents, visited, visiting)) return false;
    job->changed_mask = changes(m, job);
    return true;
}

bool echoy_arrange_stage(echoy_arrange_t *m, const echoy_arrange_job_t *job, int64_t now_us)
{
    if (!m || !job || job->revision != m->revision || job->edge_count > ECHOY_ARRANGE_MAX_EDGES) return false;
    for (int i = 0; i < 3; ++i) {
        if (job->clock_id[i] != ECHOY_TILE_NONE && !valid_tile(job->clock_id[i])) return false;
        if (job->plans[i].valid && (!valid_music(&job->plans[i].music) ||
            (job->plans[i].source != ECHOY_TILE_NONE && !valid_tile(job->plans[i].source)))) return false;
    }
    uint8_t mask = changes(m, job);
    echoy_tile_id_t driver = ECHOY_TILE_NONE;
    for (int i = 0; i < 3; ++i) if ((mask & (1u << i)) && m->tiles[i].loop) { driver = (echoy_tile_id_t)i; break; }
    if (driver == ECHOY_TILE_NONE) {
        for (int i = 0; i < 3 && driver == ECHOY_TILE_NONE; ++i) if (mask & (1u << i)) {
            for (int j = 0; j < 3; ++j) if (m->tiles[j].loop && job->clock_id[i] == m->tiles[j].clock_id) {
                driver = (echoy_tile_id_t)i; break;
            }
        }
    }
    double now = (double)now_us / 1000000.0, at = now + .045;
    m->pending_clock = ECHOY_TILE_NONE;
    if (driver != ECHOY_TILE_NONE) {
        echoy_tile_id_t clock = job->clock_id[driver] == ECHOY_TILE_NONE ? m->tiles[driver].clock_id : job->clock_id[driver];
        m->pending_clock = clock;
        double origin = m->origin_seconds[clock];
        at = origin + ceil((now + .12 - origin) / BAR_SECONDS) * BAR_SECONDS;
    }
    int64_t at_us;
    if (!microseconds(at, &at_us)) return false;
    m->pending.job = *job; m->pending.job.changed_mask = mask;
    m->pending.at_us = at_us; m->pending_seconds = at; m->has_pending = true;
    m->pending_fixed = false;
    return true;
}

bool echoy_arrange_stage_at(echoy_arrange_t *m, const echoy_arrange_job_t *job, int64_t at_us)
{
    if (at_us < 0 || !echoy_arrange_stage(m, job, 0)) return false;
    m->pending.at_us = at_us;
    m->pending_seconds = (double)at_us / 1000000.0;
    m->pending_fixed = true;
    return true;
}
void echoy_arrange_unstage(echoy_arrange_t *m)
{
    if (m) { m->has_pending = false; m->pending_fixed = false; }
}

bool echoy_arrange_commit(echoy_arrange_t *m, int64_t now_us, echoy_arrange_commit_t *result)
{
    if (result) memset(result, 0, sizeof(*result));
    if (!m || !m->has_pending || (double)now_us / 1000000.0 + 1e-8 < m->pending_seconds) return false;
    const echoy_arrange_job_t *job = &m->pending.job;
    if (job->revision != m->revision) { m->has_pending = false; return false; }
    for (int i = 0; i < 3; ++i) if (job->changed_mask & (1u << i)) {
        echoy_arrange_tile_t *d = &m->tiles[i];
        d->current = job->plans[i];
        if (job->clock_id[i] != ECHOY_TILE_NONE) d->clock_id = job->clock_id[i];
        if (d->current.valid && d->current.source != ECHOY_TILE_NONE) {
            d->fallback_count = d->current.music.source_count;
            memcpy(d->fallback, d->current.music.source, d->fallback_count * sizeof(d->fallback[0]));
            d->use_own = false;
        }
    }
    m->edge_count = job->edge_count;
    memcpy(m->edges, job->edges, job->edge_count * sizeof(job->edges[0]));
    if (result) *result = (echoy_arrange_commit_t){job->revision, job->changed_mask, m->pending.at_us};
    m->has_pending = false;
    return true;
}

bool echoy_arrange_tick(echoy_arrange_t *m, int64_t now_us, echoy_arrange_commit_t *result)
{ return echoy_arrange_commit(m, now_us, result); }

bool echoy_arrange_enable_loop(echoy_arrange_t *m, echoy_tile_id_t id, int64_t now_us, int64_t *start_us)
{ return echoy_arrange_enable_loop_shared(m,id,now_us,false,start_us); }

bool echoy_arrange_enable_loop_shared(echoy_arrange_t *m, echoy_tile_id_t id, int64_t now_us,
                                      bool peer_playing, int64_t *start_us)
{
    if (!m || !valid_tile(id) || !start_us || !m->tiles[id].present || !m->tiles[id].current.valid) return false;
    echoy_arrange_tile_t *d = &m->tiles[id];
    echoy_tile_id_t clock = d->clock_id;
    bool established = peer_playing;
    for (int i = 0; i < 3; ++i) if (m->tiles[i].loop && m->tiles[i].clock_id == clock) established = true;
    double now = (double)now_us / 1000000.0, origin = m->origin_seconds[clock];
    double start = established ? origin + ceil((now + .10 - origin) / BAR_SECONDS) * BAR_SECONDS : now + .05;
    int64_t at;
    if (!microseconds(start, &at)) return false;
    if (!established) { m->origin_seconds[clock] = start; m->clocks[clock].origin_us = at; }
    m->clocks[clock].started = true; d->loop = true; *start_us = at;
    return true;
}

bool echoy_arrange_mute(echoy_arrange_t *m, echoy_tile_id_t id)
{
    if (!m || !valid_tile(id)) return false;
    m->tiles[id].loop = false;
    return true;
}

double echoy_arrange_phase(const echoy_arrange_t *m, echoy_tile_id_t id, int64_t now_us)
{
    if (!m || !valid_tile(id)) return 0;
    double difference = (double)now_us / 1000000.0 - m->origin_seconds[m->tiles[id].clock_id];
    return fmod(fmod(difference, BAR_SECONDS) + BAR_SECONDS, BAR_SECONDS) / BAR_SECONDS;
}
