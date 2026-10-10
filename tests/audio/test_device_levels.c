#include "echoy_audio.h"
#include "echoy_device_pcm.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FRAMES 60000
static float input[FRAMES + ECHOY_DEVICE_PCM_LOOKAHEAD];
static int16_t output[FRAMES + ECHOY_DEVICE_PCM_LOOKAHEAD];
static double convert(double *peak, size_t *limited)
{
    echoy_device_pcm_t state;
    echoy_device_pcm_reset(&state);
    for (unsigned i = FRAMES; i < sizeof(input) / sizeof(*input); i++) {
        input[i] = 0;
    }
    double energy = 0;
    *limited = 0;
    for (unsigned at = 0; at < sizeof(input) / sizeof(*input); at += 240) {
        unsigned n = sizeof(input) / sizeof(*input) - at;
        if (n > 240) {
            n = 240;
        }
        echoy_device_pcm_stats_t stats;
        assert(echoy_device_pcm_convert(&state, input + at, output + at, n, &stats));
        assert(stats.clipped == 0);
        *limited += stats.limited;
        for (unsigned i = at; i < at + n; i++) {
            assert(abs(output[i]) <= 19661);
            double value = output[i] / 32767.0;
            energy += value * value;
            if (fabs(value) > *peak) {
                *peak = fabs(value);
            }
        }
    }
    return sqrt(energy / FRAMES);
}

static void harmony_speaker_headroom(void)
{
    /* The observed 40aa answer distorts on the board at .64 UI volume even
     * without digital limiting; the listening-selected clean peak was ~.65.
     * Leave headroom below that observation, including at maximum UI volume. */
    echoy_plan_t plan;
    assert(echoy_compose_mask(0x40aa, ECHOY_HARMONY, 3, .5, &plan));
    echoy_audio_loops_t *audio = NULL;
    assert(echoy_audio_prepare(&plan, false, &audio) == ECHOY_AUDIO_OK);
    const double volumes[] = {.05, .49, .65, 1};
    for (unsigned v = 0; v < sizeof(volumes) / sizeof(*volumes); ++v) {
        for (unsigned i = 0; i < FRAMES; ++i) {
            float bank = audio->samples[ECHOY_AUDIO_FULL][i] *
                         echoy_device_role_gains[ECHOY_HARMONY];
            input[i] = (float)(bank * volumes[v] * .45);
        }
        double peak = 0;
        size_t limited = 0;
        double rms = convert(&peak, &limited);
        assert(peak <= .60002 && rms > .005);
        assert(v == 0 ? limited == 0 : limited > 0);
    }
    echoy_audio_free_loops(audio);
    puts("PASS calibrated Harmony 40aa respects speaker headroom across UI volumes");
}

static void louder_balanced_speaker_profile(void)
{
    /* Captured from the installed .60-ceiling/gain6 profile before voicing,
     * for the user's actual 8234 seed at .65 volume and .5 density. */
    const double previous_rms[] = {.076304862, .078975005, .005302474, .170780880};
    for (unsigned role = 0; role < 4; ++role) {
        echoy_plan_t plan;
        assert(echoy_compose_mask(0x8234, role, role == ECHOY_HARMONY ? 3 : 1, .5, &plan));
        echoy_audio_loops_t *audio = NULL;
        assert(echoy_audio_prepare(&plan, role == ECHOY_PULSE, &audio) == ECHOY_AUDIO_OK);
        for (unsigned i = 0; i < FRAMES; ++i) {
            float bank = audio->samples[ECHOY_AUDIO_FULL][i] * echoy_device_role_gains[role];
            input[i] = (float)(bank * .65 * .45);
        }
        double peak = 0;
        size_t limited = 0;
        double change_db = 20 * log10(convert(&peak, &limited) / previous_rms[role]);
        assert(peak <= .60002 && change_db >= 2.5 && change_db <= 5);
        echoy_audio_free_loops(audio);
    }
    puts("PASS all four roles gain average level without increasing the speaker peak ceiling");
}

