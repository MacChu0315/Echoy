#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Only fixed icon+word glyphs are raster masks. Hit targets and presentation
 * state are native controls; dynamic headings/messages remain native text. */
bool echoy_strip_glyph_qoi(bool english, unsigned kind, uint8_t opacity, uint32_t color_rgb888,
                           uint8_t **out, size_t *size);
