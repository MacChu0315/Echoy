#include "echoy_audio.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_log.h"
#endif

#define PI 3.14159265358979323846f
#define SR ((float)ECHOY_AUDIO_SAMPLE_RATE)
#define DELAY_FRAMES 7500u
#define TRIANGLE_SIZE 4096u
#define SINE_SIZE 1024u

typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2, q, resonance; unsigned type; } biquad_t;
typedef struct {
    float send[ECHOY_AUDIO_LOOP_FRAMES], delay[DELAY_FRAMES], feedback[128];
    float triangle[3][TRIANGLE_SIZE], sine[SINE_SIZE + 1];
    unsigned triangle_harmonics[3];
    echoy_note_t source[16], sparse[17];
} workspace_t;

static float min(float a, float b) { return a < b ? a : b; }
static float max(float a, float b) { return a > b ? a : b; }
static float midi(int note) { return 440 * exp2f((note - 69) / 12.0f); }

/* Linear interpolation at 1024 phases has <5e-6 absolute sine error. The
 * table belongs to each preparation job, with no shared mutable audio state. */
static void workspace_init(workspace_t *work)
{
    for (unsigned i = 0; i <= SINE_SIZE; ++i)
        work->sine[i] = sinf(2 * PI * i / SINE_SIZE);
    memset(work->triangle_harmonics, 0, sizeof(work->triangle_harmonics));
}

static float sine_sample(const float *table, float phase)
{
    float position = phase * (SINE_SIZE / (2 * PI));
    unsigned whole = (unsigned)position, index = whole & (SINE_SIZE - 1);
    float fraction = position - whole;
    return table[index] + (table[index + 1] - table[index]) * fraction;
}

typedef struct { uint32_t phase, increment; } oscillator_t;

static oscillator_t oscillator(float frequency)
{
    /* Unused upper partials may exceed the sample rate; wrap before converting
     * to the unsigned phase accumulator, including the legal MIDI 127 input. */
    frequency = fmodf(frequency, SR);
    return (oscillator_t){0, (uint32_t)(frequency * (4294967296.0f / SR))};
}

static float oscillator_sample(oscillator_t *osc, const float *table)
{
    unsigned index = osc->phase >> 22;
    float fraction = (osc->phase & 0x3fffffu) * (1.0f / 4194304);
    float value = table[index] + (table[index + 1] - table[index]) * fraction;
    osc->phase += osc->increment;
    return value;
}

/* Web Audio low/high-pass Q is in dB; band-pass Q is linear. */
static void filter_set(biquad_t *f, unsigned type, float frequency, float q, const float *sine)
{
    frequency = max(0, min(SR / 2, frequency));
    float w = 2 * PI * frequency / SR, cosine = sine_sample(sine, w + PI / 2);
    if (f->q != q || f->type != type) {
        f->q = q;
        f->type = type;
        f->resonance = 1 / (2 * (type == 2 ? q : powf(10, q / 20)));
    }
    float alpha = sine_sample(sine, w) * f->resonance;
    float a0 = 1 + alpha;
    f->a1 = -2 * cosine / a0;
    f->a2 = (1 - alpha) / a0;
    if (type == 0) {
        f->b0 = f->b2 = (1 - cosine) / (2 * a0);
        f->b1 = (1 - cosine) / a0;
    } else if (type == 1) {
        f->b0 = f->b2 = (1 + cosine) / (2 * a0);
        f->b1 = -(1 + cosine) / a0;
    } else {
        f->b0 = alpha / a0;
        f->b1 = 0;
        f->b2 = -alpha / a0;
    }
}

static float filter_sample(biquad_t *f, float input)
{
    float output = f->b0 * input + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = input;
    f->y2 = f->y1;
    f->y1 = output;
    return (float)output;
}

/* The public API accepts MIDI 0..127. A high-Q band-pass at very low
 * frequencies or near Nyquist is sensitive to float coefficient cancellation.
 * Keep the original double filter in that range; generated Dust pitches use
 * the fast float path. This state remains local to its note/preparation job. */
