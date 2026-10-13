#include "echoy_display.h"

void echoy_display_init(echoy_display_t *d, int64_t now)
{
    (void)now;
    *d = (echoy_display_t){.brightness = 100};
}
void echoy_display_touch(echoy_display_t *d, int64_t now)
{
    (void)d;
    (void)now;
}
void echoy_display_set_brightness(echoy_display_t *d, unsigned percent)
{
    d->brightness = percent < 20 ? 20 : percent > 100 ? 100 : percent;
}
unsigned echoy_display_level(echoy_display_t *d, int64_t now, bool active)
{
    (void)now;
    (void)active;
    return d->brightness;
}
