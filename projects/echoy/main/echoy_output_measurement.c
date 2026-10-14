#include "echoy_output_measurement.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define PERIOD 2400u
#define QUARTER 600u
#define STRIDE (ECHOY_OUTPUT_MEASUREMENT_TONE_FRAMES + ECHOY_OUTPUT_MEASUREMENT_GAP_FRAMES)
#define NOISE_START 28800u
#define NOISE_FRAMES 14400u
#define NEAR_CLIP 32113
#define LSB (1.0 / 32768.0)

static const uint16_t frequencies[5] = {220, 330, 440, 660, 1000};
static const int16_t quarter_sine[QUARTER + 1] = {
#include "echoy_output_measurement_sine.inc"
};

_Static_assert(sizeof(echoy_output_measurement_result_t) < 10240,
               "Keep the compact result below 10 KiB");
_Static_assert(ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES ==
               ECHOY_OUTPUT_MEASUREMENT_LEAD_FRAMES +
               ECHOY_OUTPUT_MEASUREMENT_TONE_COUNT * STRIDE + 6000,
               "The complete stimulus must last exactly 10.5 seconds");

/* The caller supplies a phase in [0,2399]. Odd symmetry gives exactly zero
   mean, including when the phase increment visits only part of the table. */
static int32_t sine_at(unsigned phase)
{
    if (phase <= QUARTER) return quarter_sine[phase];
    if (phase <= 2 * QUARTER) return quarter_sine[2 * QUARTER - phase];
    if (phase <= 3 * QUARTER) return -quarter_sine[phase - 2 * QUARTER];
    return -quarter_sine[4 * QUARTER - phase];
}

void echoy_output_measurement_render(size_t offset, int16_t *out, size_t frames)
{
    if (!out) return;
    while (frames) {
        /* Check before addition, including callers near SIZE_MAX. */
        if (offset >= ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES) {
            while (frames--) *out++ = 0;
            return;
        }
        size_t count;
        if (offset < ECHOY_OUTPUT_MEASUREMENT_LEAD_FRAMES) {
            count = ECHOY_OUTPUT_MEASUREMENT_LEAD_FRAMES - offset;
            if (count > frames) count = frames;
            memset(out, 0, count * sizeof(*out));
        } else {
            const size_t position = offset - ECHOY_OUTPUT_MEASUREMENT_LEAD_FRAMES;
            const unsigned tone = (unsigned)(position / STRIDE);
            unsigned local = (unsigned)(position % STRIDE);
            if (tone >= ECHOY_OUTPUT_MEASUREMENT_TONE_COUNT ||
                local >= ECHOY_OUTPUT_MEASUREMENT_TONE_FRAMES) {
                count = tone >= ECHOY_OUTPUT_MEASUREMENT_TONE_COUNT ?
                    ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES - offset : STRIDE - local;
                if (count > frames) count = frames;
                memset(out, 0, count * sizeof(*out));
            } else {
                count = ECHOY_OUTPUT_MEASUREMENT_TONE_FRAMES - local;
                if (count > frames) count = frames;
                const unsigned step = frequencies[tone / 3] / 10;
                const int32_t percent = 12 * (int32_t)(tone % 3 + 1);
                unsigned phase = local * step % PERIOD;
                for (size_t i = 0; i < count; ++i, ++local) {
                    unsigned fade = ECHOY_OUTPUT_MEASUREMENT_FADE_FRAMES;
                    if (local < fade) fade = local;
                    unsigned remaining = ECHOY_OUTPUT_MEASUREMENT_TONE_FRAMES - 1 - local;
                    if (remaining < fade) fade = remaining;
                    /* Maximum numerator 32767*36*480 fits signed int32. */
                    out[i] = (int16_t)(sine_at(phase) * percent * (int32_t)fade /
                        (100 * (int32_t)ECHOY_OUTPUT_MEASUREMENT_FADE_FRAMES));
                    phase += step;
                    if (phase >= PERIOD) phase -= PERIOD;
                }
            }
        }
        offset += count;
        out += count;
        frames -= count;
    }
}

typedef struct {
    int64_t sum;
    uint64_t squares;
    uint32_t peak;
    uint32_t clipped;
    uint32_t near_clip;
} sample_stats_t;

