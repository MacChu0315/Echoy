// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_WIRE_H
#define ECHOY_WIRE_H
#include "echoy_core.h"
#define ECHOY_WIRE_HEADER 40u
#define ECHOY_WIRE_BODY 60u
#define ECHOY_WIRE_MAX 100u
#define ECHOY_WIRE_FRAGMENTS 16u
#define ECHOY_WIRE_STATE_MAX 960u
#define ECHOY_ALGORITHM_VERSION 1u
typedef enum { ECHOY_HELLO=1, ECHOY_TIME_REQ, ECHOY_TIME_RESP, ECHOY_HEARTBEAT,
    ECHOY_PREPARE, ECHOY_READY_MSG, ECHOY_COMMIT, ECHOY_ACK, ECHOY_ABORT } echoy_wire_kind_t;
typedef struct { uint64_t creator_uid; uint32_t creator_boot, nonce; } echoy_session_key_t;
typedef struct {
    echoy_wire_kind_t kind; uint32_t sender_boot, sequence;
    echoy_session_key_t session; uint32_t transaction, apply_bar;
    uint8_t fragment_index, fragment_count, body_length, flags;
    uint8_t body[ECHOY_WIRE_BODY];
} echoy_packet_t;
typedef struct {
    uint64_t uid; uint32_t boot; uint8_t role, phrase;
    uint16_t activity; bool motif_available; uint8_t transport;
} echoy_wire_member_t;
typedef struct {
    uint32_t revision; int64_t epoch_us; int32_t rate_ppm;
    uint64_t reference_uid, transport_request_uid;
    uint8_t member_count, edge_count; echoy_wire_member_t members[3];
    uint8_t edges[2][2];
} echoy_wire_state_t;
typedef enum { ECHOY_WIRE_OK, ECHOY_WIRE_MORE, ECHOY_WIRE_BAD,
    ECHOY_WIRE_REPLAY, ECHOY_WIRE_FULL } echoy_wire_result_t;
typedef struct {
    bool used; echoy_packet_t first; uint8_t next; size_t size;
    uint8_t data[ECHOY_WIRE_STATE_MAX]; int64_t started_us;
} echoy_assembly_t;
typedef struct {
    uint64_t uid; uint32_t boot, sequence, completed_transaction;
    echoy_assembly_t transactions[2];
} echoy_reassembler_t;
uint64_t echoy_get_le(const uint8_t *p, unsigned n);
void echoy_put_le(uint8_t *p, uint64_t v, unsigned n);
bool echoy_session_equal(echoy_session_key_t a, echoy_session_key_t b);
uint32_t echoy_wire_digest(const uint8_t *data, size_t size);
bool echoy_wire_encode(const echoy_packet_t *p, uint8_t out[100], size_t *size);
bool echoy_wire_decode(const uint8_t *data, size_t size, echoy_packet_t *out);
bool echoy_wire_state_encode(const echoy_wire_state_t *s, uint8_t out[960], size_t *size);
bool echoy_wire_state_decode(const uint8_t *data, size_t size, echoy_wire_state_t *out);
echoy_wire_result_t echoy_wire_assemble(echoy_reassembler_t *r, uint64_t source,
    uint32_t outer_boot, const echoy_packet_t *p, int64_t now, echoy_wire_state_t *out);
/* Installs a validated committed snapshot on the control owner. Local transport,
 * live expression, volume and output/data faults are never set by a peer. */
echoy_result_t echoy_core_install(echoy_core_t *core, int local,
    const echoy_wire_state_t *state, uint64_t session_id, int64_t local_epoch_us,
    int local_rate_ppm, int64_t now_us);
#endif
