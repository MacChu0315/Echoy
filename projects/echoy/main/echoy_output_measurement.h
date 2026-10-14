#ifndef ECHOY_OUTPUT_MEASUREMENT_H
#define ECHOY_OUTPUT_MEASUREMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECHOY_OUTPUT_MEASUREMENT_SAMPLE_RATE 24000u
#define ECHOY_OUTPUT_MEASUREMENT_TOTAL_FRAMES 252000u
#define ECHOY_OUTPUT_MEASUREMENT_TONE_COUNT 15u
#define ECHOY_OUTPUT_MEASUREMENT_EXCERPT_FRAMES 256u
#define ECHOY_OUTPUT_MEASUREMENT_MAX_RUNS 2u
#define ECHOY_OUTPUT_MEASUREMENT_HARD_LIMIT_MS 12000u
#define ECHOY_OUTPUT_MEASUREMENT_LEAD_FRAMES 48000u
#define ECHOY_OUTPUT_MEASUREMENT_TONE_FRAMES 8400u
#define ECHOY_OUTPUT_MEASUREMENT_GAP_FRAMES 4800u
#define ECHOY_OUTPUT_MEASUREMENT_FADE_FRAMES 480u
#define ECHOY_OUTPUT_MEASUREMENT_WINDOW_OFFSET 1800u
#define ECHOY_OUTPUT_MEASUREMENT_WINDOW_FRAMES 4800u

typedef enum {
    ECHOY_OUTPUT_MEASUREMENT_DISABLED = 0,
    ECHOY_OUTPUT_MEASUREMENT_IDLE,
    ECHOY_OUTPUT_MEASUREMENT_STARTING,
    ECHOY_OUTPUT_MEASUREMENT_CAPTURING,
    ECHOY_OUTPUT_MEASUREMENT_STOPPING,
    ECHOY_OUTPUT_MEASUREMENT_ANALYZING,
    ECHOY_OUTPUT_MEASUREMENT_COMPLETE,
    ECHOY_OUTPUT_MEASUREMENT_CANCELLED,
    ECHOY_OUTPUT_MEASUREMENT_FAILED,
} echoy_output_measurement_phase_t;

typedef struct {
    echoy_output_measurement_phase_t phase;
    uint32_t generation;
    uint32_t requested_generation;
    uint32_t runs_started;
    uint32_t runs_remaining;
    uint32_t frames_written;
    uint32_t frames_captured;
    uint64_t elapsed_us;
    float input_gain_db;
    int error_code;
    bool result_ready;
} echoy_output_measurement_status_t;

enum {
    ECHOY_OUTPUT_MEASUREMENT_WINDOW_VALID = 1u << 0,
    ECHOY_OUTPUT_MEASUREMENT_ADC_CLIPPED = 1u << 1,
    ECHOY_OUTPUT_MEASUREMENT_ADC_NEAR_CLIP = 1u << 2,
    ECHOY_OUTPUT_MEASUREMENT_NOISE_VALID = 1u << 3,
    ECHOY_OUTPUT_MEASUREMENT_FUNDAMENTAL_VALID = 1u << 4,
    ECHOY_OUTPUT_MEASUREMENT_LOW_SNR = 1u << 5,
    ECHOY_OUTPUT_MEASUREMENT_INSUFFICIENT_SAMPLES = 1u << 6,
    ECHOY_OUTPUT_MEASUREMENT_ANALYSIS_CANCELLED = 1u << 7,
    ECHOY_OUTPUT_MEASUREMENT_ANALYSIS_COMPLETE = 1u << 8,
    /* Ratios are usable only with a complete, unclipped, >=20 dB SNR window.
       This validity flag does not identify the speaker as the distortion source. */
    ECHOY_OUTPUT_MEASUREMENT_RATIOS_VALID = 1u << 9,
};

typedef struct {
    uint32_t flags;
    uint32_t sample_count;
    uint32_t clipped;
    uint32_t near_clip;
    float adc_peak;
    float rms;
    float dc;
    float ac_rms;
} echoy_output_measurement_noise_t;

typedef struct {
    uint32_t flags;
    uint32_t window_start;
    uint32_t sample_count;
    uint32_t clipped;
    uint32_t near_clip;
    uint16_t frequency_hz;
    uint16_t excerpt_count;
    float pcm_peak;
    float adc_peak;
    float rms;
    float dc;
    /* Peak amplitudes, normalized by 32768; indices 0..4 are H1..H5. */
    float harmonic_amplitude[5];
    /* H2..H5 / H1, and sqrt(H2^2+...+H5^2) / H1. */
    float harmonic_ratio[4];
    float harmonic_rss_ratio;
    float fundamental_snr_db;
    /* Central raw ADC excerpt; export only on explicit request. */
    int16_t excerpt[ECHOY_OUTPUT_MEASUREMENT_EXCERPT_FRAMES];
} echoy_output_measurement_tone_t;

typedef struct {
    uint32_t flags;
    uint32_t frames_captured;
    uint32_t tones_analyzed;
    uint32_t capture_clipped;
    uint32_t capture_near_clip;
    float capture_adc_peak;
    echoy_output_measurement_noise_t noise;
    echoy_output_measurement_tone_t tones[ECHOY_OUTPUT_MEASUREMENT_TONE_COUNT];
} echoy_output_measurement_result_t;

typedef bool (*echoy_output_measurement_cancel_fn)(void *context);

/* Settled 10.5 s mono16 sequence: 2 s quiet, 15*(350 ms tone + 200 ms
   quiet), 0.25 s quiet. Frequencies 220/330/440/660/1000 Hz each progress
   through PCM peaks .12/.24/.36. The 20 ms fades have zero endpoints.
   The first physical run used the historical 9 s / 0.5 s lead profile;
   this diagnostic-only revision allows more settling without changing tones.
   Any offset/block size is supported; positions beyond the end are zero.
   No heap allocation, SDK, I/O, or per-sample transcendental operations. */
void echoy_output_measurement_render(size_t offset, int16_t *out, size_t frames);

/* Caller owns captured PCM and result storage (both may reside in PSRAM).
   Call only after RX is closed, from a non-audio worker. Cancellation is
   checked every 4096 frames during headroom scanning, before the noise window,
   and before each tone. Returns false on invalid
   arguments, incomplete capture, or cancellation; flags retain the reason.
   An ADC-clipped or low-SNR capture can finish analysis but has invalid ratios.
   Noise window: 1200..1800 ms; tone windows: 75..275 ms after nominal onset.
   TX/RX must share a clock and stream origin. The window tolerates constant
   codec/DMA delay up to +/-55 ms without intersecting the 20 ms fades.
   This is relative output-chain evidence, not calibrated SPL or speaker THD:
   ambient noise and microphone-path nonlinearity remain part of the result. */
bool echoy_output_measurement_analyze(
    const int16_t *captured, size_t frames,
    echoy_output_measurement_result_t *result,
    echoy_output_measurement_cancel_fn should_cancel, void *context);

#ifdef __cplusplus
}
#endif
#endif
