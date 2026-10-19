#define main music_runner_main
#include "../music/runner.c"
#undef main
#include "echoy_pickup.h"
#include "echoy_arrange.h"
int main(void) {
    unsigned role,phrase,mask,depth; double density;
    while(scanf("%u %u %lf %u %u",&role,&phrase,&density,&mask,&depth)==5) {
        echoy_plan_t p,local,linked,copy,intro;
        assert(echoy_compose_mask(mask,role,phrase,density,&p));
        for(unsigned i=0;i<depth;i++) {
            assert(echoy_compose_mask(mask,(role+i+1)%4,phrase,density,&local));
            assert(echoy_arrange_linked(&local,&p,&linked)); p=linked;
        }
        copy=p; echoy_pickup_t pickup,again;
        assert(echoy_pickup_apply(&p,&pickup)); assert(!memcmp(&p,&copy,sizeof p));
        assert(echoy_pickup_apply(&p,&again)); assert(!memcmp(&pickup,&again,sizeof pickup));
        print_plan(&p);
        intro=p; intro.count=pickup.count;
        memcpy(intro.events,pickup.notes,pickup.count*sizeof(echoy_note_t));
        print_plan(&intro);
        printf("[%zu,%zu,%zu]\n",pickup.boundary,pickup.end,pickup.fade);
    }
}
