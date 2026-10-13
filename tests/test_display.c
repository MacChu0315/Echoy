#include "echoy_display.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    echoy_display_t d;
    echoy_display_init(&d, 1000);
    assert(d.brightness == 100);
    assert(echoy_display_level(&d, 60000999, false) == 100);
    assert(echoy_display_level(&d, 60001000, false) == 100);
    /* Idle, activity and touches never alter the chosen brightness. */
    assert(echoy_display_level(&d, 90000000, true) == 100);
    assert(echoy_display_level(&d, 149999999, false) == 100);
    assert(echoy_display_level(&d, 150000000, false) == 100);
    echoy_display_touch(&d, 151000000);
    assert(echoy_display_level(&d, 151000000, false) == 100);
    echoy_display_set_brightness(&d, 53);
    assert(echoy_display_level(&d, 151000000, false) == 53);
    assert(echoy_display_level(&d, 211000000, false) == 53);
    assert(echoy_display_level(&d, 999000000, false) == 53);
    assert(echoy_display_level(&d, INT64_C(86400000000), true) == 53);
    echoy_display_touch(&d, INT64_C(86400000001));
    assert(echoy_display_level(&d, INT64_C(86460000001), false) == 53);
    echoy_display_set_brightness(&d, 0);
    assert(d.brightness == 20);
    assert(echoy_display_level(&d, 999000000, false) == 20);
    echoy_display_set_brightness(&d, 101);
    assert(d.brightness == 100);
    puts("Display: chosen brightness survives idle/activity/touch; manual bounds pass");
}
