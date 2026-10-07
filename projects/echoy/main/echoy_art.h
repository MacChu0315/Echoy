#pragma once
#include "esp_gsp.h"
#include <stdatomic.h>
/* Packed snapshot: 16 seed bits, 5 active-step bits (16 means none), 7 energy bits. */
void echoy_art_draw(const esp_gsp_canvas_surface_t *surface, void *snapshot);
void echoy_feedback_draw(const esp_gsp_canvas_surface_t *surface, void *snapshot);