typedef struct { double b0, b2, a1, a2, x1, x2, y1, y2; } precise_bandpass_t;

static precise_bandpass_t precise_bandpass(int note)
{
    const double pi = 3.14159265358979323846;
    double frequency = fmin(ECHOY_AUDIO_SAMPLE_RATE / 2.0,
                            440 * exp2((note - 69) / 12.0) * 1.7);
    double w = 2 * pi * frequency / ECHOY_AUDIO_SAMPLE_RATE;
    double alpha = sin(w) / 11, a0 = 1 + alpha;
    return (precise_bandpass_t){.b0 = alpha / a0, .b2 = -alpha / a0,
                               .a1 = -2 * cos(w) / a0, .a2 = (1 - alpha) / a0};
}

static float precise_bandpass_sample(precise_bandpass_t *f, float input)
{
    double output = f->b0 * input + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = input;
    f->y2 = f->y1;
    f->y1 = output;
    return (float)output;
}

static uint32_t noise_at(unsigned offset)
{
    uint64_t factor = 16807, state = 1187;
    for (unsigned exponent = offset; exponent; exponent >>= 1) {
        if (exponent & 1) state = state * factor % 2147483647;
        factor = factor * factor % 2147483647;
    }
    return (uint32_t)state;
}

static float noise_next(uint32_t *state)
{
    *state = (uint64_t)*state * 16807 % 2147483647;
    return (float)(*state / 2147483647.0f * 2 - 1);
}

static float exponential(float start, float end, float position)
{
    return start * powf(end / start, max(0, min(1, position)));
}

/* Exponential automation advances by one multiplication per sample. Re-anchor
 * every 256 samples (and after discontinuities) to bound accumulated roundoff. */
typedef struct {
    float from, to, begin, end, logarithm, multiplier, value;
    int previous;
} ramp_t;

static ramp_t ramp(float from, float to, float begin, float duration)
{
    float logarithm = logf(to / from) / (duration * SR);
    return (ramp_t){from, to, begin * SR, (begin + duration) * SR,
                    logarithm, expf(logarithm), from, -2};
}

static float ramp_sample(ramp_t *curve, int frame)
{
    if (frame <= curve->begin) return curve->from;
    if (frame >= curve->end) return curve->to;
    if (frame != curve->previous + 1 || !(frame & 255))
        curve->value = curve->from * expf(curve->logarithm * (frame - curve->begin));
    else curve->value *= curve->multiplier;
    curve->previous = frame;
    return curve->value;
}

static void triangle_table(float *table, unsigned harmonics, const float *sine)
{
    for (unsigned i = 0; i < TRIANGLE_SIZE; ++i) {
        float phase = 2 * PI * i / TRIANGLE_SIZE;
        float previous = -sine_sample(sine, phase), current = -previous, value = 0;
        float recurrence = 2 * sine_sample(sine, 2 * phase + PI / 2), sign = 1;
        for (unsigned harmonic = 1; harmonic <= harmonics; harmonic += 2) {
            value += sign * current / ((float)harmonic * harmonic);
            float next = recurrence * current - previous;
            previous = current;
            current = next;
            sign = -sign;
        }
        table[i] = (float)(value * 8 / (PI * PI));
    }
}

static float triangle_sample(const float *table, uint32_t phase)
{
    unsigned index = phase >> 20;
    float fraction = (phase & 0xfffffu) * (1.0f / 1048576);
    return table[index] + (table[(index + 1) % TRIANGLE_SIZE] - table[index]) * fraction;
}

