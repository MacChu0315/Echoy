#pragma once
#include "echoy_music.h"
#include "echoy_pickup.h"

/* Owned by the UI render thread. Initialize before registering Canvas callbacks;
 * mutate only from serialized UI callbacks and retain through the flush barrier. */
struct echoy_art_workspace;

typedef struct {
    const echoy_plan_t *plan;
    echoy_pickup_t pickup; /* Owned value: safe across artwork worker handoff. */
    struct echoy_art_workspace *workspace; /* Optional; one serialized UI owner. */
    echoy_role_t role;
    double progress;
    float energy;
    unsigned width, height;
    uint32_t background; /* RGB888; zero means the paper color. */
    bool active, empty, anchors_only;
} echoy_art_frame_t;
