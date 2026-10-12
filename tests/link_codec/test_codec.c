#include "echoy_link_codec.h"
#include "echoy_arrange.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned round_trips, rejections;
static echoy_plan_t plan, decoded, local, upstream, linked, saved;
static uint8_t bytes[ECHOY_LINK_PLAN_MAX_BYTES + 1];

static void same_double(double a, double b)
{
    /* A float conversion, changed rounding or lost negative zero must fail. */
    assert(memcmp(&a, &b, sizeof(a)) == 0);
}

static void same_plan(const echoy_plan_t *a, const echoy_plan_t *b)
{
    assert(a->role == b->role && a->phrase == b->phrase);
    assert(a->source_count == b->source_count && a->count == b->count);
    same_double(a->density, b->density); same_double(a->duration, b->duration);
    for (size_t i = 0; i < a->source_count; ++i) {
        same_double(a->source[i].step, b->source[i].step);
        same_double(a->source[i].velocity, b->source[i].velocity);
        assert(a->source[i].kind == b->source[i].kind);
    }
    for (size_t i = 0; i < a->count; ++i) {
        const echoy_note_t *x = &a->events[i], *y = &b->events[i];
        same_double(x->step, y->step); same_double(x->velocity, y->velocity);
        same_double(x->duration, y->duration); same_double(x->max_duration, y->max_duration);
        same_double(x->source_step, y->source_step); same_double(x->expression, y->expression);
        assert(x->kind == y->kind && x->layer == y->layer && x->articulation == y->articulation);
        assert(x->note == y->note && x->note_count == y->note_count && x->harmony_count == y->harmony_count);
        assert(x->has_source_step == y->has_source_step);
        for (unsigned j = 0; j < 3; ++j) {
            assert(x->notes[j] == y->notes[j]);
            assert(x->harmony[j] == y->harmony[j]);
        }
    }
}

static size_t round_trip(const echoy_plan_t *input)
{
    size_t length = echoy_link_plan_encode(input, bytes, ECHOY_LINK_PLAN_MAX_BYTES);
    assert(length > 0 && length <= ECHOY_LINK_PLAN_MAX_BYTES);
    memset(&decoded, 0xa5, sizeof(decoded));
    assert(echoy_link_plan_decode(bytes, length, &decoded));
    same_plan(input, &decoded);
    ++round_trips;
    return length;
}

/* Independently specified ECHP v1 fixture: one source and one chord. This
 * catches ABI/padding dumps, endian errors and encoder/decoder agreeing on
 * the same incorrect format. Offsets are part of the versioned wire contract. */
static const uint8_t golden[110] = {
    [0] = 'E', [1] = 'C', [2] = 'H', [3] = 'P', [4] = 1, [6] = 32,
    [8] = 110, [12] = 3, [13] = 2, [14] = 1, [15] = 1,
    [22] = 0xe0, [23] = 0x3f, [30] = 0x04, [31] = 0x40,
    [38] = 0x10, [39] = 0x40, [46] = 0xe0, [47] = 0x3f, [48] = 1,
    [55] = 0x10, [56] = 0x40, [63] = 0xe0, [64] = 0x3f,
    [71] = 0xd0, [72] = 0x3f, [79] = 0xe0, [80] = 0x3f,
    [87] = 0x10, [88] = 0x40, [95] = 0xe8, [96] = 0x3f,
    [97] = 5, [98] = 1, [99] = 2, [100] = 65, [101] = 2,
    [102] = 3, [103] = 1, [104] = 60, [105] = 65,
    [107] = 50, [108] = 57, [109] = 65,
};

static void fixture(void)
{
    memset(&plan, 0, sizeof(plan));
    plan.role = ECHOY_HARMONY; plan.phrase = 2;
    plan.density = .5; plan.duration = 2.5; plan.source_count = plan.count = 1;
    plan.source[0] = (echoy_seed_note_t){4, .5, ECHOY_RIM};
    plan.events[0] = (echoy_note_t){.step = 4, .velocity = .5,
        .duration = .25, .max_duration = .5, .source_step = 4, .expression = .75,
        .kind = ECHOY_CHORD, .layer = ECHOY_ECHO, .articulation = ECHOY_BROKEN,
        .note = 65, .notes = {60, 65, 0}, .harmony = {50, 57, 65},
        .note_count = 2, .harmony_count = 3, .has_source_step = true};
}

