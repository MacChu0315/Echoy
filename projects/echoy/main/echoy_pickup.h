#pragma once
#include "echoy_music.h"

/* Local playback decoration only. Never serialize or import these notes as a
 * relationship plan. ECHOY_DETAIL denotes non-anchor artwork; the containing
 * pickup object supplies the HTML's separate 'pickup' layer identity. */
typedef struct {
    echoy_note_t notes[2];
    size_t count, boundary, end, fade;
} echoy_pickup_t;

/* Pure port of OpeningStudy.apply(), using source as the original root seed
 * and anchor events as foreground. Neither input nor its events are changed. */
bool echoy_pickup_apply(const echoy_plan_t *plan, echoy_pickup_t *out);
/* OpeningStudy.mix(): writes only the added layer, never normalizes the base.
 * raw and layer may alias. Buffers contain one 24 kHz, 2.5-second loop. */
bool echoy_pickup_mix(const echoy_pickup_t *pickup, const float *base,
                      const float *raw, double gain, float *layer);
