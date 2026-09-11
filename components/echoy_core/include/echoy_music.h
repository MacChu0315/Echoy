// SPDX-License-Identifier: Apache-2.0
#ifndef ECHOY_MUSIC_H
#define ECHOY_MUSIC_H
#include "echoy_types.h"
/* Returns false for invalid/beyond-bound inputs, clears output on failure.
 * Aliasing the left motif is rejected without modifying it. No allocation or I/O. */
bool echoy_music_compose(const echoy_music_input_t *input, echoy_motif_t *output);
#endif
