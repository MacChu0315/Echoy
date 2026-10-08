#include "echoy_arrange.h"
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static echoy_arrange_job_t jobs[4];
static bool job_valid[4];

static void print_seed(const echoy_seed_note_t *notes, size_t count)
{
    putchar('[');
    for (size_t i = 0; i < count; ++i) printf("%s{\"step\":%.17g,\"velocity\":%.17g,\"kind\":%d}",
        i ? "," : "", notes[i].step, notes[i].velocity, notes[i].kind);
    putchar(']');
}

static void print_ints(const int *notes, unsigned count)
{
    assert(count <= 3); putchar('[');
    for (unsigned i = 0; i < count; ++i) printf("%s%d", i ? "," : "", notes[i]);
    putchar(']');
}

static void print_note(const echoy_note_t *n)
{
    printf("{\"step\":%.17g,\"velocity\":%.17g,\"duration\":%.17g,\"max_duration\":%.17g,\"source_step\":",
        n->step, n->velocity, n->duration, n->max_duration);
    if (n->has_source_step) printf("%.17g", n->source_step); else printf("null");
    printf(",\"expression\":%.17g,\"kind\":%d,\"layer\":%d,\"articulation\":%d,\"note\":%d,\"notes\":",
        n->expression, n->kind, n->layer, n->articulation, n->note);
    print_ints(n->notes, n->note_count); printf(",\"harmony\":"); print_ints(n->harmony, n->harmony_count); putchar('}');
}

static void print_music(const echoy_plan_t *p)
{
    assert(p->count <= ECHOY_MAX_EVENTS && p->source_count <= 16);
    printf("{\"role\":%d,\"phrase\":%u,\"density\":%.17g,\"duration\":%.17g,\"source\":",
        p->role, p->phrase, p->density, p->duration);
    print_seed(p->source, p->source_count); printf(",\"events\":[");
    for (size_t i = 0; i < p->count; ++i) { if (i) putchar(','); print_note(&p->events[i]); }
    printf("],\"foreground\":[");
    unsigned anchors = 0;
    for (size_t i = 0; i < p->count; ++i) if (p->events[i].layer == ECHOY_ANCHOR) {
        if (anchors++) putchar(',');
        print_note(&p->events[i]);
    }
    printf("]}");
}

static void print_plan(const echoy_arrange_plan_t *p)
{
    if (!p->valid) { printf("null"); return; }
    printf("{\"source\":%d,\"detached\":", p->source);
    if (p->has_detached) printf("%s", p->detached ? "true" : "false"); else printf("null");
    printf(",\"music\":"); print_music(&p->music); putchar('}');
}

static void print_edges(const echoy_arrange_edge_t *edges, size_t count)
{
    assert(count <= 2); putchar('[');
    for (size_t i = 0; i < count; ++i) printf("%s[%d,%d]", i ? "," : "", edges[i].left, edges[i].right);
    putchar(']');
}

static void print_job(const echoy_arrange_job_t *job)
{
    if (!job) { printf("null"); return; }
    printf("{\"revision\":%" PRIu64 ",\"changed_mask\":%u,\"edges\":", job->revision, job->changed_mask);
    print_edges(job->edges, job->edge_count);
    printf(",\"clocks\":[%d,%d,%d],\"plans\":[", job->clock_id[0], job->clock_id[1], job->clock_id[2]);
    for (int i = 0; i < 3; ++i) { if (i) putchar(','); print_plan(&job->plans[i]); }
    printf("]}");
}

