#include "echoy_music.h"
#include <math.h>
#include <string.h>

/* Port of the frozen PulseLab.clean, ReplyAB.layered and
 * RoleAccompaniment.compose. Keep candidate order: density selects a prefix
 * before the final, stable chronological sort. */
#define STEP_DURATION (60.0 / 96.0 / 4.0)
#define BAR_DURATION (16.0 * STEP_DURATION)
#define MAX_SLOTS 32

typedef struct {
    double step, velocity;
    echoy_note_kind_t kind;
    echoy_layer_t layer;
    int source_index;
} support_slot_t;

static const int scale[] = {62, 65, 67, 69, 70, 72, 74};
static const int voicings[][3] = {
    {50, 57, 65}, {53, 60, 69}, {55, 62, 70},
    {50, 57, 65}, {48, 55, 67}, {53, 60, 69},
};

static double minimum(double a, double b) { return a < b ? a : b; }
static double maximum(double a, double b) { return a > b ? a : b; }
static double js_round(double value)
{
    double integral = floor(value);
    /* Adding .5 first can round the predecessor of .5 up to exactly 1. */
    return integral + (value - integral >= .5 ? 1 : 0);
}
static double clamp(double value, double low, double high)
{
    return maximum(low, minimum(high, isfinite(value) ? value : low));
}

static double drum_duration(echoy_note_kind_t kind)
{
    return kind == ECHOY_KICK ? .22 : kind == ECHOY_RIM ? .09 : .045;
}

static size_t clean_source(const echoy_seed_note_t *input, size_t count,
                           echoy_seed_note_t clean[16])
{
    echoy_seed_note_t slots[16] = {0};
    bool used[16] = {false};
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(input[i].step)) continue;
        /* Clamp before conversion so even finite DBL_MAX is safe. Math.round
         * resolves half ties toward +infinity, unlike C round(). */
        int step = (int)js_round(clamp(input[i].step, 0, 15));
        echoy_note_kind_t kind = input[i].kind;
        if (kind != ECHOY_KICK && kind != ECHOY_RIM && kind != ECHOY_HAT) kind = ECHOY_KICK;
        slots[step] = (echoy_seed_note_t){
            .step = step, .velocity = clamp(isfinite(input[i].velocity) ? input[i].velocity : .73, .16, .85),
            .kind = kind,
        };
        used[step] = true;
    }
    size_t length = 0;
    for (int i = 0; i < 16; ++i) if (used[i]) clean[length++] = slots[i];
    return length;
}

static int nearest_source(const echoy_plan_t *p, double step)
{
    int best = 0;
    for (size_t i = 1; i < p->source_count; ++i) {
        if (fabs(p->source[i].step - step) < fabs(p->source[best].step - step)) best = (int)i;
    }
    return best;
}

/* Equal gaps retain the first chronological pair, matching the JS comparator. */
static int closest_pair(const echoy_plan_t *p)
{
    int best = -1;
    double gap = 4;
    for (size_t i = 0; i + 1 < p->source_count; ++i) {
        double candidate = p->source[i + 1].step - p->source[i].step;
        if (candidate > 0 && candidate < gap) { best = (int)i; gap = candidate; }
    }
    return best;
}

static void sort_slots(support_slot_t *slots, size_t count)
{
    for (size_t i = 1; i < count; ++i) {
        support_slot_t value = slots[i];
        size_t j = i;
        while (j > 0 && slots[j - 1].step > value.step) {
            slots[j] = slots[j - 1];
            --j;
        }
        slots[j] = value;
    }
}

static void sort_events(echoy_plan_t *p)
{
    for (size_t i = 1; i < p->count; ++i) {
        echoy_note_t value = p->events[i];
        size_t j = i;
        while (j > 0 && (p->events[j - 1].step > value.step ||
               (p->events[j - 1].step == value.step && value.layer == ECHOY_ANCHOR &&
                p->events[j - 1].layer != ECHOY_ANCHOR))) {
            p->events[j] = p->events[j - 1];
            --j;
        }
        p->events[j] = value;
    }
}