static void add_sample(sample_stats_t *stats, int16_t sample)
{
    const int32_t value = sample;
    const uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
    stats->sum += value;
    stats->squares += (uint64_t)((int64_t)value * value);
    if (magnitude > stats->peak) stats->peak = magnitude;
    if (value == INT16_MIN || value == INT16_MAX) ++stats->clipped;
    if (magnitude >= NEAR_CLIP) ++stats->near_clip;
}

static uint32_t headroom_flags(const sample_stats_t *stats)
{
    return (stats->clipped ? ECHOY_OUTPUT_MEASUREMENT_ADC_CLIPPED : 0u) |
           (stats->near_clip ? ECHOY_OUTPUT_MEASUREMENT_ADC_NEAR_CLIP : 0u);
}

static bool cancelled(echoy_output_measurement_result_t *result,
                      echoy_output_measurement_cancel_fn should_cancel, void *context)
{
    if (!should_cancel || !should_cancel(context)) return false;
    result->flags |= ECHOY_OUTPUT_MEASUREMENT_ANALYSIS_CANCELLED;
    return true;
}

static void analyze_noise(const int16_t *captured, size_t frames,
                          echoy_output_measurement_noise_t *noise)
{
    if (frames < NOISE_START + NOISE_FRAMES) {
        noise->flags = ECHOY_OUTPUT_MEASUREMENT_INSUFFICIENT_SAMPLES;
        return;
    }
    sample_stats_t stats = {0};
    for (size_t i = NOISE_START; i < NOISE_START + NOISE_FRAMES; ++i)
        add_sample(&stats, captured[i]);
    const double dc = stats.sum * LSB / NOISE_FRAMES;
    const double mean_square = stats.squares * LSB * LSB / NOISE_FRAMES;
    noise->flags = ECHOY_OUTPUT_MEASUREMENT_WINDOW_VALID | headroom_flags(&stats);
    if (!stats.clipped && !stats.near_clip) noise->flags |= ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID;
    noise->sample_count = NOISE_FRAMES;
    noise->clipped = stats.clipped;
    noise->near_clip = stats.near_clip;
    noise->adc_peak = (float)(stats.peak * LSB);
    noise->dc = (float)dc;
    noise->rms = (float)sqrt(mean_square);
    noise->ac_rms = (float)sqrt(fmax(0, mean_square - dc * dc));
}