static void print_state(const echoy_arrange_t *model)
{
    printf("{\"revision\":%" PRIu64 ",\"edges\":", echoy_arrange_revision(model));
    size_t count; const echoy_arrange_edge_t *edges = echoy_arrange_get_edges(model, false, &count);
    print_edges(edges, count); printf(",\"wanted\":");
    edges = echoy_arrange_get_edges(model, true, &count); print_edges(edges, count);
    printf(",\"pending\":");
    const echoy_arrange_pending_t *pending = echoy_arrange_get_pending(model);
    if (!pending) printf("null");
    else { printf("{\"at_us\":%" PRId64 ",\"job\":", pending->at_us); print_job(&pending->job); putchar('}'); }
    printf(",\"clocks\":[");
    for (int i = 0; i < 3; ++i) {
        const echoy_arrange_clock_t *clock = echoy_arrange_get_clock(model, (echoy_tile_id_t)i);
        printf("%s{\"origin_us\":%" PRId64 ",\"started\":%s}", i ? "," : "", clock->origin_us, clock->started ? "true" : "false");
    }
    printf("],\"tiles\":[");
    for (int i = 0; i < 3; ++i) {
        const echoy_arrange_tile_t *d = echoy_arrange_get_tile(model, (echoy_tile_id_t)i);
        printf("%s{\"id\":%d,\"clock_id\":%d,\"role\":%d,\"phrase\":%u,\"density\":%.17g,\"present\":%s,\"loop\":%s,\"use_own\":%s,\"memories\":[%u,%u,%u,%u],\"own_seed\":",
            i ? "," : "", d->id, d->clock_id, d->role, d->phrase, d->density,
            d->present ? "true" : "false", d->loop ? "true" : "false", d->use_own ? "true" : "false",
            d->memories[0], d->memories[1], d->memories[2], d->memories[3]);
        print_seed(d->own_seed, d->own_count); printf(",\"fallback\":"); print_seed(d->fallback, d->fallback_count);
        printf(",\"current\":"); print_plan(&d->current); putchar('}');
    }
    printf("]}");
}

static unsigned read_seed(echoy_seed_note_t notes[256])
{
    unsigned count; assert(scanf("%u", &count) == 1 && count <= 256);
    for (unsigned i = 0; i < count; ++i) {
        int kind; assert(scanf("%lf %lf %d", &notes[i].step, &notes[i].velocity, &kind) == 3);
        notes[i].kind = (echoy_note_kind_t)kind;
    }
    return count;
}

static void boundaries(void)
{
    echoy_arrange_t *model = echoy_arrange_create(); assert(model);
    echoy_arrange_job_t *job = calloc(1, sizeof(*job)); assert(job);
    echoy_seed_note_t seed = {0, .74, ECHOY_KICK};
    assert(!echoy_arrange_set_seed(NULL, ECHOY_TILE_A, &seed, 1));
    assert(!echoy_arrange_set_seed(model, ECHOY_TILE_NONE, &seed, 1));
    assert(!echoy_arrange_set_seed(model, ECHOY_TILE_A, NULL, 1));
    assert(!echoy_arrange_set_edges(model, NULL, 1));
    assert(!echoy_arrange_compile(NULL, job)); assert(!echoy_arrange_compile(model, NULL));
    assert(!echoy_arrange_stage(model, NULL, 0)); assert(!echoy_arrange_mute(model, (echoy_tile_id_t)3));
    assert(echoy_arrange_get_tile(model, ECHOY_TILE_NONE) == NULL);
    assert(echoy_arrange_get_clock(NULL, ECHOY_TILE_A) == NULL);
    assert(echoy_arrange_set_seed(model, ECHOY_TILE_A, &seed, 1));
    assert(echoy_arrange_compile(model, job));
    assert(!echoy_arrange_stage(model, job, INT64_MAX));
    assert(echoy_arrange_stage(model, job, 0));
    assert(!echoy_arrange_commit(model, 44999, NULL));
    assert(echoy_arrange_tick(model, 45000, NULL));
    int64_t start;
    assert(!echoy_arrange_enable_loop(model, ECHOY_TILE_A, 0, NULL));
    assert(!echoy_arrange_enable_loop(model, ECHOY_TILE_A, INT64_MAX, &start));
    assert(echoy_arrange_enable_loop(model, ECHOY_TILE_A, 100000, &start) && start == 150000);
    assert(echoy_arrange_compile(model, job));
    job->clock_id[0] = (echoy_tile_id_t)4; assert(!echoy_arrange_stage(model, job, 0));
    assert(echoy_arrange_compile(model, job));
    job->plans[0].music.count = ECHOY_MAX_EVENTS + 1; assert(!echoy_arrange_stage(model, job, 0));
    assert(!echoy_arrange_linked(NULL, &job->plans[0].music, &job->plans[1].music));
    assert(!echoy_arrange_linked(&job->plans[0].music, &job->plans[1].music, &job->plans[1].music));
    free(job); echoy_arrange_delete(model); echoy_arrange_delete(NULL);
}

