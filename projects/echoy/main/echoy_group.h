#pragma once
#include "echoy_coop.h"
#include <stddef.h>

typedef struct echoy_group echoy_group_t;
typedef struct {
    bool (*read)(void *ctx,echoy_link_view_t *out);
    bool (*receive)(void *ctx,const echoy_link_update_t *update);
    bool (*send)(void *ctx,uint64_t destination,uint16_t service,
                 const uint8_t *bytes,size_t size);
    void *ctx;
} echoy_group_callbacks_t;
/* Caller-owned cold storage: PSRAM on hardware, independent allocations on PC.
 * No asynchronous callbacks or owned threads survive the caller's worker. */
size_t echoy_group_size(void);
bool echoy_group_init(echoy_group_t *group,size_t size,uint64_t self,uint32_t boot,
                       const echoy_group_callbacks_t *callbacks);
/* A zero peer is a confirmed physical release. Radio-only loss must retain the
 * edge; a renewed nonzero session retires prior protocol state. */
void echoy_group_edge(echoy_group_t *group,bool left,uint64_t peer,uint32_t session,int64_t now);
/* A transport loss on an admitted retained seam reports recovery, not release. */
void echoy_group_radio_lost(echoy_group_t *group,bool left,int64_t now);
void echoy_group_receive(echoy_group_t *group,uint64_t source,const uint8_t *bytes,
                          size_t size,int64_t now);
void echoy_group_delivery(echoy_group_t *group,uint64_t destination,uint16_t service,
                           bool success,int64_t now);
/* Correct only PING/PONG send timestamps at actual first wire submission.
 * Caller keeps these bytes unchanged for reliable retransmissions. */
void echoy_group_stamp_send(echoy_group_t *group,uint8_t *bytes,size_t size,int64_t now);
void echoy_group_tick(echoy_group_t *group,int64_t now);

/* Copied worker diagnostics for the external native desk / device evidence;
 * these never grant musical permission or appear on the 480x480 interface. */
typedef struct {
    uint64_t members[3];
    uint32_t identity,generation,attempt;
    unsigned count,rank,phase,operations;
    bool clock_mapped;
    int64_t root_offset_us,origin_us,boundary_us,deadline_us;
} echoy_group_info_t;
bool echoy_group_info(const echoy_group_t *group,echoy_group_info_t *out);
