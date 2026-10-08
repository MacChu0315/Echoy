#pragma once
#include "echoy_art_frame.h"
#include <stddef.h>
#include <stdint.h>

/* Render a 1..480 by 1..480 art frame into an opaque QOI image.
 * On success the caller owns *out and must free() it. On failure, any supplied
 * output pointer is set to NULL and any supplied size is set to zero.
 * Does not submit to GSP; call outside Canvas/render callbacks. */
bool echoy_art_encode_qoi(const echoy_art_frame_t *frame, uint8_t **out, size_t *size);
