#pragma once
#include "echoy_link_codec.h"

/* Portable primitives shared by the physical worker and independent native
 * owners. No platform, radio, task, UI or audio calls are made here. */
typedef enum { ECHOY_LINK_REJECTED=-1, ECHOY_LINK_INCOMPLETE=0,
               ECHOY_LINK_COMPLETE=1 } echoy_link_fragment_result_t;
typedef struct {
    uint8_t bytes[ECHOY_LINK_PLAN_MAX_BYTES];
    uint8_t coverage[(ECHOY_LINK_PLAN_MAX_BYTES+7)/8];
    uint64_t revision;
    uint32_t crc;
    uint16_t size, covered;
} echoy_link_receiver_t;
bool echoy_link_receiver_begin(echoy_link_receiver_t *rx, uint64_t revision,
                               uint32_t crc, size_t size);
echoy_link_fragment_result_t echoy_link_receiver_chunk(echoy_link_receiver_t *rx,
    uint64_t revision, size_t offset, const uint8_t *data, size_t size);

typedef struct {
    bool valid;
    unsigned samples;
    int64_t offset, best_offset, best_rtt,window_start;
} echoy_link_clock_t;
/* offset translates remote timestamps onto the local timeline. frozen keeps
 * an armed mapping immutable while still collecting useful observations. */
bool echoy_link_clock_sample(echoy_link_clock_t *clock, int64_t t1, int64_t t2,
                             int64_t t3, int64_t t4, bool frozen);

typedef struct {
    bool active;
    uint64_t destination;
    uint16_t service, offset, bytes;
    uint32_t generation, transfer;
    uint8_t kind;
    int64_t since;
} echoy_link_slot_t;
typedef struct { uint16_t serial; echoy_link_slot_t slots[6]; } echoy_link_pool_t;
/* Six persistent node-wide operations, including retired destinations. The
 * first two slots are reserved for control, the other four for data. A single
 * destination may occupy at most one control and two data operations. */
echoy_link_slot_t *echoy_link_pool_reserve(echoy_link_pool_t *pool,uint64_t destination,
    bool control,uint32_t generation,int64_t now);
bool echoy_link_pool_take(echoy_link_pool_t *pool,uint64_t destination,
    uint16_t service,echoy_link_slot_t *completed);