static void add_drum(support_slot_t slots[MAX_SLOTS], size_t *count, unsigned used[16],
                     unsigned anchor_steps, double step, echoy_note_kind_t kind,
                     double velocity, echoy_layer_t layer, int source_index)
{
    step = js_round(step);
    if (step < 0 || step > 15) return;
    unsigned at = (unsigned)step;
    if ((anchor_steps & (1u << at)) || (used[at] & (1u << kind))) return;
    if (*count >= MAX_SLOTS) return;
    used[at] |= 1u << kind;
    slots[(*count)++] = (support_slot_t){step, velocity, kind, layer, source_index};
}

static size_t layered(const echoy_plan_t *p, support_slot_t slots[MAX_SLOTS])
{
    unsigned used[16] = {0}, anchors = 0;
    double min_velocity = p->source[0].velocity;
    for (size_t i = 0; i < p->source_count; ++i) {
        unsigned step = (unsigned)p->source[i].step;
        anchors |= 1u << step;
        used[step] |= 1u << p->source[i].kind;
        min_velocity = minimum(min_velocity, p->source[i].velocity);
    }
    size_t count = 0;
    int pair = closest_pair(p);
    double head = p->source[0].step, last = p->source[p->source_count - 1].step;
    if (pair >= 0) {
        double gap = p->source[pair + 1].step - p->source[pair].step;
        const int offsets[] = {8, 4, 6, 10, 12};
        for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
            int at = (int)head + offsets[i];
            if (at > p->source[pair + 1].step && at + gap <= 14 &&
                !(anchors & (1u << at)) && !(anchors & (1u << (unsigned)(at + gap)))) {
                add_drum(slots, &count, used, anchors, at, ECHOY_RIM, .53, ECHOY_ECHO, pair);
                add_drum(slots, &count, used, anchors, at + gap, ECHOY_RIM, .47, ECHOY_ECHO, pair + 1);
                break;
            }
        }
    }
    add_drum(slots, &count, used, anchors, head + 8, ECHOY_KICK, .46, ECHOY_SUPPORT, 0);
    if (last < 12) {
        double closing = last < 4 ? head + 12 : minimum(14, ceil((last + 2) / 2) * 2);
        add_drum(slots, &count, used, anchors, closing, ECHOY_RIM, .49, ECHOY_ENDING, (int)p->source_count - 1);
    }
    add_drum(slots, &count, used, anchors, head + 4, ECHOY_RIM, .42, ECHOY_SUPPORT, -1);
    add_drum(slots, &count, used, anchors, head + 12, ECHOY_RIM, .42, ECHOY_SUPPORT, -1);
    int pulse = pair >= 0 && p->source[pair + 1].step - p->source[pair].step == 3 ? 3 : 2;
    for (int step = (int)head + pulse; step < 16; step += pulse) {
        add_drum(slots, &count, used, anchors, step, ECHOY_HAT, step % 4 == 2 ? .40 : .32, ECHOY_SUPPORT, -1);
    }
    if (last < 14) add_drum(slots, &count, used, anchors, 14, ECHOY_RIM, .41, ECHOY_ENDING, (int)p->source_count - 1);
    const int details[] = {7, 11, 15, 5, 9, 3, 13, 1};
    for (size_t i = 0; i < sizeof(details) / sizeof(details[0]); ++i) {
        add_drum(slots, &count, used, anchors, head + details[i], ECHOY_HAT, .26, ECHOY_DETAIL, -1);
    }
    size_t budget = (size_t)minimum(26 - p->source_count, 4 + js_round(p->density * 6));
    if (count > budget) count = budget;
    for (size_t i = 0; i < count; ++i) slots[i].velocity = minimum(slots[i].velocity, min_velocity * .82);
    sort_slots(slots, count);
    return count;
}