static void render_note(float *dry, float *send, size_t frames, const echoy_note_t *note,
                        workspace_t *work)
{
    double at = note->step * .15625;
    float duration = note->duration ? note->duration : .2f;
    const float velocity = (float)note->velocity;
    size_t start = (size_t)ceil(at * ECHOY_AUDIO_SAMPLE_RATE);
    float stop, phase[3] = {0}, frequencies[3] = {0};
    float total = 0, attack = 0, release = 0, hold = 0, peak = 0;
    bool short_chord = note->articulation == ECHOY_SHORT;
    bool answer = note->articulation == ECHOY_ANSWER;
    bool broken = note->articulation == ECHOY_BROKEN;
    unsigned pitches = note->note_count ? note->note_count : 1;
    bool pickup = answer && pitches == 1;
    biquad_t filter = {0};
    const bool precise_grain = note->kind == ECHOY_GRAIN && (note->note < 20 || note->note >= 114);
    precise_bandpass_t precise_filter = {0};
    unsigned noise_offset = note->kind == ECHOY_HAT ? 4800 : 15600;
    uint32_t noise = noise_at(noise_offset);
    if (note->kind == ECHOY_KICK) stop = .34f;
    else if (note->kind == ECHOY_RIM) stop = .13f;
    else if (note->kind == ECHOY_HAT) {
        stop = .08f;
        filter_set(&filter, 1, 6500, 1, work->sine);
    } else if (note->kind == ECHOY_TONE) stop = duration + .46f;
    else if (note->kind == ECHOY_GRAIN) {
        stop = duration + .23f;
        if (precise_grain) precise_filter = precise_bandpass(note->note);
        else filter_set(&filter, 2, midi(note->note) * 1.7f, 5.5f, work->sine);
    } else {
        total = max(short_chord || answer ? .025f : .07f, min(duration, note->max_duration ? note->max_duration : 2.45f));
        attack = min(total * .25f, broken ? .015f : short_chord ? .008f : answer ? (pickup ? .010f : .018f) : .075f);
        release = min(total * (short_chord ? .42f : answer ? .38f : .32f), broken ? .12f : short_chord ? .075f : answer ? .10f : .23f);
        hold = max(attack + .001f, total - release);
        peak = max(.0001f, velocity * (broken || pickup ? .26f : .32f) / sqrtf(pitches));
        stop = total + .01f;
        for (unsigned p = 0; p < pitches; ++p) {
            frequencies[p] = midi(note->note_count ? note->notes[p] : note->note);
            unsigned harmonics = (unsigned)((SR / 2 - 1) / frequencies[p]);
            if (work->triangle_harmonics[p] != harmonics) {
                triangle_table(work->triangle[p], harmonics, work->sine);
                work->triangle_harmonics[p] = harmonics;
            }
        }
    }
    static const float attacks[] = {.004f, .001f, .001f, .012f, .014f};
    static const float levels[] = {.72f, .34f, .16f, .29f, .3f};
    const float note_frequency = midi(note->note);
    oscillator_t oscillators[3];
    for (unsigned p = 0; p < 3; ++p) {
        static const float rim_frequencies[] = {238, 517, 1034};
        oscillators[p] = oscillator(note->kind == ECHOY_RIM ? rim_frequencies[p] :
                                    note->kind == ECHOY_CHORD ? frequencies[p] : note_frequency * (p + 1));
    }
    float sustain = peak * (broken ? .64f : short_chord ? .46f : answer ? .60f : .82f);
    ramp_t rise = {0}, fall = {0}, sweep = {0};
    if (note->kind != ECHOY_CHORD) {
        static const float releases[] = {.28f, .095f, .055f, .38f, .16f};
        attack = attacks[note->kind];
        peak = max(.0001f, velocity * levels[note->kind]);
        rise = ramp(.00001f, peak, 0, attack);
        fall = ramp(peak, .00001f, attack, releases[note->kind] + (note->kind >= ECHOY_TONE ? duration : 0));
        if (note->kind == ECHOY_KICK) sweep = ramp(128, 47, 0, .095f);
        if (note->kind == ECHOY_TONE) sweep = ramp(2400, 700, 0, .5f);
    } else {
        float begin = broken ? 2900 : short_chord ? 2700 : answer ? 2350 : 1750;
        float finish = broken ? 1700 : short_chord ? 1050 : answer ? 1250 : 1100;
        sweep = ramp(begin, finish, 0, total);
        fall = ramp(sustain, .00001f, hold, total - hold);
    }
    size_t end = (size_t)ceil((at + stop) * ECHOY_AUDIO_SAMPLE_RATE);
    if (end > frames) end = frames;
    const bool wet = note->kind == ECHOY_TONE || note->kind == ECHOY_GRAIN || note->kind == ECHOY_CHORD;
    for (size_t i = start; i < end; ++i) {
        float time = (i - start) / SR, value = 0, gain = 0;
        if (note->kind == ECHOY_KICK) {
            value = sine_sample(work->sine, phase[0]);
            /* Chromium's a-rate oscillator reads the first render quantum's
             * automation from its block start, including the pre-onset default
             * 440 Hz entries. Preserve that phase behavior for delayed kicks. */
            int frequency_frame = (int)(i - start);
            if (i / 128 == start / 128) frequency_frame -= start % 128;
            phase[0] += 2 * PI * (frequency_frame < 0 ? 440 : ramp_sample(&sweep, frequency_frame)) / SR;
            if (phase[0] >= 2 * PI) phase[0] -= 2 * PI;
        } else if (note->kind == ECHOY_RIM) {
            value = oscillator_sample(&oscillators[0], work->sine) * .7f + oscillator_sample(&oscillators[1], work->sine) * .23f + oscillator_sample(&oscillators[2], work->sine) * .1f;
        } else if (note->kind == ECHOY_HAT) {
            value = filter_sample(&filter, noise_next(&noise));
        } else if (note->kind == ECHOY_TONE) {
            float frequency = note_frequency;
            for (unsigned partial = 1; partial <= 3; ++partial) {
                static const float levels[] = {1, .22f, .07f};
                if (frequency * partial < SR / 2) value += oscillator_sample(&oscillators[partial - 1], work->sine) * levels[partial - 1];
            }
            filter_set(&filter, 0, ramp_sample(&sweep, i - start), .45f, work->sine);
            value = filter_sample(&filter, (float)value);
        } else if (note->kind == ECHOY_GRAIN) {
            float source = i - start + noise_offset < 48000 ? noise_next(&noise) : 0;
            value = (precise_grain ? precise_bandpass_sample(&precise_filter, source) : filter_sample(&filter, source)) +
                    oscillator_sample(&oscillators[0], work->sine) * .19f;
        } else {
            for (unsigned p = 0; p < pitches; ++p) {
                if (frequencies[p] < SR / 2) {
                    float triangle = triangle_sample(work->triangle[p], oscillators[p].phase);
                    value += oscillator_sample(&oscillators[p], work->sine) + triangle * (broken ? .15f : .22f);
                }
            }
            filter_set(&filter, 0, ramp_sample(&sweep, i - start), .48f, work->sine);
            value = filter_sample(&filter, (float)value);
            gain = time < attack ? .00001f + (peak - .00001f) * time / attack :
                   time < hold ? peak + (sustain - peak) * (time - attack) / (hold - attack) :
                   ramp_sample(&fall, i - start);
        }
        if (note->kind != ECHOY_CHORD)
            gain = time < attack ? ramp_sample(&rise, i - start) : ramp_sample(&fall, i - start);
        float sample = (float)(value * gain);
        dry[i] += sample;
        if (wet) send[i] += sample;
    }
}