static void test_known_wire_and_crc(void)
{
    assert(echoy_link_crc32((const uint8_t *)"123456789", 9) == UINT32_C(0xcbf43926));
    assert(echoy_link_crc32(NULL, 0) == 0);
    fixture();
    assert(echoy_link_plan_decode(golden, sizeof(golden), &decoded));
    same_plan(&plan, &decoded);
    assert(round_trip(&plan) == sizeof(golden));
    assert(memcmp(bytes, golden, sizeof(golden)) == 0);
    memcpy(&decoded, golden, sizeof(golden));
    assert(echoy_link_plan_decode((const uint8_t *)&decoded, sizeof(golden), &decoded));
    same_plan(&plan, &decoded);
    /* Inactive fields remain lossless, and zero duration limits are legal. */
    plan.events[0].max_duration = 0;
    plan.events[0].expression = -0.0;
    plan.events[0].has_source_step = false;
    plan.events[0].notes[2] = 127;
    plan.events[0].velocity = nextafter(.5, 1);
    round_trip(&plan);
}

static void test_real_composed_and_linked_plans(void)
{
    const uint16_t masks[] = {0, 1, 0x8000, 0x8234, 0x40aa, 0xffff, 0x5555, 0x0032};
    const double densities[] = {0, .01, .45, .5, .62, .7, .72, .99, 1};
    for (size_t s = 0; s < sizeof(masks) / sizeof(masks[0]); ++s) {
        for (unsigned role = ECHOY_PULSE; role <= ECHOY_HARMONY; ++role) {
            for (unsigned phrase = 1; phrase <= 3; ++phrase) {
                for (size_t d = 0; d < sizeof(densities) / sizeof(densities[0]); ++d) {
                    assert(echoy_compose_mask(masks[s], (echoy_role_t)role, phrase, densities[d], &local));
                    round_trip(&local);
                    for (unsigned parent_role = ECHOY_PULSE; parent_role <= ECHOY_HARMONY; ++parent_role) {
                        for (unsigned parent_phrase = 1; parent_phrase <= 3; ++parent_phrase) {
                            assert(echoy_compose_mask(masks[s], (echoy_role_t)parent_role,
                                parent_phrase, 1 - densities[d], &upstream));
                            assert(echoy_arrange_linked(&local, &upstream, &linked));
                            round_trip(&linked);
                        }
                    }
                }
            }
        }
    }
    /* Full capacity must fit; no valid tail event may be silently dropped. */
    fixture();
    for (size_t i = 1; i < 16; ++i) plan.source[i] = plan.source[0];
    for (size_t i = 1; i < ECHOY_MAX_EVENTS; ++i) plan.events[i] = plan.events[0];
    plan.source_count = 16; plan.count = ECHOY_MAX_EVENTS;
    assert(round_trip(&plan) == 4208);
}

static void reject_bytes(const uint8_t *input, size_t length)
{
    memset(&decoded, 0xa5, sizeof(decoded));
    memcpy(&saved, &decoded, sizeof(saved));
    assert(!echoy_link_plan_decode(input, length, &decoded));
    assert(memcmp(&decoded, &saved, sizeof(saved)) == 0);
    ++rejections;
}

static void test_truncation_and_header_rejection(void)
{
    fixture();
    size_t length = round_trip(&plan);
    for (size_t n = 0; n < length; ++n) reject_bytes(bytes, n);
    reject_bytes(bytes, length + 1);
    reject_bytes(bytes, ECHOY_LINK_PLAN_MAX_BYTES + 1);
    reject_bytes(NULL, length);
    assert(!echoy_link_plan_decode(bytes, length, NULL));
    const struct { size_t at; uint8_t value; } bad[] = {
        {0, 'X'}, {4, 2}, {5, 1}, {6, 31}, {7, 1}, {8, 109}, {11, 1},
        {12, 4}, {13, 0}, {13, 4}, {14, 17}, {15, 65}, {14, 0}, {15, 0},
        {48, 3}, {97, 6}, {98, 5}, {99, 4}, {100, 128}, {101, 4},
        {102, 4}, {103, 2}, {104, 128}, {106, 255}, {107, 128}, {109, 255},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memcpy(bytes, golden, sizeof(golden)); bytes[bad[i].at] = bad[i].value;
        reject_bytes(bytes, sizeof(golden));
    }
    /* Correctly sized messages with only sources or only events must also be
     * rejected: otherwise a half-empty phrase bypasses count/length checks. */
    memcpy(bytes, golden, sizeof(golden));
    bytes[8] = 93; bytes[14] = 0;
    memcpy(bytes + 32, golden + 49, 61);
    reject_bytes(bytes, 93);
    memcpy(bytes, golden, sizeof(golden));
    bytes[8] = 49; bytes[15] = 0;
    reject_bytes(bytes, 49);
}

