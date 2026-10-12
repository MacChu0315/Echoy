#pragma once
#include "echoy_music.h"

#define ECHOY_LINK_PLAN_MAX_BYTES 8192

/* ECHP version 1, independent of C ABI and padding. All multibyte values are
 * little endian; doubles use IEEE-754 binary64 without rounding.
 *
 * Header (32 bytes): magic "ECHP", u16 version=1, u16 header_size=32,
 * u32 total_size, u8 role, phrase, source_count, event_count, then binary64
 * density and duration. A source record is 17 bytes: binary64 step, velocity,
 * then u8 kind. An event is 61 bytes: binary64 step, velocity, duration,
 * max_duration, source_step, expression; then u8 kind, layer, articulation,
 * note, note_count, harmony_count, has_source_step (0 or 1), notes[3], harmony[3].
 * All three entries of both pitch arrays are preserved, including unused ones.
 * There is no implicit alignment or trailing data. CRC belongs to the transport.
 *
 * Both directions validate finite numbers, enum/count bounds, MIDI pitches
 * [0,127], steps [0,16), density/velocity/expression [0,1] and durations [0,2.5]
 * (plan duration must be positive). Phrase is 1..3. Source kinds are drums.
 * A plan is empty only when both source_count and count are zero.
 *
 * Encode returns the byte count, or 0 without changing out on invalid input or
 * insufficient capacity. The plan and output buffer must not overlap.
 * Decode returns false without changing out, including allocation failure;
 * scratch storage is heap allocated so the device task stack stays bounded.
 */
size_t echoy_link_plan_encode(const echoy_plan_t *plan, uint8_t *out, size_t capacity);
bool echoy_link_plan_decode(const uint8_t *bytes, size_t length, echoy_plan_t *out);

/* Standard reflected IEEE CRC-32 (polynomial 0xedb88320, init/final xor all 1s).
 * An empty input has CRC 0; NULL is permitted only with length 0. */
uint32_t echoy_link_crc32(const uint8_t *bytes, size_t length);