/* Compressor portions adapted from Chromium's Web Audio implementation.
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The following compressor follows Chromium's Web Audio transfer and envelope
 * equations (DynamicsCompressor, Google 2011):
 * https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/platform/audio/dynamics_compressor.cc
 * The 32-sample detector update, 6ms look-ahead and automatic makeup gain are
 * audible even when the incoming signal stays below the compression knee. */
static float decibels(float value) { return 20 * log10f(value); }
static float linear(float db) { return powf(10, db / 20); }
static float knee(float value, float k, float threshold)
{
    return value < threshold ? value : threshold + (1 - (float)expf(-k * (value - threshold))) / k;
}

static void compress(float *samples, size_t frames, const float *sine)
{
    const float threshold = linear(-12), slope = 1.0f / 8;
    float lo = .1f, hi = 10000, k = 5;
    for (unsigned i = 0; i < 15; ++i) {
        float curve_slope = (decibels(knee(1.001f, k, threshold)) - decibels(knee(1, k, threshold))) / decibels(1.001f);
        if (curve_slope < slope) hi = k; else lo = k;
        k = sqrtf(lo * hi);
    }
    const float knee_db = decibels(knee(1, k, threshold));
    const float makeup = powf(1 / knee(1, k, threshold), .6f);
    const float a = .15f * SR * .09f;
    const float b = .15f * SR * (-1.5788320352845888f * .09f + 2.3305837032074286f * .16f - .9141194204840429f * .42f + .1623677525612032f * .98f);
    const float c = .15f * SR * (.5334142869106424f * .09f - 1.272736789213631f * .16f + .9258856042207512f * .42f - .18656310191776226f * .98f);
    const float d = .15f * SR * (.08783463138207234f * .09f - .1694162967925622f * .16f + .08588057951595272f * .42f - .00429891410546283f * .98f);
    const float e = .15f * SR * (-.042416883008123074f * .09f + .1115693827987602f * .16f - .09764676325265872f * .42f + .028494263462021576f * .98f);
    const float detector_release = linear(2 / (.0025f * SR)) - 1;
    float detector = 0, gain = 1, maximum_attack = -1, delay[144] = {0};
    size_t cursor = 0;
    for (size_t base = 0; base < frames; base += 32) {
        float desired = asinf(detector) / (float)(PI / 2);
        bool releasing = desired > gain;
        float difference = desired == 0 ? (releasing ? -1 : 1) : decibels(gain / desired);
        float rate;
        if (releasing) {
            maximum_attack = -1;
            if (!isfinite(difference)) difference = -1;
            float x = .25f * (max(-12, min(0, difference)) + 12);
            rate = linear(5 / (a + b * x + c * x * x + d * x * x * x + e * x * x * x * x));
        } else {
            if (!isfinite(difference)) difference = 1;
            maximum_attack = max(maximum_attack, difference);
            rate = 1 - powf(.25f / max(.5f, maximum_attack), 1 / (.003f * (float)SR));
        }
        size_t end = base + 32 < frames ? base + 32 : frames;
        for (size_t i = base; i < end; ++i) {
            float input = samples[i], delayed = delay[cursor];
            delay[cursor] = input;
            cursor = (cursor + 1) % 144;
            float absolute = fabsf(input);
            float shaped = absolute < 1 ? knee(absolute, k, threshold) : linear(knee_db + slope * decibels(absolute));
            float attenuation = absolute <= .0001f ? 1 : shaped / absolute;
            float detector_rate = attenuation > detector ?
                (attenuation >= .7943282347f ? detector_release : linear(-decibels(attenuation) / (.0025f * SR)) - 1) : 1;
            detector = min(1, detector + (attenuation - detector) * detector_rate);
            gain = rate < 1 ? gain + (desired - gain) * rate : min(1, gain * rate);
            samples[i] = delayed * makeup * sine_sample(sine, (PI / 2) * gain);
        }
    }
}

