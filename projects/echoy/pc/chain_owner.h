#pragma once
#include "echoy_band.h"
#include "echoy_group.h"
typedef struct echoy_chain echoy_chain_t;
/* External simulator adapter only: three actual owners with separate clocks.
 * Caller serializes control/network/audio access; immutable preparation stays
 * outside that lock, exactly as on a device. No UI or alternate sound model. */
echoy_chain_t *echoy_chain_create(void);
void echoy_chain_delete(echoy_chain_t *chain);
echoy_band_t *echoy_chain_band(echoy_chain_t *chain,int id);
int64_t echoy_chain_offset(int id);
bool echoy_chain_action(echoy_chain_t *chain,int id,echoy_action_t action,double value,int64_t physical_us);
bool echoy_chain_read(echoy_chain_t *chain,int id,echoy_control_view_t *out);
bool echoy_chain_edges(echoy_chain_t *chain,const echoy_arrange_edge_t *edges,size_t count,int64_t physical_us);
size_t echoy_chain_get_edges(echoy_chain_t *chain,echoy_arrange_edge_t out[2]);
void echoy_chain_tick(echoy_chain_t *chain,int64_t physical_us);
void echoy_chain_render(echoy_chain_t *chain,int64_t physical_us,float *out,size_t frames);
bool echoy_chain_info(echoy_chain_t *chain,int id,echoy_group_info_t *out);
double echoy_chain_energy(echoy_chain_t *chain,int id);
