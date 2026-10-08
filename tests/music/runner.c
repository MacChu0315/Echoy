#include "echoy_music.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void boundaries(void)
{
    echoy_plan_t plan;
    const echoy_seed_note_t seed = {.step = 0, .velocity = .74, .kind = ECHOY_KICK};
    assert(!echoy_compose(&seed, 1, ECHOY_PULSE, 1, .5, NULL));
    assert(!echoy_compose(NULL, 1, ECHOY_PULSE, 1, .5, &plan));
    assert(!echoy_compose(&seed, 1, (echoy_role_t)-1, 1, .5, &plan));
    assert(!echoy_compose(&seed, 1, (echoy_role_t)4, 1, .5, &plan));
    assert(!echoy_compose(&seed, 1, ECHOY_PULSE, 0, .5, &plan));
    assert(!echoy_compose(&seed, 1, ECHOY_PULSE, 4, .5, &plan));
    assert(!echoy_compose_mask(1, ECHOY_PULSE, 1, .5, NULL));
    assert(echoy_compose(NULL, 0, ECHOY_PULSE, 1, .5, &plan));
    assert(plan.count == 0 && plan.source_count == 0);
}

static void print_ints(const int *values, unsigned count)
{
    assert(count <= 3);
    putchar('[');
    for (unsigned i = 0; i < count; ++i) printf("%s%d", i ? "," : "", values[i]);
    putchar(']');
}

static void print_plan(const echoy_plan_t *p)
{
    assert(p->count <= ECHOY_MAX_EVENTS && p->source_count <= 16);
    printf("{\"role\":%d,\"phrase\":%u,\"density\":%.17g,\"duration\":%.17g,"
           "\"source_count\":%zu,\"count\":%zu,\"source\":[",
           p->role, p->phrase, p->density, p->duration, p->source_count, p->count);
    for (size_t i = 0; i < p->source_count; ++i) {
        const echoy_seed_note_t *s = &p->source[i];
        printf("%s{\"step\":%.17g,\"velocity\":%.17g,\"kind\":%d}",
               i ? "," : "", s->step, s->velocity, s->kind);
    }
    printf("],\"events\":[");
    for (size_t i = 0; i < p->count; ++i) {
        const echoy_note_t *n = &p->events[i];
        printf("%s{\"step\":%.17g,\"velocity\":%.17g,\"duration\":%.17g,"
               "\"max_duration\":%.17g,\"source_step\":", i ? "," : "",
               n->step, n->velocity, n->duration, n->max_duration);
        if (n->has_source_step) printf("%.17g", n->source_step);
        else printf("null");
        printf(",\"expression\":%.17g,\"kind\":%d,\"layer\":%d,\"articulation\":%d,\"note\":%d,\"notes\":",
               n->expression, n->kind, n->layer, n->articulation, n->note);
        print_ints(n->notes, n->note_count);
        printf(",\"harmony\":");
        print_ints(n->harmony, n->harmony_count);
        putchar('}');
    }
    puts("]}");
}

int main(void)
{
    boundaries();
    int role, mode;
    unsigned phrase, argument;
    double density;
    while (scanf("%d %u %lf %d %u", &role, &phrase, &density, &mode, &argument) == 5) {
        echoy_seed_note_t source[256] = {0};
        if (!mode) {
            assert(argument <= 256);
            for (unsigned i = 0; i < argument; ++i) {
                int kind;
                assert(scanf("%lf %lf %d", &source[i].step, &source[i].velocity, &kind) == 3);
                source[i].kind = (echoy_note_kind_t)kind;
            }
        }
        struct {
            unsigned char before[16];
            echoy_plan_t plan;
            unsigned char after[16];
        } guarded;
        memset(&guarded, 0xa5, sizeof(guarded));
        bool ok = mode ? echoy_compose_mask((uint16_t)argument, (echoy_role_t)role, phrase, density, &guarded.plan)
                       : echoy_compose(source, argument, (echoy_role_t)role, phrase, density, &guarded.plan);
        assert(ok);
        for (size_t i = 0; i < sizeof(guarded.before); ++i) {
            assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
        }
        echoy_plan_t repeat;
        assert(echoy_compose(guarded.plan.source, guarded.plan.source_count,
                            (echoy_role_t)role, phrase, density, &repeat));
        assert(memcmp(&repeat, &guarded.plan, sizeof(repeat)) == 0);
        print_plan(&guarded.plan);
    }
    assert(feof(stdin));
    return 0;
}
