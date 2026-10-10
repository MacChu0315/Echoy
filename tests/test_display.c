#include "echoy_display.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    echoy_display_t d;
    echoy_display_init(&d, 1000);
    assert(d.brightness == 100 && d.auto_dim);
    assert(echoy_display_level(&d, 60000999, false) == 100);
    assert(echoy_display_level(&d, 60001000, false) == 20);
    /* Playback/preparation/capture keep the display awake and restart idle time. */
    assert(echoy_display_level(&d, 90000000, true) == 100);
    assert(echoy_display_level(&d, 149999999, false) == 100);
    assert(echoy_display_level(&d, 150000000, false) == 20);
    echoy_display_touch(&d, 151000000);
    assert(echoy_display_level(&d, 151000000, false) == 100);
    echoy_display_set_brightness(&d, 53);
    assert(echoy_display_level(&d, 151000000, false) == 53);
    assert(echoy_display_level(&d, 211000000, false) == 20);
    d.auto_dim = false;
    assert(echoy_display_level(&d, 999000000, false) == 53);
    echoy_display_set_brightness(&d, 0);
    assert(d.brightness == 20);
    d.auto_dim = true;
    assert(echoy_display_level(&d, 999000000, false) == 20);
    echoy_display_set_brightness(&d, 101);
    assert(d.brightness == 100);
    puts("Display: idle threshold, activity, touch, preference and bounds pass");
}