int main(void)
{
    boundaries();
    echoy_arrange_t *model = echoy_arrange_create(); assert(model);
    char op;
    while (scanf(" %c", &op) == 1) {
        bool ok = true, has_value = false, has_commit = false;
        double value = 0;
        echoy_arrange_commit_t committed = {0};
        const echoy_arrange_job_t *printed_job = NULL;
        int tile, slot; int64_t now;
        if (op == 'R') {
            echoy_arrange_delete(model); model = echoy_arrange_create(); assert(model);
            memset(job_valid, 0, sizeof(job_valid));
        } else if (op == 'S') {
            echoy_seed_note_t notes[256] = {0}; assert(scanf("%d", &tile) == 1);
            unsigned count = read_seed(notes); ok = echoy_arrange_set_seed(model, (echoy_tile_id_t)tile, notes, count);
        } else if (op == 'C') {
            unsigned flags; int role; echoy_arrange_patch_t patch = {0};
            assert(scanf("%d %u %d %u %lf", &tile, &flags, &role, &patch.phrase, &patch.density) == 5);
            patch.set_role = (flags & 1) != 0; patch.set_phrase = (flags & 2) != 0; patch.set_density = (flags & 4) != 0;
            patch.role = (echoy_role_t)role; ok = echoy_arrange_configure(model, (echoy_tile_id_t)tile, &patch);
        } else if (op == 'P') {
            int present; assert(scanf("%d %d", &tile, &present) == 2);
            ok = echoy_arrange_set_present(model, (echoy_tile_id_t)tile, present != 0);
        } else if (op == 'E') {
            unsigned count; echoy_arrange_edge_t edges[8]; assert(scanf("%u", &count) == 1 && count <= 8);
            for (unsigned i = 0; i < count; ++i) {
                int left, right; assert(scanf("%d %d", &left, &right) == 2);
                edges[i] = (echoy_arrange_edge_t){(echoy_tile_id_t)left, (echoy_tile_id_t)right};
            }
            ok = echoy_arrange_set_edges(model, edges, count);
        } else if (op == 'J') {
            assert(scanf("%d", &slot) == 1 && slot >= 0 && slot < 4);
            ok = job_valid[slot] = echoy_arrange_compile(model, &jobs[slot]); if (ok) printed_job = &jobs[slot];
        } else if (op == 'T') {
            assert(scanf("%d %" SCNd64, &slot, &now) == 2 && slot >= 0 && slot < 4);
            ok = job_valid[slot] && echoy_arrange_stage(model, &jobs[slot], now);
        } else if (op == 'K') {
            assert(scanf("%" SCNd64, &now) == 1);
            ok = has_commit = echoy_arrange_commit(model, now, &committed);
        } else if (op == 'L') {
            int64_t start; assert(scanf("%d %" SCNd64, &tile, &now) == 2);
            ok = echoy_arrange_enable_loop(model, (echoy_tile_id_t)tile, now, &start);
            if (ok) { value = (double)start; has_value = true; }
        } else if (op == 'M') {
            assert(scanf("%d", &tile) == 1); ok = echoy_arrange_mute(model, (echoy_tile_id_t)tile);
        } else if (op == 'H') {
            assert(scanf("%d %" SCNd64, &tile, &now) == 2);
            value = echoy_arrange_phase(model, (echoy_tile_id_t)tile, now); has_value = true;
        } else if (op == 'X') {
            echoy_arrange_invalidate(model);
        } else if (op == 'D') {
            int role, up_role; unsigned phrase, up_phrase; double density, up_density;
            assert(scanf("%d %u %lf %d %u %lf", &role, &phrase, &density, &up_role, &up_phrase, &up_density) == 6);
            echoy_seed_note_t notes[256] = {0}; unsigned count = read_seed(notes);
            echoy_plan_t *local = &jobs[0].plans[0].music, *upstream = &jobs[0].plans[1].music;
            assert(echoy_compose(notes, count, (echoy_role_t)role, phrase, density, local));
            assert(echoy_compose(notes, count, (echoy_role_t)up_role, up_phrase, up_density, upstream));
            assert(echoy_arrange_linked(local, upstream, local));
            print_music(local); putchar('\n'); fflush(stdout); continue;
        } else assert(!"unknown command");
        printf("{\"ok\":%s,\"value\":", ok ? "true" : "false");
        if (has_value) printf("%.17g", value); else printf("null");
        printf(",\"commit\":");
        if (has_commit) printf("{\"revision\":%" PRIu64 ",\"changed_mask\":%u,\"at_us\":%" PRId64 "}",
                              committed.revision, committed.changed_mask, committed.at_us);
        else printf("null");
        printf(",\"job\":"); print_job(printed_job); printf(",\"state\":"); print_state(model); puts("}"); fflush(stdout);
    }
    assert(feof(stdin)); echoy_arrange_delete(model); return 0;
}
