#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Worker-owned horizontal discovery and bounded transport. No SDK/RTOS/audio. */
#define ECHOY_RADIO_MAX_FRAME 100u
#define ECHOY_CONTACT_LEFT 1u
#define ECHOY_CONTACT_RIGHT 2u
typedef struct echoy_radio echoy_radio_t;
typedef struct {
    bool (*send)(void *ctx,uint64_t next_hop,const uint8_t *data,size_t size);
    void (*edge)(void *ctx,bool left,uint64_t peer,uint32_t session,int64_t now);
    void (*lost)(void *ctx,bool left,int64_t now);
    void (*receive)(void *ctx,uint64_t source,const uint8_t *data,size_t size,int64_t now);
    void (*delivery)(void *ctx,uint64_t destination,uint16_t service,bool success,int64_t now);
    void *ctx;
    /* Optional: stamp clock payload immediately before its first accepted wire
     * submission. Retries retain identical bytes; forwarding never restamps. */
    void (*stamp)(void *ctx,uint8_t *data,size_t size,int64_t now);
} echoy_radio_callbacks_t;
typedef struct {
    uint64_t graph,members[3],peers[2];
    uint32_t contact_generation,transmitted,send_failures,received,rejected,retries,queue_drops;
    unsigned known,count,rank,pending,queue_high_water;
    uint8_t contacts;
    bool ready,admitted,scene_fresh;
} echoy_radio_info_t;
size_t echoy_radio_size(void);
bool echoy_radio_init(echoy_radio_t *radio,size_t size,uint64_t self,uint32_t boot,
                      const echoy_radio_callbacks_t *callbacks);
void echoy_radio_contacts(echoy_radio_t *radio,bool ready,uint8_t mask,int64_t now);
void echoy_radio_receive(echoy_radio_t *radio,uint64_t source,uint32_t source_boot,
                         const uint8_t *data,size_t size,int64_t now);
/* Unknown/legacy same-scene advertisements cannot be treated as absent. */
void echoy_radio_observe_unknown(echoy_radio_t *radio,uint64_t source,uint32_t boot,int64_t now);
/* RX overflow/transport loss invalidates evidence, never invents a release. */
void echoy_radio_recover(echoy_radio_t *radio,int64_t now);
void echoy_radio_tick(echoy_radio_t *radio,int64_t now);
bool echoy_radio_send(echoy_radio_t *radio,uint64_t destination,uint16_t service,
                      const uint8_t *data,size_t size,int64_t now);
bool echoy_radio_info(const echoy_radio_t *radio,echoy_radio_info_t *out);
