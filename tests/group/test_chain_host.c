#include "chain_owner.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void advance(echoy_chain_t *chain,int64_t from,int64_t until)
{
    for(int64_t now=from;now<until;now+=10000){
        echoy_chain_tick(chain,now);
        for(int id=0;id<3;id++){
            echoy_band_t *band=echoy_chain_band(chain,id);
            echoy_band_job_t *job=echoy_band_take_job(band);
            if(job){assert(echoy_band_prepare_job(job));assert(echoy_band_finish_job(band,job,now+echoy_chain_offset(id)));}
        }
        float pcm[240];echoy_chain_render(chain,now,pcm,240);
        for(unsigned i=0;i<240;i++)assert(isfinite(pcm[i]));
    }
}
static void check(echoy_chain_t *chain,const int order[3])
{
    for(int rank=0;rank<3;rank++){
        int id=order[rank];echoy_control_view_t v;assert(echoy_chain_read(chain,id,&v));
        assert(v.state.following==(rank!=0));assert(!v.state.continuous);
        assert(v.cooperation==(rank?ECHOY_COOP_FOLLOWING:ECHOY_COOP_CONNECTED));
        echoy_group_info_t info;assert(echoy_chain_info(chain,id,&info));
        assert(info.count==3 && info.rank==(unsigned)rank && info.phase==5 && info.operations<=6);
        int64_t expected=echoy_chain_offset(id)-echoy_chain_offset(order[0]);
        assert(llabs(info.root_offset_us-expected)<10000);
        for(int i=0;i<3;i++)assert(info.members[i]==(uint64_t)order[i]+1);
    }
}
int main(void)
{
    const int orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for(unsigned p=0;p<6;p++){
        const int *order=orders[p];echoy_chain_t *chain=echoy_chain_create();assert(chain);
        for(int id=0;id<3;id++)assert(echoy_chain_action(chain,id,ECHOY_ACT_EXAMPLE,0,1000000));
        echoy_arrange_edge_t edges[]={{order[0],order[1]},{order[1],order[2]}};
        assert(echoy_chain_edges(chain,edges,2,1000000));
        echoy_arrange_edge_t bad[]={{0,1},{0,2}};assert(!echoy_chain_edges(chain,bad,2,1000000));
        advance(chain,1000000,17000000);check(chain,order);
        /* Remove the root and immediately attach it to the former tail. The
         * radio sees all three units; it must not shortcut across the middle. */
        assert(echoy_chain_edges(chain,edges+1,1,17000000));
        const int rotated[]={order[1],order[2],order[0]};
        echoy_arrange_edge_t reordered[]={{rotated[0],rotated[1]},{rotated[1],rotated[2]}};
        assert(echoy_chain_edges(chain,reordered,2,17000000));
        advance(chain,17000000,33000000);check(chain,rotated);
        assert(echoy_chain_edges(chain,NULL,0,33000000));
        advance(chain,33000000,33500000);
        for(int id=0;id<3;id++){
            echoy_control_view_t v;assert(echoy_chain_read(chain,id,&v));
            assert(!v.state.following && !v.state.continuous && v.cooperation==ECHOY_COOP_SOLO);
        }
        assert(echoy_chain_band(chain,0)!=echoy_chain_band(chain,1));
        echoy_chain_delete(chain);printf("PASS actual owners/PCM with real radio admission: order%u, fast root-to-tail reorder, detach\n",p);
    }
}