static void render(float *output, size_t frames, const echoy_note_t *notes, size_t count,
                   bool anchors_only, bool pulse, bool fade, workspace_t *work)
{
    memset(output, 0, frames * sizeof(float));
    memset(work->send, 0, frames * sizeof(float));
    memset(work->delay, 0, sizeof(work->delay));
    memset(work->feedback, 0, sizeof(work->feedback));
    for (size_t i = 0; i < count; ++i) {
        if (!anchors_only || notes[i].layer == ECHOY_ANCHOR) {
            echoy_note_t event = notes[i];
            if (pulse) event.note = event.kind == ECHOY_KICK ? 38 : event.kind == ECHOY_RIM ? 50 : 84;
            render_note(output, work->send, frames, &event, work);
        }
    }
    biquad_t damp = {0};
    filter_set(&damp, 0, 2900, 1, work->sine);
    for (size_t i = 0; i < frames; ++i) {
        float wet = filter_sample(&damp, work->delay[i % DELAY_FRAMES]);
        /* Web Audio resolves the cycle through a previous 128-frame quantum.
         * An impulse therefore returns at 7500, 15128, 22756, ... samples. */
        work->delay[i % DELAY_FRAMES] = work->send[i] + work->feedback[i % 128];
        work->feedback[i % 128] = wet * .18f;
        output[i] = (output[i] * .5f + wet * .2f) * .55f * (float)sqrtf(.5f);
    }
    compress(output, frames, work->sine);
    if (fade) {
        size_t release = pulse ? 432 : 1440;
        for (size_t i = 0; i < release; ++i) output[frames - release + i] *= (float)(release - 1 - i) / release;
    }
}

