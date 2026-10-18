#include "../../projects/echoy/main/echoy_art.c"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    echoy_plan_t p={.count=ECHOY_MAX_EVENTS};
    for(size_t i=0;i<p.count;i++)p.events[i]=(echoy_note_t){.step=4+i*.1,.layer=ECHOY_ANCHOR};
    echoy_art_frame_t frame={.plan=&p,.role=ECHOY_PULSE,.width=300,.height=204};
    struct echoy_art_workspace *w=echoy_art_workspace_create();assert(w);
    w->valid=true;w->frame=frame;w->plan=p;assert(same_frame(w,&frame));
    frame.pickup=(echoy_pickup_t){.count=2,.notes={{.step=0,.layer=ECHOY_DETAIL},{.step=2,.layer=ECHOY_DETAIL}}};
    assert(!same_frame(w,&frame));
    assert(note_count(&frame)==ECHOY_MAX_EVENTS+2);
    assert(note_at(&frame,0)==&frame.pickup.notes[0]);
    assert(note_at(&frame,1)==&frame.pickup.notes[1]);
    for(size_t i=0;i<p.count;i++)assert(note_at(&frame,i+2)==&p.events[i]);
    unsigned cells[16];onset_cells(&frame,cells);assert(cells[0]==1&&cells[2]==1&&cells[4]==2);
    frame.anchors_only=true;onset_cells(&frame,cells);assert(cells[0]==0&&cells[2]==0&&cells[4]==2);
    echoy_art_workspace_delete(w);
    puts("PASS artwork/feedback merges two pickup events without truncating 64 original events; capture hides pickup; cache invalidates");
}
