// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_STATE_INTERNAL_H
#define ECHOY_STATE_INTERNAL_H
#include "echoy_core.h"
#define MEMBER(i) (1u << (i))
#define EDGE(a,b) (1u << ((a)*3+(b)))
echoy_session_t *echoy_session_for(echoy_core_t *c,int d);
void echoy_leave(echoy_core_t *c,int d);
void echoy_fresh_session(echoy_core_t *c,int d,int64_t at,int64_t epoch);
void echoy_session_tick(echoy_core_t *c);
echoy_result_t echoy_receive_contacts(echoy_core_t *c,const echoy_input_t *e);
void echoy_commit_members(echoy_core_t *c,unsigned members);
void echoy_commit_idle(echoy_core_t *c,int d);
void echoy_publish(echoy_core_t *c);
#endif
