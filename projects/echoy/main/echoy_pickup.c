#include "echoy_pickup.h"
#include "echoy_audio.h"
#include <math.h>
#include <string.h>

bool echoy_pickup_apply(const echoy_plan_t *p, echoy_pickup_t *out)
{
    if (!p || !out || p->source_count > 16 || p->count > ECHOY_MAX_EVENTS) return false;
    memset(out, 0, sizeof(*out));
    if (!p->source_count) return true;
    double head = p->source[0].step;
    const echoy_note_t *first = NULL;
    double soft = INFINITY;
    for (size_t i = 0; i < p->count; ++i) {
        if (p->events[i].layer != ECHOY_ANCHOR) continue;
        if (!first) first = &p->events[i];
        soft = fmin(soft, p->events[i].velocity);
    }
    if (!first || !isfinite(head) || head <= 0) return true;
    if (head >= 16 || !isfinite(soft) || first->harmony_count > 3 || first->note_count > 3)
        return false;
    double gap = fmax(1, fmin(3, p->source_count > 1 ? p->source[1].step - head : 2));
    out->count = p->density >= .5 && head >= gap * 2 ? 2 : 1;
    static const int scale[] = {38,41,43,45,46,48,50,53,55,57,58,60,62,65,67,69,70,72,74,77,79,81,82,84,86};
    int lower = first->note;
    for (size_t i = 0; i < sizeof(scale)/sizeof(scale[0]); ++i)
        if (scale[i] < first->note) lower = scale[i];
    for (size_t i = 0; i < out->count; ++i) {
        bool last = i + 1 == out->count;
        double step = out->count == 2 ? head - gap * (2 - i) : head - fmin(head, gap);
        echoy_note_t *n = &out->notes[i];
        *n = (echoy_note_t){.step=step, .kind=p->role == ECHOY_PULSE ? ECHOY_RIM : first->kind,
            .velocity=soft * (last ? .56 : .44), .duration=fmin(.12, (head-step)*.15625*.45),
            .note=last ? first->note : lower, .layer=ECHOY_DETAIL,
            .source_step=head, .has_source_step=true};
        if (n->kind == ECHOY_CHORD) {
            n->harmony_count = first->harmony_count ? first->harmony_count : first->note_count;
            const int *h = first->harmony_count ? first->harmony : first->notes;
            if (n->harmony_count) memcpy(n->harmony, h, n->harmony_count*sizeof(int));
            else { n->harmony_count=1; n->harmony[0]=first->note; }
            size_t at = i+1 < n->harmony_count ? i+1 : n->harmony_count-1;
            n->note = n->notes[0] = n->harmony[at];
            n->note_count=1; n->articulation=ECHOY_ANSWER; n->max_duration=n->duration;
        }
    }
    /* Frozen 24 kHz path: 3750 frames/step, 12 ms cutoff, 40 ms fade. */
    out->boundary = (size_t)floor(head * 3750 + .5);
    out->end = out->boundary > 288 ? out->boundary - 288 : 0;
    size_t start = (size_t)floor(out->notes[0].step * 3750 + .5);
    out->fade = out->end > start ? out->end-start : 1;
    if (out->fade > 960) out->fade=960;
    return true;
}

bool echoy_pickup_mix(const echoy_pickup_t *p, const float *base,
                      const float *raw, double gain, float *layer)
{
    if (!p || !base || !raw || !layer || !isfinite(gain) || gain < 0 ||
        p->count > 2 || p->end > ECHOY_AUDIO_LOOP_FRAMES || (p->count && !p->fade)) return false;
    double original_peak=0, peak=0;
    for (size_t i=0; i<ECHOY_AUDIO_LOOP_FRAMES; ++i) {
        if (!isfinite(base[i]) || !isfinite(raw[i])) return false;
        original_peak=fmax(original_peak,fabs(base[i]));
    }
    size_t end=p->count ? p->end : 0;
    for (size_t i=0; i<end; ++i) {
        double window=end-i <= p->fade ? .5-.5*cos(3.14159265358979323846*(end-i)/p->fade) : 1;
        layer[i]=(float)(raw[i]*gain*window);
        if (!isfinite(layer[i])) return false;
        peak=fmax(peak,fabs(layer[i]));
    }
    double cap=fmin(.25,original_peak*.55), trim=peak>cap ? cap/peak : 1;
    for (size_t i=0; i<end; ++i) layer[i]=(float)(layer[i]*trim);
    memset(layer+end,0,(ECHOY_AUDIO_LOOP_FRAMES-end)*sizeof(float));
    return true;
}