static void analyze_tone(const int16_t *captured, size_t frames, unsigned index,
                         echoy_output_measurement_result_t *result)
{
    echoy_output_measurement_tone_t *tone = &result->tones[index];
    tone->frequency_hz = frequencies[index / 3];
    tone->pcm_peak = .12f * (float)(index % 3 + 1);
    tone->window_start = ECHOY_OUTPUT_MEASUREMENT_LEAD_FRAMES + index * STRIDE +
                         ECHOY_OUTPUT_MEASUREMENT_WINDOW_OFFSET;
    if (frames < tone->window_start + ECHOY_OUTPUT_MEASUREMENT_WINDOW_FRAMES) {
        tone->flags = ECHOY_OUTPUT_MEASUREMENT_INSUFFICIENT_SAMPLES;
        return;
    }
    const size_t count = ECHOY_OUTPUT_MEASUREMENT_WINDOW_FRAMES;
    sample_stats_t stats = {0};
    int64_t sine_sum[5] = {0}, cosine_sum[5] = {0};
    unsigned phase[5] = {0};
    const unsigned base_step = tone->frequency_hz / 10;
    for (size_t i = 0; i < count; ++i) {
        int32_t sample = captured[tone->window_start + i];
        add_sample(&stats, (int16_t)sample);
        for (unsigned h = 0; h < 5; ++h) {
            unsigned cosine_phase = phase[h] + QUARTER;
            if (cosine_phase >= PERIOD) cosine_phase -= PERIOD;
            sine_sum[h] += (int64_t)sample * sine_at(phase[h]);
            cosine_sum[h] += (int64_t)sample * sine_at(cosine_phase);
            phase[h] += base_step * (h + 1);
            if (phase[h] >= PERIOD) phase[h] -= PERIOD;
        }
    }
    tone->sample_count = (uint32_t)count;
    tone->flags = ECHOY_OUTPUT_MEASUREMENT_WINDOW_VALID | headroom_flags(&stats);
    tone->clipped = stats.clipped;
    tone->near_clip = stats.near_clip;
    tone->adc_peak = (float)(stats.peak * LSB);
    tone->dc = (float)(stats.sum * LSB / count);
    tone->rms = (float)sqrt(stats.squares * LSB * LSB / count);
    for (unsigned h = 0; h < 5; ++h) {
        tone->harmonic_amplitude[h] = (float)(2 * hypot((double)sine_sum[h], (double)cosine_sum[h]) *
            LSB / (count * 32767.0));
    }
    const double fundamental = tone->harmonic_amplitude[0];
    const double noise_rms = fmax(result->noise.ac_rms, LSB);
    /* Finite values make diagnostic serialization unambiguous even for silence.
       One ADC LSB is a conservative floor, not a measured analog noise floor. */
    tone->fundamental_snr_db = (float)(20 * log10(fmax(fundamental / sqrt(2.0), LSB * 1e-6) / noise_rms));
    if (result->noise.flags & ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID)
        tone->flags |= ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID;
    if (tone->fundamental_snr_db < 20 || !(tone->flags & ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID))
        tone->flags |= ECHOY_OUTPUT_MEASUREMENT_LOW_SNR;
    if (fundamental > LSB) {
        tone->flags |= ECHOY_OUTPUT_MEASUREMENT_FUNDAMENTAL_VALID;
        double ratio_squares = 0;
        for (unsigned h = 1; h < 5; ++h) {
            const double ratio = tone->harmonic_amplitude[h] / fundamental;
            tone->harmonic_ratio[h - 1] = (float)ratio;
            ratio_squares += ratio * ratio;
        }
        tone->harmonic_rss_ratio = (float)sqrt(ratio_squares);
        if (!(tone->flags & ECHOY_OUTPUT_MEASUREMENT_LOW_SNR) &&
            !result->capture_near_clip && !result->capture_clipped)
            tone->flags |= ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID;
    }
    tone->excerpt_count = ECHOY_OUTPUT_MEASUREMENT_EXCERPT_FRAMES;
    const size_t excerpt_start = tone->window_start + (count - tone->excerpt_count) / 2;
    memcpy(tone->excerpt, captured + excerpt_start, sizeof(tone->excerpt));
    ++result->tones_analyzed;
}

bool echoy_output_measurement_analyze(
    const int16_t *captured, size_t frames, echoy_output_measurement_result_t *result,
    echoy_output_measurement_cancel_fn should_cancel, void *context)
{
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    if (!captured) frames = 0;
    if (frames > ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES) frames = ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES;
    result->frames_captured = (uint32_t)frames;
    if (frames < ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES)
        result->flags |= ECHOY_OUTPUT_MEASUREMENT_INSUFFICIENT_SAMPLES;
    sample_stats_t capture_stats = {0};
    for (size_t i = 0; i < frames; ++i) {
        if (i % 4096 == 0 && cancelled(result, should_cancel, context)) return false;
        add_sample(&capture_stats, captured[i]);
    }
    result->capture_adc_peak = (float)(capture_stats.peak * LSB);
    result->capture_clipped = capture_stats.clipped;
    result->capture_near_clip = capture_stats.near_clip;
    result->flags |= headroom_flags(&capture_stats);
    if (cancelled(result, should_cancel, context)) return false;
    analyze_noise(captured, frames, &result->noise);
    if (result->noise.flags & ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID)
        result->flags |= ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID;
    for (unsigned i = 0; i < ECHOY_OUTPUT_MEASUREMENT_TONE_COUNT; ++i) {
        if (cancelled(result, should_cancel, context)) return false;
        analyze_tone(captured, frames, i, result);
    }
    if (result->flags & ECHOY_OUTPUT_MEASUREMENT_INSUFFICIENT_SAMPLES) return false;
    result->flags |= ECHOY_OUTPUT_MEASUREMENT_ANALYSIS_COMPLETE;
    return true;
}
