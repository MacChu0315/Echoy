#include "board_touch_trace.h"
int main(void)
{
    board_touch_trace_reset();
    board_touch_trace_note_phase(2);
    board_touch_trace_note_observer(10, 20, true, 100, 2, 90, 0, true);
    board_touch_trace_dump();
    board_touch_trace_freeze();
    return 0;
}
