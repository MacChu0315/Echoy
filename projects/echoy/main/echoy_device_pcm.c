#include "echoy_device_pcm.h"
#include <math.h>
#include <string.h>

const float echoy_device_role_gains[4] = {1.0f, 1.0f, .6309573445f, 1.188502227f};

void echoy_device_pcm_reset(echoy_device_pcm_t *state)
{
    memset(state, 0, sizeof(*state));
    state->envelope = 1;
    for (unsigned i = 0; i < ECHOY_DEVICE_PCM_LOOKAHEAD; ++i) {
        state->gains[i] = 1;
    }
}

bool echoy_device_pcm_convert(echoy_device_pcm_t *state, const float *source, int16_t *out,
                              size_t frames, echoy_device_pcm_stats_t *stats)
{
    if (!state || !source || !out || !stats) {
        return false;
    }
    *stats = (echoy_device_pcm_stats_t){0};
    /* Reject before consuming anything, so an invalid block cannot leave a tail. */
    for (size_t i = 0; i < frames; ++i) {
        if (!isfinite(source[i] * ECHOY_DEVICE_PCM_GAIN)) {
            return false;
        }
    }
    for (size_t i = 0; i < frames; ++i) {
        state->samples[state->sample_at] = source[i] * ECHOY_DEVICE_PCM_GAIN;
        state->sample_at = (state->sample_at + 1) % (ECHOY_DEVICE_PCM_LOOKAHEAD + 1);
        float peak = 0;
        for (unsigned j = 0; j <= ECHOY_DEVICE_PCM_LOOKAHEAD; ++j) {
            peak = fmaxf(peak, fabsf(state->samples[j]));
        }
        float desired = peak > ECHOY_DEVICE_PCM_CEILING ? ECHOY_DEVICE_PCM_CEILING / peak : 1;
        /* 1-exp(-1/(24000*.080)): 80 ms release time constant, no runtime exp. */
        state->envelope = fminf(desired, state->envelope + (1 - state->envelope) * .0005206977f);
        state->gains[state->gain_at] = state->envelope;
        state->gain_at = (state->gain_at + 1) % ECHOY_DEVICE_PCM_LOOKAHEAD;
        float gain = 0;
        for (unsigned j = 0; j < ECHOY_DEVICE_PCM_LOOKAHEAD; ++j) {
            gain += state->gains[j];
        }
        gain /= ECHOY_DEVICE_PCM_LOOKAHEAD;
        float delayed = state->samples[state->sample_at];
        /* Every gain in the smoothing window saw this delayed sample. The
         * extra bound protects the ceiling against floating-point rounding. */
        if (fabsf(delayed) > ECHOY_DEVICE_PCM_CEILING) {
            gain = fminf(gain, ECHOY_DEVICE_PCM_CEILING / fabsf(delayed));
        }
        float value = delayed * gain;
        if (delayed != 0 && gain < .99999f) {
            ++stats->limited;
        }
        if (value > 1 || value < -1) {
            ++stats->clipped;
        }
        out[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, value)) * 32767);
    }
    return true;
}