static void append_drum(echoy_plan_t *p, const support_slot_t *slot)
{
    echoy_note_t *n = &p->events[p->count++];
    n->step = slot->step;
    n->velocity = slot->velocity;
    n->duration = drum_duration(slot->kind);
    n->kind = slot->kind;
    n->layer = slot->layer;
    n->has_source_step = slot->source_index >= 0;
    if (n->has_source_step) n->source_step = p->source[slot->source_index].step;
}

static void compose_pulse(echoy_plan_t *p)
{
    support_slot_t base[MAX_SLOTS], support[MAX_SLOTS];
    size_t base_count = layered(p, base), support_count = 0;
    unsigned used[16] = {0};
    for (size_t i = 0; i < p->source_count; ++i) {
        const echoy_seed_note_t *seed = &p->source[i];
        support_slot_t anchor = {seed->step, seed->velocity, seed->kind, ECHOY_ANCHOR, (int)i};
        append_drum(p, &anchor);
        used[(unsigned)seed->step] |= 1u << seed->kind;
    }
    if (p->phrase == 1) {
        for (size_t i = 0; i < base_count; ++i) append_drum(p, &base[i]);
        return;
    }
    if (p->phrase == 2) {
        for (size_t i = 0; i < base_count; ++i) {
            const support_slot_t *n = &base[i];
            if (n->layer == ECHOY_ECHO || n->layer == ECHOY_ENDING) {
                add_drum(support, &support_count, used, 0, n->step, n->kind,
                         minimum(n->velocity * .98, .42), n->layer, n->source_index);
            }
        }
        const int routes[] = {3, 6, 10, 13, 7, 11, 15, 5, 9, 1, 14, 12, 4, 8, 2};
        size_t budget = 3 + (size_t)js_round(p->density * 5);
        double head = p->source[0].step, last = p->source[p->source_count - 1].step;
        for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
            int offset = routes[i];
            double at = head + offset;
            if (at > 15 || (at <= last && offset < 6)) continue;
            echoy_note_kind_t kind = offset % 4 == 3 || offset % 4 == 1 ? ECHOY_HAT : ECHOY_RIM;
            double velocity = kind == ECHOY_HAT ? (p->density > .62 ? .30 : .24) : .34;
            add_drum(support, &support_count, used, 0, at, kind, velocity, ECHOY_SUPPORT, nearest_source(p, at));
            if (support_count >= budget) break;
        }
    } else {
        size_t chosen_count = 0, budget = 4 + (size_t)js_round(p->density * 2);
        for (unsigned group = 0; group < 4; ++group) {
            for (size_t i = 0; i < base_count; ++i) {
                const support_slot_t *n = &base[i];
                bool choose = group == 0 ? n->layer == ECHOY_ECHO :
                              group == 1 ? n->layer == ECHOY_ENDING :
                              group == 2 ? p->density > .45 && n->layer == ECHOY_SUPPORT && n->kind == ECHOY_RIM :
                              p->density > .72 && (n->layer == ECHOY_DETAIL || (n->layer == ECHOY_SUPPORT && n->kind == ECHOY_HAT));
                if (choose && chosen_count++ < budget) {
                    add_drum(support, &support_count, used, 0, n->step, n->kind,
                             minimum(n->velocity, n->kind == ECHOY_HAT ? .18 : .30), n->layer, n->source_index);
                }
            }
        }
    }
    for (size_t i = 0; i < support_count; ++i) append_drum(p, &support[i]);
}

static void add_slot(support_slot_t slots[MAX_SLOTS], size_t *count, bool occupied[32],
                     double step, echoy_layer_t layer, int source_index)
{
    if (step < 0 || step >= 16 || occupied[(int)(step * 2)] || *count >= MAX_SLOTS) return;
    occupied[(int)(step * 2)] = true;
    slots[(*count)++] = (support_slot_t){.step = step, .layer = layer, .source_index = source_index};
}

