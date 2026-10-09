#include "echoy_audio_probe.h"
void echoy_audio_probe_render(size_t offset, int16_t *out, size_t frames)
{
    static const int16_t sine[24] = {0,     2120,  4096,  5793,  7094,  7913,  8192,  7913,
                                     7094,  5793,  4096,  2120,  0,     -2120, -4096, -5793,
                                     -7094, -7913, -8192, -7913, -7094, -5793, -4096, -2120};
    for (size_t i = 0; i < frames; ++i) {
        if (offset >= ECHOY_PROBE_FRAMES || i >= ECHOY_PROBE_FRAMES - offset) {
            out[i] = 0;
            continue;
        }
        size_t at = offset + i;
        size_t ramp = at < 480 ? at : 480;
        if (ECHOY_PROBE_FRAMES - 1 - at < ramp) {
            ramp = ECHOY_PROBE_FRAMES - 1 - at;
        }
        out[i] = (int16_t)((int32_t)sine[at % 24] * (int32_t)ramp / 480);
    }
}