static void put_test_double(uint8_t *out, double value)
{
    uint64_t bits; memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i) out[i] = (uint8_t)(bits >> (8 * i));
}

static void test_invalid_numeric_wire(void)
{
    const size_t double_offsets[] = {16, 24, 32, 40, 49, 57, 65, 73, 81, 89};
    const double bad_finite[] = {-1, 1e300};
    const double nonfinite[] = {NAN, INFINITY, -INFINITY};
    for (size_t f = 0; f < sizeof(double_offsets) / sizeof(double_offsets[0]); ++f) {
        for (size_t b = 0; b < sizeof(nonfinite) / sizeof(nonfinite[0]); ++b) {
            memcpy(bytes, golden, sizeof(golden));
            put_test_double(bytes + double_offsets[f], nonfinite[b]);
            reject_bytes(bytes, sizeof(golden));
        }
        for (size_t b = 0; b < sizeof(bad_finite) / sizeof(bad_finite[0]); ++b) {
            memcpy(bytes, golden, sizeof(golden));
            put_test_double(bytes + double_offsets[f], bad_finite[b]);
            reject_bytes(bytes, sizeof(golden));
        }
    }
    const struct { size_t at; double value; } bad[] = {
        {16, 1.01}, {24, 0}, {24, 2.51}, {32, 16}, {40, 1.01},
        {49, 16}, {57, 1.01}, {65, 2.51}, {73, 2.51}, {81, 16}, {89, 1.01},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memcpy(bytes, golden, sizeof(golden));
        put_test_double(bytes + bad[i].at, bad[i].value);
        reject_bytes(bytes, sizeof(golden));
    }
}

static void reject_plan(void)
{
    memset(bytes, 0xa5, sizeof(bytes));
    assert(echoy_link_plan_encode(&plan, bytes, sizeof(bytes)) == 0);
    for (size_t i = 0; i < sizeof(bytes); ++i) assert(bytes[i] == 0xa5);
    ++rejections;
}

static void test_encode_validation(void)
{
    fixture();
    assert(!echoy_link_plan_encode(NULL, bytes, sizeof(bytes)));
    assert(!echoy_link_plan_encode(&plan, NULL, sizeof(bytes)));
    for (size_t capacity = 0; capacity < sizeof(golden); ++capacity) {
        memset(bytes, 0xa5, sizeof(bytes));
        assert(!echoy_link_plan_encode(&plan, bytes, capacity));
        for (size_t i = 0; i < sizeof(bytes); ++i) assert(bytes[i] == 0xa5);
    }
#define BAD(member, value) do { fixture(); plan.member = (value); reject_plan(); } while (0)
    BAD(role, (echoy_role_t)-1); BAD(role, (echoy_role_t)4);
    BAD(phrase, 0); BAD(phrase, 4); BAD(density, NAN); BAD(density, 1.01);
    BAD(duration, INFINITY); BAD(duration, 0); BAD(duration, 2.51);
    BAD(source_count, 17); BAD(count, ECHOY_MAX_EVENTS + 1);
    BAD(source_count, 0); BAD(count, 0);
    BAD(source[0].step, 16); BAD(source[0].velocity, -.1); BAD(source[0].kind, ECHOY_TONE);
    BAD(events[0].step, -.1); BAD(events[0].velocity, 1.01);
    BAD(events[0].duration, NAN); BAD(events[0].max_duration, INFINITY);
    BAD(events[0].source_step, 16); BAD(events[0].expression, NAN);
    BAD(events[0].kind, (echoy_note_kind_t)-1); BAD(events[0].layer, (echoy_layer_t)5);
    BAD(events[0].articulation, (echoy_articulation_t)4); BAD(events[0].note, -1);
    BAD(events[0].note_count, 4); BAD(events[0].harmony_count, 4);
    BAD(events[0].notes[2], 128); BAD(events[0].harmony[2], -1);
#undef BAD
}

int main(void)
{
    test_known_wire_and_crc();
    test_real_composed_and_linked_plans();
    test_truncation_and_header_rejection();
    test_invalid_numeric_wire();
    test_encode_validation();
    printf("link codec: %u lossless round trips, %u malformed-input rejections passed\n",
        round_trips, rejections);
    return 0;
}