int main(void)
{
    harmony_speaker_headroom();
    louder_balanced_speaker_profile();
    const unsigned seeds[] = {0x0890, 0xffff, 0x5555, 1};
    echoy_audio_buffer_t *tap = NULL;
    assert(echoy_audio_render_cue(ECHOY_AUDIO_TAP, &tap) == ECHOY_AUDIO_OK);
    double peak = 0, mixed_peak = 0, rms_min = 1, rms_max = 0, boost_min = 100, boost_max = 0;
    unsigned loops = 0, mixtures = 0, limited_loops = 0;
    FILE *csv = NULL;
    const char *path = getenv("ECHOY_LEVEL_CSV");
    if (path) {
        csv = fopen(path, "w");
        assert(csv);
        fprintf(csv, "seed,role,phrase,density,gain3_rms,limited_rms,boost_db,limited_samples\n");
    }
    for (unsigned seed = 0; seed < 4; ++seed) {
        for (unsigned role = 0; role < 4; ++role) {
            for (unsigned phrase = 1; phrase <= 3; ++phrase) {
                for (unsigned density = 0; density < 3; ++density) {
                    echoy_plan_t plan;
                    assert(echoy_compose_mask(seeds[seed], role, phrase, density * .5, &plan));
                    echoy_audio_loops_t *audio = NULL;
                    assert(echoy_audio_prepare(&plan, role == 0 && phrase == 1, &audio) ==
                           ECHOY_AUDIO_OK);
                    assert(audio->frames == FRAMES);
                    double baseline_energy = 0;
                    for (unsigned i = 0; i < FRAMES; i++) {
                        input[i] = audio->samples[ECHOY_AUDIO_FULL][i] * .45f;
                        baseline_energy += input[i] * (double)input[i] * 9;
                    }
                    size_t limited;
                    double rms = convert(&peak, &limited),
                           baseline = sqrt(baseline_energy / FRAMES);
                    double boost = 20 * log10(rms / baseline);
                    assert(rms >
                           baseline); /* Loudness must improve over the installed gain-3 build. */
                    if (rms < rms_min) {
                        rms_min = rms;
                    }
                    if (rms > rms_max) {
                        rms_max = rms;
                    }
                    if (boost < boost_min) {
                        boost_min = boost;
                    }
                    if (boost > boost_max) {
                        boost_max = boost;
                    }
                    if (limited) {
                        ++limited_loops;
                    }
                    if (csv) {
                        fprintf(csv, "%04x,%u,%u,%.1f,%.9f,%.9f,%.6f,%zu\n", seeds[seed], role,
                                phrase, density * .5, baseline, rms, boost, limited);
                    }
                    ++loops;
                    if (density != 1) {
                        for (unsigned offset = 0; offset < FRAMES; offset += 2500) {
                            for (unsigned i = 0; i < FRAMES; ++i) {
                                double sample = audio->samples[ECHOY_AUDIO_FULL][i];
                                unsigned t = (i + offset) % FRAMES;
                                for (size_t k = t % 2400; k < tap->frames; k += 2400) {
                                    sample += tap->samples[k];
                                }
                                input[i] = (float)(sample * .45);
                            }
                            convert(&mixed_peak, &limited);
                            ++mixtures;
                        }
                    }
                    echoy_audio_free_loops(audio);
                }
            }
        }
    }
    if (csv) {
        assert(fclose(csv) == 0);
    }
    echoy_audio_free_buffer(tap);
    assert(loops == 144 && mixtures == 2304);
    printf("PASS loops=%u tap_mixtures=%u output_rms=[%.9f,%.9f] gain_over_3x_db=[%.6f,%.6f] "
           "peak=%.9f mixed_peak=%.9f limited_loops=%u no_clipping\n",
           loops, mixtures, rms_min, rms_max, boost_min, boost_max, peak, mixed_peak,
           limited_loops);
}
