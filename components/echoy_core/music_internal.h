// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_MUSIC_INTERNAL_H
#define ECHOY_MUSIC_INTERNAL_H
#include "echoy_music.h"
#include <math.h>
#include <string.h>
static inline double music_clamp(double x, double lo, double hi) { return fmin(hi, fmax(lo, isfinite(x) ? x : lo)); }
static inline double music_round(double x) { return floor(x + .5); }
static inline double music_wrap(double x, double modulus) { return fmod(fmod(x, modulus) + modulus, modulus); }
static inline int wrap16(int x) { return (x % 16 + 16) % 16; }
static inline double music_distance(double a, double b) { return fmin(music_wrap(a-b,16), music_wrap(b-a,16)); }
static inline double role_initial(echoy_role_t r) { const double a[]={.58,.56,.48,.50}; return a[r]; }
static inline echoy_event_t music_event(int step, echoy_kind_t kind, double vel, double note, double dur, bool accent) {
    echoy_event_t e = {0}; e.step=step; e.kind=kind; e.vel=vel; e.note=note; e.dur=dur; e.accent=accent; e.chord=-1; return e;
}
static inline void sort_steps(int *a, size_t n) { for(size_t i=1;i<n;i++){ int x=a[i]; size_t j=i; while(j && a[j-1]>x){a[j]=a[j-1];j--;} a[j]=x;} }
static inline void sort_events(echoy_motif_t *m) { for(size_t i=1;i<m->count;i++){ echoy_event_t e=m->events[i]; size_t j=i; while(j && m->events[j-1].step>e.step){m->events[j]=m->events[j-1];j--;} m->events[j]=e;} }
typedef struct { echoy_event_t event; double span; } analyzed_event_t;
typedef struct {
    analyzed_event_t events[ECHOY_MAX_EVENTS]; size_t count;
    double onset[16], release[16], held[16], space[16];
    int first; double mean_velocity, mean_span;
} music_analysis_t;
bool music_analyze(const echoy_motif_t *m, double bpm, music_analysis_t *q);
const analyzed_event_t *music_preceding(const music_analysis_t *q, int step, bool pitched);
bool music_pitched(const echoy_event_t *e);
void music_response(const echoy_music_input_t *in, echoy_motif_t *out);
void music_harmony(const echoy_music_input_t *in, echoy_motif_t *out);
#endif