static size_t plan_support(const echoy_plan_t *p, support_slot_t slots[MAX_SLOTS])
{
    static const int routes[3][3][10] = {
        {{4, 6, 12, 3, 11, 9, 1, 7, 13, 15}, {5, 11, 3, 13, 7, 9, 1, 15, 4, 12}, {6, 12, 4, 9, 3, 11, 2, 13, 7, 15}},
        {{1, 5, 9, 3, 13, 7, 11, 15, 4, 12}, {4, 11, 7, 13, 6, 10, 1, 15, -1, -1}, {2, 4, 8, 11, 6, 12, 3, 13, 7, 15}},
        {{4, 12, 6, 10, 3, 7, 11, 15, 2, 9}, {3, 6, 10, 13, 4, 7, 11, 15, 2, 5}, {9, 11, 13, 5, 7, 3, 12, 6, 4, 15}},
    };
    bool occupied[32] = {false};
    for (size_t i = 0; i < p->source_count; ++i) occupied[(int)p->source[i].step * 2] = true;
    size_t count = 0;
    double head = p->source[0].step, last = p->source[p->source_count - 1].step;
    int pair = closest_pair(p);
    if (pair >= 0) {
        double gap = p->source[pair + 1].step - p->source[pair].step;
        const int offsets[] = {8, 4, 6, 10};
        for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
            double at = head + offsets[i];
            if (at > p->source[pair + 1].step && at + gap < 15 &&
                !occupied[(int)(at * 2)] && !occupied[(int)((at + gap) * 2)]) {
                add_slot(slots, &count, occupied, at, ECHOY_ECHO, pair);
                add_slot(slots, &count, occupied, at + gap, ECHOY_ECHO, pair + 1);
                break;
            }
        }
    }
    for (int offset = 14; offset >= 2; offset -= 2) {
        double at = head + offset;
        if (at < 16 && at > last && !occupied[(int)(at * 2)]) {
            add_slot(slots, &count, occupied, at, ECHOY_ENDING, (int)p->source_count - 1);
            break;
        }
    }
    for (size_t i = 0; i < 10; ++i) {
        int offset = routes[p->role - ECHOY_VOICE][p->phrase - 1][i];
        if (offset < 0) break;
        double at = head + offset;
        add_slot(slots, &count, occupied, at, ECHOY_SUPPORT, nearest_source(p, at));
    }
    if (p->role == ECHOY_DUST && p->phrase == 1) {
        for (size_t i = 0; i < p->source_count; ++i) {
            add_slot(slots, &count, occupied, p->source[i].step + .5, ECHOY_DETAIL, (int)i);
            add_slot(slots, &count, occupied, p->source[i].step + 1.5, ECHOY_DETAIL, (int)i);
        }
    }
    return count;
}

static int voice_pitch(const echoy_plan_t *p, size_t index)
{
    if (!index) return 62;
    static const int contour[] = {0, 3, 1, 2, 4, 3, 1, 0};
    int at = contour[index % 8];
    if (p->source[index].step - p->source[index - 1].step >= 6 && at > 0) --at;
    return scale[at];
}

static echoy_articulation_t articulation(unsigned phrase)
{
    return phrase == 1 ? ECHOY_SHORT : phrase == 2 ? ECHOY_BROKEN : ECHOY_ANSWER;
}

