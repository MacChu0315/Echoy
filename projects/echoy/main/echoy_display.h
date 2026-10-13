#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Display preferences are per running tile; musical state is never stored here. */
typedef struct {
    unsigned brightness;
} echoy_display_t;
/* Timing arguments remain source-compatible; only explicit adjustment changes
 * brightness. Idle time, playback and touch no longer dim or brighten it. */
void echoy_display_init(echoy_display_t *display, int64_t now_us);
void echoy_display_touch(echoy_display_t *display, int64_t now_us);
void echoy_display_set_brightness(echoy_display_t *display, unsigned percent);
unsigned echoy_display_level(echoy_display_t *display, int64_t now_us, bool active);
