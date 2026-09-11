// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "echoy_protocol.h"
#include "echoy_services.h"
#include "echoy_touch.h"
#define ECHOY_CONTROL_DEPTH 16
#define ECHOY_REQUEST_FLAG 1
/* The caller serializes owner APIs. UI/RPC callbacks enqueue into an external
 * thread-safe bounded queue, then the owner calls input. No callback owns core. */
typedef struct {
    echoy_core_t core; echoy_protocol_t protocol;
    echoy_reassembler_t requests[2]; echoy_wire_state_t remote[2];
    uint64_t physical[2]; bool uncertain; int64_t physical_since_us,request_at_us;
    uint32_t request_transaction; uint64_t session_serial; echoy_session_key_t installed_key;
    echoy_ui_event_t queue[ECHOY_CONTROL_DEPTH]; unsigned head,count,dropped,rejected,publication_failures;
    bool english,output_fault; int64_t last_proposal_us;
    echoy_protocol_send_fn send; void *send_ctx;
    bool (*publish)(void *,const echoy_snapshot_t *,const echoy_bar_plan_t *);void *publish_ctx;
} echoy_control_t;
void echoy_control_init(echoy_control_t *,uint64_t uid,uint32_t boot,uint32_t nonce,const echoy_settings_t *,echoy_protocol_send_fn,void *);
bool echoy_control_enqueue(echoy_control_t *,echoy_ui_event_t);
void echoy_control_physical(echoy_control_t *,uint64_t left,uint64_t right,bool uncertain,int64_t now);
bool echoy_control_receive(echoy_control_t *,uint64_t uid,uint32_t boot,const uint8_t *,size_t,int64_t received,int64_t now);
void echoy_control_tick(echoy_control_t *,int64_t now,bool output_fault);
/* Private audio look-ahead copy; a received COMMIT can be scheduled before its
 * boundary without mutating product state. Caller must cancel if it is aborted. */
bool echoy_control_forecast(const echoy_control_t *,int64_t target,echoy_core_t *scratch);

/* Queue handoff clock is sampled per packet; processing_us also carries the
 * newest processing time back to the owner's later tick. */
typedef int64_t (*echoy_control_now_fn)(void *);
bool echoy_control_receive_queued(echoy_control_t *,uint64_t uid,uint32_t boot,
    const uint8_t *,size_t,int64_t received,int64_t *processing_us,
    echoy_control_now_fn,void *clock_context);