/* Sum short float blocks, then accumulate in double: the 60,000-frame RMS
 * keeps its original normalization accuracy without software-double arithmetic
 * for every sample on ESP32-S31. */
static void statistics(const float *samples, size_t frames, double *rms, double *peak)
{
    double energy = 0;
    float maximum = 0;
    for (size_t base = 0; base < frames; base += 128) {
        float block = 0;
        size_t end = base + 128 < frames ? base + 128 : frames;
        for (size_t i = base; i < end; ++i) {
            block += samples[i] * samples[i];
            maximum = max(maximum, fabsf(samples[i]));
        }
        energy += block;
    }
    *peak = maximum;
    *rms = sqrt(energy / frames);
}

static bool valid(const echoy_plan_t *plan)
{
    if (!plan || (unsigned)plan->role > ECHOY_HARMONY || !plan->phrase || plan->phrase > 3 ||
        plan->count > ECHOY_MAX_EVENTS || plan->source_count > 16) return false;
    for (size_t i = 0; i < plan->source_count; ++i) {
        const echoy_seed_note_t *n = &plan->source[i];
        if (!isfinite(n->step) || n->step < 0 || n->step >= 16 || !isfinite(n->velocity) ||
            n->velocity < 0 || n->velocity > 1 || (unsigned)n->kind > ECHOY_HAT) return false;
    }
    for (size_t i = 0; i < plan->count; ++i) {
        const echoy_note_t *n = &plan->events[i];
        if (!isfinite(n->step) || n->step < 0 || n->step >= 16 || !isfinite(n->velocity) || n->velocity < 0 || n->velocity > 1 ||
            !isfinite(n->duration) || n->duration < 0 || n->duration > 2.5f || !isfinite(n->max_duration) || n->max_duration < 0 || n->max_duration > 2.5f ||
            (unsigned)n->kind > ECHOY_CHORD || n->note < 0 || n->note > 127 || n->note_count > 3) return false;
        for (unsigned j = 0; j < n->note_count; ++j) if (n->notes[j] < 0 || n->notes[j] > 127) return false;
    }
    return true;
}

/* Archived PulseModel('A').load -> answer(..., round=0): this sparse response
 * affects Pulse phrase 1's RMS target even though its foreground plays source. */