static void compose_foreground(echoy_plan_t *p)
{
    static const int dust_pitches[] = {81, 86, 84, 89, 86};
    for (size_t i = 0; i < p->source_count; ++i) {
        echoy_note_t *n = &p->events[p->count++];
        n->step = p->source[i].step;
        n->velocity = p->source[i].velocity;
        n->layer = ECHOY_ANCHOR;
        n->has_source_step = true;
        n->source_step = n->step;
        double next = i + 1 < p->source_count ? p->source[i + 1].step : 16;
        double space = (next - n->step) * STEP_DURATION - .025;
        if (p->role == ECHOY_VOICE) {
            n->kind = ECHOY_TONE;
            n->note = voice_pitch(p, i);
            n->duration = minimum(maximum(.05, space), p->phrase == 3 ? .62 : p->phrase == 2 ? .12 : .24);
        } else if (p->role == ECHOY_DUST) {
            n->kind = ECHOY_GRAIN;
            n->note = dust_pitches[i % 5] + (p->phrase == 2 ? 5 : 0);
            n->duration = minimum(maximum(.045, space), p->phrase == 1 ? .085 : p->phrase == 2 ? .14 : .46);
        } else {
            n->kind = ECHOY_CHORD;
            memcpy(n->harmony, voicings[i % 6], sizeof(n->harmony));
            n->harmony_count = 3;
            n->note_count = p->phrase == 1 ? 3 : 1;
            if (p->phrase == 1) memcpy(n->notes, n->harmony, sizeof(n->notes));
            else n->notes[0] = n->harmony[2];
            n->note = n->notes[n->note_count - 1];
            n->articulation = articulation(p->phrase);
            n->duration = minimum(maximum(.05, space), p->phrase == 1 ? .19 : p->phrase == 2 ? .23 : .15);
            n->max_duration = minimum(.27, maximum(.05, space));
        }
    }
}

static void append_tonal(echoy_plan_t *p, const support_slot_t *slot, size_t index, double min_velocity)
{
    const int parent_index = slot->source_index >= 0 ? slot->source_index : 0;
    const echoy_note_t *parent = &p->events[parent_index];
    echoy_note_t *n = &p->events[p->count++];
    bool ending = slot->layer == ECHOY_ENDING;
    n->step = slot->step;
    n->layer = slot->layer;
    n->has_source_step = slot->source_index >= 0;
    if (n->has_source_step) n->source_step = p->source[slot->source_index].step;
    n->velocity = minimum(slot->layer == ECHOY_ECHO ? .48 : ending ? .44 : slot->layer == ECHOY_DETAIL ? .28 : .36,
                          min_velocity * .78);
    double remaining = maximum(.035, BAR_DURATION - slot->step * STEP_DURATION - .018);
    if (p->role == ECHOY_VOICE) {
        int pitch_index = 0;
        for (int i = 0; i < 7; ++i) if (scale[i] == parent->note) { pitch_index = i; break; }
        int note = slot->layer == ECHOY_ECHO ? parent->note : ending ? 62 :
                   scale[(pitch_index + (p->phrase == 2 ? 2 : 1) + index % 2) % 7];
        n->kind = ECHOY_TONE;
        n->note = p->phrase == 3 && !ending ? note - 12 : note;
        n->duration = minimum(remaining, p->phrase == 1 ? .16 : p->phrase == 2 ? .085 : .32);
        if (p->phrase == 3) n->velocity *= .8;
    } else if (p->role == ECHOY_DUST) {
        static const int pitches[] = {86, 93, 89, 96, 84, 91};
        n->kind = ECHOY_GRAIN;
        n->note = p->phrase == 3 ? parent->note : pitches[index % 6] + (p->phrase == 2 ? 5 : 0);
        n->duration = minimum(remaining, p->phrase == 1 ? .055 : p->phrase == 2 ? .08 : .32);
        if (p->phrase == 3) n->velocity = minimum(n->velocity, .48 * exp(-maximum(0, slot->step - parent->step) / 18));
    } else {
        n->kind = ECHOY_CHORD;
        n->harmony_count = 3;
        memcpy(n->harmony, parent->harmony, sizeof(n->harmony));
        if (p->phrase == 1) {
            const int *v = voicings[(parent_index + 1 + index % 2) % 6];
            memcpy(n->harmony, v, sizeof(n->harmony));
            if (ending) { memcpy(n->notes, v, sizeof(n->notes)); n->note_count = 3; }
            else if (index % 2) { n->notes[0] = v[1]; n->notes[1] = v[2]; n->note_count = 2; }
            else { n->notes[0] = v[2]; n->note_count = 1; }
        } else if (p->phrase == 2) {
            const int tones[] = {n->harmony[0] + 12, n->harmony[1], n->harmony[2], n->harmony[1] + 12};
            n->notes[0] = slot->layer == ECHOY_ECHO ? parent->note : tones[index % 4];
            n->note_count = 1;
        } else if (ending) {
            memcpy(n->harmony, voicings[0], sizeof(n->harmony));
            memcpy(n->notes, voicings[0], sizeof(n->notes));
            n->note_count = 3;
        } else {
            n->notes[0] = slot->layer == ECHOY_ECHO ? parent->note : n->harmony[(index + 1) % 3] + (index % 2 == 0 ? 12 : 0);
            n->note_count = 1;
        }
        n->note = n->notes[n->note_count - 1];
        n->articulation = articulation(p->phrase);
        n->duration = minimum(remaining, p->phrase == 1 ? .12 : p->phrase == 2 ? .20 : ending ? .24 : .11);
        n->max_duration = minimum(remaining, .30);
    }
}

