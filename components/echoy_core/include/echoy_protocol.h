// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_PROTOCOL_H
#define ECHOY_PROTOCOL_H
#include "echoy_wire.h"
#include "echoy_clock.h"
#define ECHOY_HEARTBEAT_US INT64_C(250000)
#define ECHOY_HEARTBEAT_MISSES 3
#define ECHOY_PROTOCOL_HOLD_US INT64_C(1800000)
typedef bool (*echoy_protocol_send_fn)(void *ctx,uint64_t target,const uint8_t *data,size_t size);
typedef bool (*echoy_protocol_prepare_fn)(void *ctx,const echoy_wire_state_t *state);
typedef void (*echoy_protocol_commit_fn)(void *ctx,const echoy_wire_state_t *state,
    echoy_session_key_t session,int64_t local_epoch,int rate_ppm,int64_t at_us);
typedef struct {
    uint64_t uid; uint32_t boot; bool authorized,member,lost,expired;
    int64_t last_rx_us,lost_us,time_request_us; uint8_t advertised_members;
    uint64_t advertised_reference; echoy_session_key_t session;
    uint32_t sequence; echoy_clock_t clock; echoy_reassembler_t assembly;
} echoy_protocol_peer_t;
typedef struct {
    bool active,coordinator,commit_seen; echoy_wire_state_t state;
    echoy_session_key_t session; uint32_t transaction,apply_bar,digest;
    int64_t apply_us,epoch_us,last_send_us; int rate_ppm;
    unsigned ready_mask,ack_mask;
} echoy_protocol_pending_t;
typedef struct {
    uint64_t uid; uint32_t boot,sequence,next_transaction;
    echoy_session_key_t session; echoy_wire_state_t committed;
    echoy_protocol_peer_t peers[2]; echoy_protocol_pending_t pending;
    int64_t now_us,last_periodic_us; bool syncing,degraded;
    unsigned rejected,queue_full,send_failures,discovered;
    echoy_protocol_send_fn send; echoy_protocol_prepare_fn prepare;
    echoy_protocol_commit_fn commit; void *ctx;
} echoy_protocol_t;
void echoy_protocol_init(echoy_protocol_t *p,uint64_t uid,uint32_t boot,uint32_t nonce,
    echoy_protocol_send_fn send,echoy_protocol_prepare_fn prepare,echoy_protocol_commit_fn commit,void *ctx);
/* Only the control owner calls these APIs. Authorization requires a reliable BSP
 * neighbor UID and a HELLO boot for that same UID, never RSSI or discovery alone.
 * A changed boot clears all pending work and does not restore membership. */
bool echoy_protocol_authorize(echoy_protocol_t *p,uint64_t uid,uint32_t boot,int64_t now_us);
bool echoy_protocol_receive(echoy_protocol_t *p,uint64_t source,uint32_t outer_boot,
    const uint8_t *data,size_t size,int64_t received_us,int64_t send_now_us);
bool echoy_protocol_begin(echoy_protocol_t *p,const echoy_wire_state_t *state,int64_t now_us);
void echoy_protocol_tick(echoy_protocol_t *p,int64_t now_us);
uint64_t echoy_protocol_reference(const echoy_protocol_t *p);
#endif