static size_t original_sparse(const echoy_note_t *source, size_t count, echoy_note_t *out)
{
    memcpy(out, source, count * sizeof(*out));
    uint32_t hash = 97;
    unsigned occupied = 0;
    for (size_t i = 0; i < count; ++i) {
        hash = hash * 31 + (unsigned)source[i].step * 17 + (source[i].kind == ECHOY_KICK ? 1 : source[i].kind == ECHOY_RIM ? 3 : 5);
        occupied |= 1u << (unsigned)source[i].step;
    }
    size_t at = count > 1 ? 1 + hash % (count - 1) : 0;
    bool changed = false;
    if (count > 1) {
        int sign = hash % 2 ? 1 : -1, target = (int)out[at].step + sign;
        if (target <= 0 || target > 15 || (occupied & (1u << target))) target = (int)out[at].step - sign;
        if (target > 0 && target <= 15 && !(occupied & (1u << target))) {
            out[at].step = target;
            changed = true;
        }
    }
    if (!changed) {
        static const unsigned order[] = {3,6,10,14,15,5,9,13,2,7};
        unsigned slots[10], available = 0;
        for (unsigned i = 0; i < 10; ++i) if (!(occupied & (1u << order[i]))) slots[available++] = order[i];
        if (available && count < 10) out[count++] = (echoy_note_t){.step = slots[hash % available], .kind = ECHOY_RIM, .velocity = .46f};
        else {
            out[at].kind = out[at].kind == ECHOY_RIM ? ECHOY_KICK : ECHOY_RIM;
            out[at].velocity = .57f;
        }
    }
    for (size_t i = 1; i < count; ++i) {
        echoy_note_t n = out[i];
        size_t j = i;
        while (j && out[j - 1].step > n.step) { out[j] = out[j - 1]; --j; }
        out[j] = n;
    }
    for (size_t i = 0; i < count; ++i) {
        if (out[i].kind == ECHOY_KICK && i % 2) out[i].kind = ECHOY_RIM;
        out[i].velocity = min(.65f, out[i].velocity);
        out[i].duration = out[i].kind == ECHOY_KICK ? .22f : out[i].kind == ECHOY_RIM ? .09f : .045f;
    }
    return count;
}

