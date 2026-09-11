// SPDX-License-Identifier: Apache-2.0
#include "echoy_touch_delivery.h"
static void cleanup(echoy_touch_delivery_t *d,const echoy_touch_delivery_ops_t *ops){
    if(!d->cancel_pending)return;
    ops->pointer(ops->ctx,UI_CANCEL,0,0);
    d->cancel_pending=false;d->cleanups++;
}
void echoy_touch_delivery_send(echoy_touch_delivery_t *d,const echoy_touch_delivery_ops_t *ops,echoy_pointer_t phase,int x,int y){
    if(ops->lock(ops->ctx)){
        /* The cancellation and new event share one lock acquisition, so a
         * stale captured gesture cannot consume the next accepted DOWN. */
        cleanup(d,ops);
        ops->pointer(ops->ctx,phase,x,y);d->processed++;
        ops->unlock(ops->ctx);
    }else{
        /* A lost event makes the gesture incomplete. In particular, losing
         * UP/CANCEL must not leave a capture active indefinitely. */
        d->dropped++;d->cancel_pending=true;
    }
}
void echoy_touch_delivery_flush(echoy_touch_delivery_t *d,const echoy_touch_delivery_ops_t *ops){
    if(d->cancel_pending&&ops->lock(ops->ctx)){
        cleanup(d,ops);ops->unlock(ops->ctx);
    }
}
