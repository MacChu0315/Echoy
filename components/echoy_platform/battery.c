// SPDX-License-Identifier: Apache-2.0
#include "echoy_services.h"
bool echoy_battery_observe(echoy_battery_t *b,int error,unsigned value){
    if(!b)return false;
    if(error||value>100){b->last_error=error?error:-1;b->failures++;return false;}
    b->last_error=0;b->valid=true;b->percent=(int)value;b->low=value<15;b->shutdown_requested=value==0;return true;
}
