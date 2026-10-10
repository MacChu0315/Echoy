#pragma once
#include <stdint.h>

/* Preserve the original RGB888 truncation, using hardware float except very
 * close to an integer boundary. With channels <=255 and alpha in [0,1], the
 * three float roundings contribute <1/4096 channel value in total. */
static inline unsigned echoy_art_blend_channel(unsigned base, unsigned ink, float alpha)
{
    if (base == ink) return base;
    float removed = base * alpha;
    float mixed = base + ink * alpha - removed;
    unsigned value = (unsigned)mixed;
    float fraction = mixed - value;
    if (fraction < 1.0f/4096 || fraction > 1 - 1.0f/4096)
        return (unsigned)(base + ink * (double)alpha - removed);
    return value;
}
