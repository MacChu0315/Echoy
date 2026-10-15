#pragma once
#include "echoy_control.h"

/* Application-owned link/owner contract. All timestamps are translated to the
 * receiving device's monotonic timeline before queue admission. No OS types. */
typedef enum {
    ECHOY_LINK_NONE,
    ECHOY_LINK_BEGIN,  /* leader binds its proposed local revision */
    ECHOY_LINK_OFFER,  /* follower prepares the complete proposed upstream plan */
    ECHOY_LINK_ARM,    /* freeze a prepared revision at a future common boundary */
    ECHOY_LINK_COMMIT, /* authorize the previously armed boundary */
    ECHOY_LINK_ABORT
} echoy_link_command_t;

typedef struct {
    bool attached, following, recovering, hold_changes;
    bool valid, clock_started, looping, failed;
    bool clock_sync, peer_looping;
    /* V3 groups adopt the frozen activation time as their next bar origin,
     * including owners whose musical bank is unchanged. */
    bool align_boundary;
    uint32_t clock_start_ack;
    uint32_t connection_generation, session, transaction;
    uint64_t revision;
    echoy_link_command_t command;
    int64_t clock_origin_us, commit_at_us;
    echoy_plan_t plan;
} echoy_link_update_t;

typedef struct {
    bool available, deferred, following, clock_started, looping;
    uint32_t local_start_serial, local_edit_serial;
    bool busy;
    int64_t clock_origin_us;
    echoy_plan_t current, proposal;
    uint64_t requested_revision, proposal_revision;
    bool proposal_pending, proposal_ready, proposal_failed;
    uint32_t offered_transaction, armed_transaction, authorized_transaction;
    uint32_t committed_transaction, failed_transaction;
    uint64_t offered_revision, armed_revision;
    int64_t armed_at_us;
    bool offer_ready, offer_failed;
} echoy_link_view_t;
