#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "echoy_music.h"

#define ECHOY_ARRANGE_TILE_COUNT 3
#define ECHOY_ARRANGE_MAX_EDGES 2
#define ECHOY_ARRANGE_BAR_US INT64_C(2500000)

typedef enum {
    ECHOY_TILE_NONE = -1, ECHOY_TILE_A, ECHOY_TILE_B, ECHOY_TILE_C
} echoy_tile_id_t;
typedef struct { echoy_tile_id_t left, right; } echoy_arrange_edge_t;

/* music.source is the original, cleaned root seed. music.events contains the
 * complete structured linked phrase; its anchor events are the foreground.
 * source names the immediate upstream tile, never a seed mask. */
typedef struct {
    bool valid, detached, has_detached;
    echoy_tile_id_t source;
    echoy_plan_t music;
} echoy_arrange_plan_t;

typedef struct {
    echoy_tile_id_t id, clock_id;
    echoy_role_t role;
    unsigned phrase, memories[4];
    double density;
    bool present, loop, use_own;
    size_t own_count, fallback_count;
    echoy_seed_note_t own_seed[16], fallback[16];
    echoy_arrange_plan_t current;
} echoy_arrange_tile_t;

typedef struct { int64_t origin_us; bool started; } echoy_arrange_clock_t;
typedef struct {
    bool set_role, set_phrase, set_density;
    echoy_role_t role;
    unsigned phrase;
    double density;
} echoy_arrange_patch_t;

/* A job owns all of its data: it may outlive the mutex held during compile.
 * Allocate large jobs on the heap or in long-lived context, not a small task
 * stack. changed_mask describes the compile snapshot; stage recomputes it. */
typedef struct {
    uint64_t revision;
    uint8_t changed_mask;
    size_t edge_count;
    echoy_arrange_edge_t edges[ECHOY_ARRANGE_MAX_EDGES];
    echoy_tile_id_t clock_id[ECHOY_ARRANGE_TILE_COUNT];
    echoy_arrange_plan_t plans[ECHOY_ARRANGE_TILE_COUNT];
} echoy_arrange_job_t;

typedef struct { echoy_arrange_job_t job; int64_t at_us; } echoy_arrange_pending_t;
typedef struct {
    uint64_t revision;
    uint8_t changed_mask;
    int64_t at_us;
} echoy_arrange_commit_t;
typedef struct echoy_arrange echoy_arrange_t;

/* No platform, audio, wall-clock, threading or network dependencies. The
 * caller supplies one monotonic logical timeline and external synchronization.
 * Getters return borrowed pointers, invalidated by a subsequent mutation. */
echoy_arrange_t *echoy_arrange_create(void);
void echoy_arrange_delete(echoy_arrange_t *model);
uint64_t echoy_arrange_revision(const echoy_arrange_t *model);
const echoy_arrange_tile_t *echoy_arrange_get_tile(const echoy_arrange_t *model,
                                                echoy_tile_id_t tile);
const echoy_arrange_clock_t *echoy_arrange_get_clock(const echoy_arrange_t *model,
                                                   echoy_tile_id_t clock);
const echoy_arrange_pending_t *echoy_arrange_get_pending(const echoy_arrange_t *model);
const echoy_arrange_edge_t *echoy_arrange_get_edges(const echoy_arrange_t *model,
                                                  bool wanted, size_t *count);

bool echoy_arrange_set_seed(echoy_arrange_t *model, echoy_tile_id_t tile,
                            const echoy_seed_note_t *notes, size_t count);
/* Device adapter: an inaudible upstream replica supplies an already validated
 * complete phrase. The portable compiler still produces the local accompaniment. */
bool echoy_arrange_import(echoy_arrange_t *model, echoy_tile_id_t tile,
                          const echoy_plan_t *plan);
bool echoy_arrange_clock_sync(echoy_arrange_t *model, echoy_tile_id_t clock,
                              int64_t origin_us, bool started, int64_t now_us);
bool echoy_arrange_peer_loop(echoy_arrange_t *model, echoy_tile_id_t tile, bool playing);
/* Patches follow the prototype's order: role/memory first, then phrase, then
 * density. An invalid phrase after a valid role leaves that role change in
 * place, without advancing revision, just as the original JS exception does.
 * Nonfinite density selects .5; finite density clamps to [0,1]. */
bool echoy_arrange_configure(echoy_arrange_t *model, echoy_tile_id_t tile,
                             const echoy_arrange_patch_t *patch);
bool echoy_arrange_set_present(echoy_arrange_t *model, echoy_tile_id_t tile, bool present);
/* Validate the entire ordered chain before filtering absent endpoints. */
bool echoy_arrange_set_edges(echoy_arrange_t *model,
                             const echoy_arrange_edge_t *edges, size_t count);
/* Release a removed incoming edge immediately, retaining music, loop and phase.
 * Remaining descendants keep this tile's new independent clock. */
bool echoy_arrange_release(echoy_arrange_t *model, echoy_tile_id_t tile);
bool echoy_arrange_edges_valid(const echoy_arrange_edge_t *edges, size_t count);
/* Equivalent to ArrangeModel.change(): cancel pending and reject old jobs. */
uint64_t echoy_arrange_invalidate(echoy_arrange_t *model);

bool echoy_arrange_compile(const echoy_arrange_t *model, echoy_arrange_job_t *job);
bool echoy_arrange_stage(echoy_arrange_t *model, const echoy_arrange_job_t *job,
                         int64_t now_us);
/* A negotiated wireless boundary is fixed once armed, not retimed by a later
 * clock estimate. Cancellation does not change the requested musical revision. */
bool echoy_arrange_stage_at(echoy_arrange_t *model, const echoy_arrange_job_t *job, int64_t at_us);
void echoy_arrange_unstage(echoy_arrange_t *model);
/* True includes a due edge-only commit with changed_mask == 0. */
bool echoy_arrange_commit(echoy_arrange_t *model, int64_t now_us,
                          echoy_arrange_commit_t *result);
bool echoy_arrange_tick(echoy_arrange_t *model, int64_t now_us,
                        echoy_arrange_commit_t *result);
bool echoy_arrange_enable_loop(echoy_arrange_t *model, echoy_tile_id_t tile,
                               int64_t now_us, int64_t *start_us);
bool echoy_arrange_enable_loop_shared(echoy_arrange_t *model, echoy_tile_id_t tile,
                                      int64_t now_us, bool peer_playing, int64_t *start_us);
bool echoy_arrange_mute(echoy_arrange_t *model, echoy_tile_id_t tile);
double echoy_arrange_phase(const echoy_arrange_t *model, echoy_tile_id_t tile,
                           int64_t now_us);

/* Pure ArrangeLinked port. out may alias local, but not upstream. */
bool echoy_arrange_linked(const echoy_plan_t *local, const echoy_plan_t *upstream,
                          echoy_plan_t *out);
