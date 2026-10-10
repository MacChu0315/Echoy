#pragma once
#include "esp_gsp.h"
#include "echoy_art_frame.h"

/* RGB565 Canvas callbacks. user_ctx points to an echoy_art_frame_t retained by
 * the UI through the GSP flush barrier, with one immutable plan per repaint. */
void echoy_art_draw(const esp_gsp_canvas_surface_t *surface, void *user_ctx);
void echoy_feedback_draw(const esp_gsp_canvas_surface_t *surface, void *user_ctx);

/* Allocate once per UI owner; never share concurrently. Cache/scratch storage
 * avoids repeatedly rasterizing the same artwork for GSP's partial surfaces. */
struct echoy_art_workspace *echoy_art_workspace_create(void);
void echoy_art_workspace_delete(struct echoy_art_workspace *workspace);
