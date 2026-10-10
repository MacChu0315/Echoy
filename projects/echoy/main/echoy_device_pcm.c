#include "echoy_device_pcm.h"
#include <math.h>
bool echoy_device_pcm_convert(const float *source, int16_t *out, size_t frames, size_t *clipped)
{
    if (!source || !out || !clipped) {
        return false;
    }
    *clipped = 0;
    for (size_t i = 0; i < frames; ++i) {
        float value = source[i] * ECHOY_DEVICE_PCM_GAIN;
        if (!isfinite(value)) {
            return false;
        }
        if (value > 1 || value < -1) {
            ++*clipped;
        }
        out[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, value)) * 32767);
    }
    return true;
}
