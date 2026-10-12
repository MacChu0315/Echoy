#include "echoy_link_codec.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { HEADER_BYTES = 32, SOURCE_BYTES = 17, EVENT_BYTES = 61 };
static const uint8_t magic[4] = {'E', 'C', 'H', 'P'};
_Static_assert(CHAR_BIT == 8 && sizeof(double) == 8 && FLT_RADIX == 2 &&
               DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "Echoy link requires IEEE-754 binary64 double");
_Static_assert(HEADER_BYTES + 16 * SOURCE_BYTES + ECHOY_MAX_EVENTS * EVENT_BYTES <=
               ECHOY_LINK_PLAN_MAX_BYTES, "plan exceeds transport capacity");

static bool in_range(double value, double low, double high)
{
    return isfinite(value) && value >= low && value <= high;
}

static bool valid_step(double value)
{
    return isfinite(value) && value >= 0 && value < 16;
}

static bool valid_pitch(int note) { return note >= 0 && note <= 127; }

static bool valid_plan(const echoy_plan_t *p)
{
    if (!p || (unsigned)p->role > ECHOY_HARMONY || p->phrase < 1 || p->phrase > 3 ||
        p->source_count > 16 || p->count > ECHOY_MAX_EVENTS ||
        ((p->source_count == 0) != (p->count == 0)) ||
        !in_range(p->density, 0, 1) || !in_range(p->duration, 0, 2.5) || p->duration == 0)
        return false;
    for (size_t i = 0; i < p->source_count; ++i) {
        const echoy_seed_note_t *n = &p->source[i];
        if (!valid_step(n->step) || !in_range(n->velocity, 0, 1) || (unsigned)n->kind > ECHOY_HAT)
            return false;
    }
    for (size_t i = 0; i < p->count; ++i) {
        const echoy_note_t *n = &p->events[i];
        if (!valid_step(n->step) || !valid_step(n->source_step) ||
            !in_range(n->velocity, 0, 1) || !in_range(n->duration, 0, 2.5) ||
            !in_range(n->max_duration, 0, 2.5) || !in_range(n->expression, 0, 1) ||
            (unsigned)n->kind > ECHOY_CHORD || (unsigned)n->layer > ECHOY_DETAIL ||
            (unsigned)n->articulation > ECHOY_ANSWER || !valid_pitch(n->note) ||
            n->note_count > 3 || n->harmony_count > 3)
            return false;
        for (unsigned j = 0; j < 3; ++j) {
            if (!valid_pitch(n->notes[j]) || !valid_pitch(n->harmony[j])) return false;
        }
    }
    return true;
}

static void put_unsigned(uint8_t **out, uint64_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) *(*out)++ = (uint8_t)(value >> (i * 8));
}

static uint64_t get_unsigned(const uint8_t **input, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= (uint64_t)*(*input)++ << (i * 8);
    return value;
}

static void put_double(uint8_t **out, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_unsigned(out, bits, 8);
}

static double get_double(const uint8_t **input)
{
    uint64_t bits = get_unsigned(input, 8);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

size_t echoy_link_plan_encode(const echoy_plan_t *plan, uint8_t *out, size_t capacity)
{
    if (!out || !valid_plan(plan)) return 0;
    size_t length = HEADER_BYTES + plan->source_count * SOURCE_BYTES + plan->count * EVENT_BYTES;
    if (length > capacity) return 0;
    memcpy(out, magic, sizeof(magic));
    uint8_t *at = out + sizeof(magic);
    put_unsigned(&at, 1, 2);
    put_unsigned(&at, HEADER_BYTES, 2);
    put_unsigned(&at, length, 4);
    *at++ = (uint8_t)plan->role;
    *at++ = (uint8_t)plan->phrase;
    *at++ = (uint8_t)plan->source_count;
    *at++ = (uint8_t)plan->count;
    put_double(&at, plan->density);
    put_double(&at, plan->duration);
    for (size_t i = 0; i < plan->source_count; ++i) {
        const echoy_seed_note_t *n = &plan->source[i];
        put_double(&at, n->step);
        put_double(&at, n->velocity);
        *at++ = (uint8_t)n->kind;
    }
    for (size_t i = 0; i < plan->count; ++i) {
        const echoy_note_t *n = &plan->events[i];
        put_double(&at, n->step);
        put_double(&at, n->velocity);
        put_double(&at, n->duration);
        put_double(&at, n->max_duration);
        put_double(&at, n->source_step);
        put_double(&at, n->expression);
        *at++ = (uint8_t)n->kind;
        *at++ = (uint8_t)n->layer;
        *at++ = (uint8_t)n->articulation;
        *at++ = (uint8_t)n->note;
        *at++ = (uint8_t)n->note_count;
        *at++ = (uint8_t)n->harmony_count;
        *at++ = n->has_source_step ? 1 : 0;
        for (unsigned j = 0; j < 3; ++j) *at++ = (uint8_t)n->notes[j];
        for (unsigned j = 0; j < 3; ++j) *at++ = (uint8_t)n->harmony[j];
    }
    return length;
}

bool echoy_link_plan_decode(const uint8_t *bytes, size_t length, echoy_plan_t *out)
{
    if (!bytes || !out || length < HEADER_BYTES || length > ECHOY_LINK_PLAN_MAX_BYTES ||
        memcmp(bytes, magic, sizeof(magic)) != 0) return false;
    const uint8_t *at = bytes + sizeof(magic);
    if (get_unsigned(&at, 2) != 1 || get_unsigned(&at, 2) != HEADER_BYTES ||
        get_unsigned(&at, 4) != length) return false;
    unsigned role = *at++, phrase = *at++, source_count = *at++, count = *at++;
    /* Check all record sizes before allocation or reading any variable data. */
    if (source_count > 16 || count > ECHOY_MAX_EVENTS ||
        length != HEADER_BYTES + source_count * SOURCE_BYTES + count * EVENT_BYTES)
        return false;
    echoy_plan_t *p = calloc(1, sizeof(*p));
    if (!p) return false;
    p->role = (echoy_role_t)role;
    p->phrase = phrase;
    p->source_count = source_count;
    p->count = count;
    p->density = get_double(&at);
    p->duration = get_double(&at);
    for (size_t i = 0; i < p->source_count; ++i) {
        echoy_seed_note_t *n = &p->source[i];
        n->step = get_double(&at);
        n->velocity = get_double(&at);
        n->kind = (echoy_note_kind_t)*at++;
    }
    for (size_t i = 0; i < p->count; ++i) {
        echoy_note_t *n = &p->events[i];
        n->step = get_double(&at);
        n->velocity = get_double(&at);
        n->duration = get_double(&at);
        n->max_duration = get_double(&at);
        n->source_step = get_double(&at);
        n->expression = get_double(&at);
        n->kind = (echoy_note_kind_t)*at++;
        n->layer = (echoy_layer_t)*at++;
        n->articulation = (echoy_articulation_t)*at++;
        n->note = *at++;
        n->note_count = *at++;
        n->harmony_count = *at++;
        unsigned has_source_step = *at++;
        if (has_source_step > 1) { free(p); return false; }
        n->has_source_step = has_source_step != 0;
        for (unsigned j = 0; j < 3; ++j) n->notes[j] = *at++;
        for (unsigned j = 0; j < 3; ++j) n->harmony[j] = *at++;
    }
    bool valid = valid_plan(p);
    if (valid) memcpy(out, p, sizeof(*out));
    free(p);
    return valid;
}

uint32_t echoy_link_crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    if (!bytes && length) return 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return crc ^ UINT32_MAX;
}
