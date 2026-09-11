// SPDX-License-Identifier: Apache-2.0
#include "../components/echoy_core/music_internal.h"
#include <assert.h>
#include <stdio.h>

static void mathematical_semantics(void)
{
    assert(wrap16(-1)==15 && wrap16(-17)==15 && wrap16(32)==0);
    assert(music_wrap(-13,12)==11 && music_wrap(-.5,16)==15.5);
    assert(music_round(-1.5)==-1 && music_round(-.5)==0 && music_round(.5)==1);
    assert(music_clamp(NAN,.05,1)==.05);
}
static void validation(void)
{
    echoy_motif_t out,left={0};
    echoy_music_input_t in={ECHOY_VOICE,1,0,0,.56,96,NULL};
    assert(!echoy_music_compose(NULL,&out) && out.count==0);
    assert(!echoy_music_compose(&in,NULL));
    in.left=&left;left.count=17;assert(!echoy_music_compose(&in,&out));left.count=0;
    in.bpm=NAN;assert(!echoy_music_compose(&in,&out));in.bpm=0;assert(!echoy_music_compose(&in,&out));in.bpm=96;
    in.phrase=0;assert(!echoy_music_compose(&in,&out));in.phrase=4;assert(!echoy_music_compose(&in,&out));in.phrase=1;
    in.duplicate=3;assert(!echoy_music_compose(&in,&out));in.duplicate=0;
    in.activity=NAN;assert(echoy_music_compose(&in,&out));assert(out.events[0].expression==-.56);
    in.activity=INFINITY;assert(echoy_music_compose(&in,&out));
    in.activity=.56;left.count=1;left.events[0]=music_event(0,ECHOY_TONE,.5,62,.1,false);
    left.events[0].notes_count=4;assert(!echoy_music_compose(&in,&out));left.events[0].notes_count=0;
    left.events[0].step=NAN;assert(echoy_music_compose(&in,&out) && out.count==0);
    left.events[0].step=0;left.events[0].dur=INFINITY;assert(echoy_music_compose(&in,&out) && out.count==0);
    left.events[0].dur=.1;left.events[0].vel=NAN;left.events[0].note=NAN;assert(echoy_music_compose(&in,&out) && out.count>0);
    assert(!echoy_music_compose(&in,&left) && left.count==1);
}
static void left_motif_is_immutable(void)
{
    const echoy_role_t chains[][3]={{ECHOY_PULSE,ECHOY_VOICE,ECHOY_HARMONY},{ECHOY_HARMONY,ECHOY_DUST,ECHOY_PULSE},{ECHOY_HARMONY,ECHOY_VOICE,ECHOY_PULSE},
        {ECHOY_PULSE,ECHOY_PULSE,ECHOY_PULSE},{ECHOY_VOICE,ECHOY_VOICE,ECHOY_VOICE},{ECHOY_DUST,ECHOY_DUST,ECHOY_DUST},{ECHOY_HARMONY,ECHOY_HARMONY,ECHOY_HARMONY}};
    for(size_t c=0;c<sizeof chains/sizeof chains[0];c++)for(int phrase=1;phrase<=3;phrase++){
        echoy_motif_t chain[3];int counts[4]={0};
        for(int i=0;i<3;i++){
            echoy_role_t role=chains[c][i];echoy_music_input_t in={role,phrase,2,counts[role]++,role_initial(role),96,i?&chain[i-1]:NULL};
            assert(echoy_music_compose(&in,&chain[i]));
        }
        echoy_motif_t before=chain[1],right;
        echoy_music_input_t changed={ECHOY_DUST,3,2,0,1,96,&chain[1]};
        assert(echoy_music_compose(&changed,&right));
        assert(memcmp(&before,&chain[1],sizeof before)==0);
    }
}
int main(void)
{
    mathematical_semantics();validation();left_motif_is_immutable();puts("PASS music math, invalid inputs, direct-left immutability");return 0;
}
