#include "echoy_audio.h"
#include "echoy_device_pcm.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    const unsigned seeds[] = {0x0890, 0xffff, 0x5555, 1};
    echoy_audio_buffer_t *tap = NULL;
    assert(echoy_audio_render_cue(ECHOY_AUDIO_TAP, &tap) == ECHOY_AUDIO_OK);
    double maximum = 0, mixed_maximum = 0, rms_min = 1, rms_max = 0;
    unsigned loops = 0, mixtures = 0;
    for (unsigned seed = 0; seed < 4; ++seed) {
        for (unsigned role = 0; role < 4; ++role) {
            for (unsigned phrase = 1; phrase <= 3; ++phrase) {
                for (unsigned density = 0; density < 3; ++density) {
                    echoy_plan_t plan;
                    assert(echoy_compose_mask(seeds[seed], role, phrase, density * .5, &plan));
                    echoy_audio_loops_t *audio = NULL;
                    assert(echoy_audio_prepare(&plan, role == 0 && phrase == 1, &audio) == ECHOY_AUDIO_OK);
                    double energy = 0;
                    for (size_t at = 0; at < audio->frames; at += 240) {
                        float source[240]; int16_t pcm[240]; size_t clipped;
                        for (unsigned i = 0; i < 240; ++i) {
                            /* One physical tile at maximum UI volume, through
                             * the unchanged shared mixer's .45 master gain. */
                            source[i] = audio->samples[ECHOY_AUDIO_FULL][at+i] * .45f;
                            energy += source[i] * (double)source[i];
                            if (fabs(source[i]) > maximum) maximum = fabs(source[i]);
                        }
                        assert(echoy_device_pcm_convert(source, pcm, 240, &clipped) && clipped == 0);
                    }
                    double rms = sqrt(energy / audio->frames);
                    if (rms < rms_min) rms_min = rms;
                    if (rms > rms_max) rms_max = rms;
                    ++loops;
                    if (density != 1) {
                        /* Stress a steady ten taps/second over the playing
                         * phrase, including prior-bar tails, at 24 offsets. */
                        for (unsigned offset = 0; offset < 60000; offset += 2500) {
                            for (unsigned i = 0; i < 60000; ++i) {
                                double sample = audio->samples[ECHOY_AUDIO_FULL][i];
                                unsigned t = (i + offset) % 60000;
                                for (size_t k = t % 2400; k < tap->frames; k += 2400)
                                    sample += tap->samples[k];
                                sample = fabs(sample * .45);
                                if (sample > mixed_maximum) mixed_maximum = sample;
                            }
                            ++mixtures;
                        }
                    }
                    echoy_audio_free_loops(audio);
                }
            }
        }
    }
    echoy_audio_free_buffer(tap);
    float worst = (float)mixed_maximum; int16_t pcm; size_t clipped;
    assert(echoy_device_pcm_convert(&worst, &pcm, 1, &clipped) && clipped == 0);
    assert(loops == 144 && mixtures == 2304);
    printf("PASS loops=%u tap_mixtures=%u baseline_rms=[%.9f,%.9f] calibrated_rms=[%.9f,%.9f] "
           "baseline_peak=%.9f calibrated_peak=%.9f calibrated_mix_peak=%.9f no_clipping\n",
           loops, mixtures, rms_min, rms_max, rms_min * ECHOY_DEVICE_PCM_GAIN,
           rms_max * ECHOY_DEVICE_PCM_GAIN, maximum, maximum * ECHOY_DEVICE_PCM_GAIN,
           mixed_maximum * ECHOY_DEVICE_PCM_GAIN);
}
