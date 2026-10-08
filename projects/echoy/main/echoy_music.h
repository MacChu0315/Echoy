#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ECHOY_MAX_EVENTS 64
typedef enum { ECHOY_PULSE, ECHOY_VOICE, ECHOY_DUST, ECHOY_HARMONY } echoy_role_t;
typedef enum { ECHOY_KICK, ECHOY_RIM, ECHOY_HAT, ECHOY_TONE, ECHOY_GRAIN, ECHOY_CHORD } echoy_note_kind_t;
typedef enum { ECHOY_ANCHOR, ECHOY_ECHO, ECHOY_ENDING, ECHOY_SUPPORT, ECHOY_DETAIL } echoy_layer_t;
typedef enum { ECHOY_ARTICULATION_NONE, ECHOY_SHORT, ECHOY_BROKEN, ECHOY_ANSWER } echoy_articulation_t;
typedef struct {
    double step, velocity;
    echoy_note_kind_t kind;
} echoy_seed_note_t;
typedef struct {
    double step, velocity, duration, max_duration, source_step, expression;
    echoy_note_kind_t kind;
    echoy_layer_t layer;
    echoy_articulation_t articulation;
    int note, notes[3], harmony[3];
    unsigned note_count, harmony_count;
    bool has_source_step;
} echoy_note_t;
typedef struct {
    echoy_role_t role;
    unsigned phrase;
    double density, duration;
    size_t source_count, count;
    echoy_seed_note_t source[16];
    echoy_note_t events[ECHOY_MAX_EVENTS];
} echoy_plan_t;

/* Pure, bounded port of RoleAccompaniment.compose in the frozen HTML. */
bool echoy_compose(const echoy_seed_note_t *source, size_t count, echoy_role_t role,
                  unsigned phrase, double density, echoy_plan_t *out);
bool echoy_compose_mask(uint16_t seed, echoy_role_t role, unsigned phrase,
                       double density, echoy_plan_t *out);