static void compose_tonal(echoy_plan_t *p)
{
    compose_foreground(p);
    support_slot_t slots[MAX_SLOTS];
    size_t count = plan_support(p, slots);
    double min_velocity = p->source[0].velocity;
    for (size_t i = 1; i < p->source_count; ++i) min_velocity = minimum(min_velocity, p->source[i].velocity);
    unsigned base = 4, factor = 6;
    if (p->role == ECHOY_VOICE) { base = p->phrase == 1 ? 4 : 3; factor = p->phrase == 1 ? 5 : 4; }
    else if (p->role == ECHOY_DUST) { base = p->phrase == 2 ? 3 : 4; factor = p->phrase == 1 ? 8 : p->phrase == 2 ? 4 : 5; }
    size_t budget = (size_t)minimum(32 - p->source_count, base + js_round(p->density * factor));
    size_t chosen = count < budget ? count : budget;
    if (p->role == ECHOY_DUST && p->phrase == 1 && p->density > .7) {
        size_t fine_indices[3], fine_count = 0;
        size_t fine_budget = (size_t)maximum(1, floor(p->density * 3));
        for (size_t i = 0; i < count && fine_count < fine_budget; ++i) {
            if (slots[i].layer == ECHOY_DETAIL) fine_indices[fine_count++] = i;
        }
        size_t prefix = chosen < budget - fine_count ? chosen : budget - fine_count;
        for (size_t i = 0; i < prefix; ++i) append_tonal(p, &slots[i], i, min_velocity);
        for (size_t i = 0; i < fine_count; ++i) append_tonal(p, &slots[fine_indices[i]], prefix + i, min_velocity);
    } else {
        for (size_t i = 0; i < chosen; ++i) append_tonal(p, &slots[i], i, min_velocity);
    }
}

bool echoy_compose(const echoy_seed_note_t *source, size_t count, echoy_role_t role,
                  unsigned phrase, double density, echoy_plan_t *out)
{
    if (!out || (!source && count) || role < ECHOY_PULSE || role > ECHOY_HARMONY || phrase < 1 || phrase > 3) return false;
    echoy_seed_note_t clean[16] = {0};
    size_t source_count = clean_source(source, count, clean);
    memset(out, 0, sizeof(*out));
    out->role = role;
    out->phrase = phrase;
    out->density = clamp(density, 0, 1);
    out->duration = BAR_DURATION;
    out->source_count = source_count;
    memcpy(out->source, clean, source_count * sizeof(clean[0]));
    if (!source_count) return true;
    if (role == ECHOY_PULSE) compose_pulse(out);
    else compose_tonal(out);
    sort_events(out);
    return true;
}

bool echoy_compose_mask(uint16_t seed, echoy_role_t role, unsigned phrase,
                       double density, echoy_plan_t *out)
{
    echoy_seed_note_t source[16] = {0};
    size_t count = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (seed & (1u << i)) source[count++] = (echoy_seed_note_t){.step = i, .velocity = .74, .kind = ECHOY_KICK};
    }
    return echoy_compose(source, count, role, phrase, density, out);
}
