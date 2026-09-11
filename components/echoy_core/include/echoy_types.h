// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_TYPES_H
#define ECHOY_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#define ECHOY_MAX_EVENTS 16
#define ECHOY_MAX_PITCHES 3

typedef enum { ECHOY_PULSE, ECHOY_VOICE, ECHOY_DUST, ECHOY_HARMONY } echoy_role_t;
typedef enum { ECHOY_KICK, ECHOY_RIM, ECHOY_HAT, ECHOY_TONE, ECHOY_GRAIN, ECHOY_CHORD } echoy_kind_t;
typedef enum { ECHOY_ART_NONE, ECHOY_ART_SHORT, ECHOY_ART_BROKEN, ECHOY_ART_ANSWER } echoy_articulation_t;
typedef struct {
    double step, vel, note, dur, expression;
    bool accent;
    echoy_kind_t kind;
    double notes[ECHOY_MAX_PITCHES], harmony[ECHOY_MAX_PITCHES];
    size_t notes_count, harmony_count;
    bool has_notes, has_harmony;
    int chord; /* Index into Dm, F, C, Gm, Bb, Am; -1 if absent. */
    echoy_articulation_t articulation;
    double max_duration;
} echoy_event_t;
typedef struct { echoy_event_t events[ECHOY_MAX_EVENTS]; size_t count; } echoy_motif_t;
typedef struct {
    echoy_role_t role;
    int phrase, bar, duplicate;
    double activity, bpm;
    const echoy_motif_t *left; /* Complete direct-left motif; no output volume. */
} echoy_music_input_t;
#endif
