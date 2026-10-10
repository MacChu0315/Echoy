#include "echoy_device_pcm.h"
#include <float.h>
#include <math.h>
#include <string.h>

/* These trims match an approximately +3 dB default-volume increase across
 * roles after the 120 Hz filter and stronger compression, relative to the
 * .60-ceiling/gain6 listening trial. Physical acceptance is still required. */
const float echoy_device_role_gains[4] = {1.6f, .52f, .0317f, 2.38f};

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
        /* Retain arithmetic headroom for the filter's feedforward/feedback
         * sums, including finite inputs near float overflow. */
        if (!isfinite(source[i]) || fabsf(source[i]) > FLT_MAX / (ECHOY_DEVICE_PCM_GAIN * 16)) {
            return false;
        }
    }
    for (size_t i = 0; i < frames; ++i) {
        /* 120 Hz Butterworth high-pass at 24 kHz, bilinear transform, Q=1/sqrt(2).
         * Only the device output changes; state spans blocks and reset clears
         * the filter together with the limiter on pause/fault/reopen. */
        float x = source[i];
        float filtered = .9780304792f * x - 1.9560609584f * state->hp_x1 +
                         .9780304792f * state->hp_x2 + 1.9555782403f * state->hp_y1 -
                         .9565436765f * state->hp_y2;
        state->hp_x2 = state->hp_x1;
        state->hp_x1 = x;
        state->hp_y2 = state->hp_y1;
        state->hp_y1 = filtered;
        state->samples[state->sample_at] = filtered * ECHOY_DEVICE_PCM_GAIN;
        state->sample_at = (state->sample_at + 1) % (ECHOY_DEVICE_PCM_LOOKAHEAD + 1);
        float peak = 0;
        for (unsigned j = 0; j <= ECHOY_DEVICE_PCM_LOOKAHEAD; ++j) {
            peak = fmaxf(peak, fabsf(state->samples[j]));
        }
        float desired = peak > ECHOY_DEVICE_PCM_CEILING ? ECHOY_DEVICE_PCM_CEILING / peak : 1;
        /* 1-exp(-1/(24000*.040)): 40 ms release time constant, no runtime exp. */
        state->envelope = fminf(desired, state->envelope + (1 - state->envelope) * .0010411242f);
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
