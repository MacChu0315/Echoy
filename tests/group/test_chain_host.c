#include "chain_owner.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
    echoy_chain_t *chain=echoy_chain_create(); assert(chain);
    for (int id=0;id<3;++id)
        assert(echoy_chain_action(chain,id,ECHOY_ACT_EXAMPLE,0,1000000));
    echoy_arrange_edge_t edges[]={{0,1},{1,2}};
    assert(echoy_chain_edges(chain,edges,2,1000000));
    echoy_arrange_edge_t bad[]={{0,1},{0,2}};
    assert(!echoy_chain_edges(chain,bad,2,1000000));
    for (int64_t now=1000000;now<17000000;now+=10000) {
        echoy_chain_tick(chain,now);
        for (int id=0;id<3;++id) {
            echoy_band_t *band=echoy_chain_band(chain,id);
            echoy_band_job_t *job=echoy_band_take_job(band);
            if (job) {
                assert(echoy_band_prepare_job(job));
                assert(echoy_band_finish_job(band,job,now+echoy_chain_offset(id)));
            }
        }
        float pcm[240]; echoy_chain_render(chain,now,pcm,240);
        for (unsigned i=0;i<240;++i) assert(isfinite(pcm[i]));
    }
    for (int id=0;id<3;++id) {
        echoy_control_view_t v;
        assert(echoy_chain_read(chain,id,&v));
        assert(v.state.following==(id!=0));
        assert(!v.state.continuous);
        echoy_group_info_t info;
        assert(echoy_chain_info(chain,id,&info) && info.count==3 && info.operations<=6);
    }
    assert(echoy_chain_band(chain,0)!=echoy_chain_band(chain,1));
    echoy_chain_delete(chain);
    puts("PASS native chain adapter: three distinct actual owners, offsets, real protocol/PCM, topology validation and cleanup");
}
