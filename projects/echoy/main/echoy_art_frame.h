#pragma once
#include "echoy_music.h"

/* Owned by the UI render thread. Initialize before registering Canvas callbacks;
 * mutate only from serialized UI callbacks and retain through the flush barrier. */
typedef struct {
    const echoy_plan_t *plan;
    echoy_role_t role;
    double progress;
    float energy;
    unsigned width, height;
    uint32_t background; /* RGB888; zero means the paper color. */
    bool active, empty, anchors_only;
} echoy_art_frame_t;