echoy_audio_result_t echoy_audio_prepare(const echoy_plan_t *plan, bool original_pulse1,
                                       echoy_audio_loops_t **out)
{
    if (out) *out = NULL;
    if (!out || !valid(plan) || (original_pulse1 && (plan->role != ECHOY_PULSE || plan->phrase != 1))) return ECHOY_AUDIO_INVALID;
    if (!plan->source_count || !plan->count) return ECHOY_AUDIO_EMPTY;
    echoy_audio_loops_t *loops = calloc(1, sizeof(*loops) + 3 * ECHOY_AUDIO_LOOP_FRAMES * sizeof(float));
    if (!loops) return ECHOY_AUDIO_NO_MEMORY;
    workspace_t *work = malloc(sizeof(*work));
    if (!work) {
        free(loops);
        return ECHOY_AUDIO_NO_MEMORY;
    }
    workspace_init(work);
    loops->frames = ECHOY_AUDIO_LOOP_FRAMES;
    for (unsigned kind = 0; kind < ECHOY_AUDIO_KIND_COUNT; ++kind)
        loops->samples[kind] = (float *)(loops + 1) + kind * ECHOY_AUDIO_LOOP_FRAMES;
    for (size_t i = 0; i < plan->source_count; ++i) {
        const echoy_seed_note_t *n = &plan->source[i];
        work->source[i] = (echoy_note_t){.step = n->step, .velocity = n->velocity, .kind = n->kind,
            .duration = n->kind == ECHOY_KICK ? .22f : n->kind == ECHOY_RIM ? .09f : .045f};
    }
#ifdef ESP_PLATFORM
    int64_t profile_start = esp_timer_get_time();
#endif
    render(loops->samples[ECHOY_AUDIO_SOURCE], loops->frames, work->source, plan->source_count, false, true, true, work);
#ifdef ESP_PLATFORM
    int64_t profile_source = esp_timer_get_time();
#endif
    if (original_pulse1) {
        size_t count = original_sparse(work->source, plan->source_count, work->sparse);
        render(loops->samples[ECHOY_AUDIO_FOREGROUND], loops->frames, work->sparse, count, false, true, true, work);
    } else render(loops->samples[ECHOY_AUDIO_FOREGROUND], loops->frames, plan->events, plan->count, true, plan->role == ECHOY_PULSE, true, work);
#ifdef ESP_PLATFORM
    int64_t profile_foreground = esp_timer_get_time();
#endif
    render(loops->samples[ECHOY_AUDIO_FULL], loops->frames, plan->events, plan->count, false, plan->role == ECHOY_PULSE, true, work);
#ifdef ESP_PLATFORM
    int64_t profile_full = esp_timer_get_time();
#endif
    double rms[3], peak[3], target = .045;
    for (unsigned kind = 0; kind < 3; ++kind) {
        statistics(loops->samples[kind], loops->frames, &rms[kind], &peak[kind]);
        if (kind != ECHOY_AUDIO_SOURCE && rms[kind] > 1e-10f && peak[kind] > 0)
            target = fmin(target, .65 * rms[kind] / peak[kind]);
    }
    for (unsigned kind = 0; kind < 3; ++kind) {
        double gain = rms[kind] > 1e-10 ? target / rms[kind] : 0;
        if (kind == ECHOY_AUDIO_SOURCE && peak[kind] > 0) gain = fmin(gain, .65 / peak[kind]);
        for (size_t i = 0; i < loops->frames; ++i) loops->samples[kind][i] = (float)(loops->samples[kind][i] * (float)gain);
    }
    if (original_pulse1) memcpy(loops->samples[ECHOY_AUDIO_FOREGROUND], loops->samples[ECHOY_AUDIO_SOURCE], loops->frames * sizeof(float));
    loops->target_rms = target;
#ifdef ESP_PLATFORM
    ESP_LOGI("echoy_synth", "role=%u phrase=%u events=%u source_us=%lld foreground_us=%lld full_us=%lld normalize_us=%lld",
             (unsigned)plan->role, plan->phrase, (unsigned)plan->count,
             (long long)(profile_source-profile_start), (long long)(profile_foreground-profile_source),
             (long long)(profile_full-profile_foreground), (long long)(esp_timer_get_time()-profile_full));
#endif
    free(work);
    *out = loops;
    return ECHOY_AUDIO_OK;
}
void echoy_audio_free_loops(echoy_audio_loops_t *loops) { free(loops); }
echoy_audio_result_t echoy_audio_render_cue(echoy_audio_cue_t cue, echoy_audio_buffer_t **out)
{
    if (out) *out = NULL;
    if (!out || (unsigned)cue > ECHOY_AUDIO_CAPTURE_START) return ECHOY_AUDIO_INVALID;
    size_t frames = cue == ECHOY_AUDIO_TAP ? 8640 : 720;
    echoy_audio_buffer_t *buffer = calloc(1, sizeof(*buffer) + frames * sizeof(float));
    if (!buffer) return ECHOY_AUDIO_NO_MEMORY;
    buffer->samples = (float *)(buffer + 1);
    buffer->frames = frames;
    if (cue == ECHOY_AUDIO_TAP) {
        workspace_t *work = malloc(sizeof(*work));
        if (!work) { free(buffer); return ECHOY_AUDIO_NO_MEMORY; }
        workspace_init(work);
        echoy_note_t note = {.kind = ECHOY_KICK, .velocity = .78f, .duration = .22f};
        render(buffer->samples, frames, &note, 1, false, true, false, work);
        free(work);
    } else {
        float peak = cue == ECHOY_AUDIO_CAPTURE_START ? .075f : .05f;
        float frequency = cue == ECHOY_AUDIO_CAPTURE_START ? 1500 : 1050;
        for (size_t i = 0; i < frames; ++i) {
            float time = i / SR;
            float gain = time < .001f ? .00001f + (peak - .00001f) * time / .001f : exponential(peak, .00001f, (time - .001f) / .026f);
            buffer->samples[i] = (float)(sinf(2 * PI * frequency * time) * gain);
        }
    }
    *out = buffer;
    return ECHOY_AUDIO_OK;
}
void echoy_audio_free_buffer(echoy_audio_buffer_t *buffer) { free(buffer); }
