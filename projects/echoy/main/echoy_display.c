#include "echoy_display.h"

void echoy_display_init(echoy_display_t *d, int64_t now)
{
    *d = (echoy_display_t){.brightness = 100, .auto_dim = true, .last_active_us = now};
}
void echoy_display_touch(echoy_display_t *d, int64_t now)
{
    d->last_active_us = now;
}
void echoy_display_set_brightness(echoy_display_t *d, unsigned percent)
{
    d->brightness = percent < 20 ? 20 : percent > 100 ? 100 : percent;
}
unsigned echoy_display_level(echoy_display_t *d, int64_t now, bool active)
{
    if (active) {
        echoy_display_touch(d, now);
    }
    return d->auto_dim && now - d->last_active_us >= INT64_C(60000000) ? 20 : d->brightness;
}
