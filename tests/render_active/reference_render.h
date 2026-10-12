/* Frozen pre-optimization scheduler from 5fa0d8f, used only as a PCM oracle. */
static void reference_render(echoy_band_t *b, int64_t at, float *out, size_t frames)
{
    if (!b || !out || at < 0) {
        return;
    }
    memset(out, 0, frames * sizeof(*out));
    const double smooth = exp(-1.0 / (.015 * ECHOY_AUDIO_SAMPLE_RATE));
    for (size_t k = 0; k < frames; k++) {
        int64_t t = at + (int64_t)(k * 1000000 / ECHOY_AUDIO_SAMPLE_RATE);
        double sum = 0;
        const echoy_arrange_pending_t *pending = echoy_arrange_get_pending(b->arrange);
        if (pending && t >= pending->at_us) {
            echoy_band_tick(b, t);
        }
        for (int i = 0; i < N; i++) {
            struct voice *v = &b->voices[i];
            double sample = 0;
            v->smoothed_volume = v->volume + (v->smoothed_volume - v->volume) * smooth;
            for (unsigned j = 0; j < RECORDS; j++) {
                struct playback *p = &v->play[j];
                if (!p->bank || t < p->at || (p->stop && t >= p->stop + FADE)) {
                    continue;
                }
                uint64_t pos = ((uint64_t)(t - p->at) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
                if (!p->loop && pos >= p->bank->loops->frames) {
                    continue;
                }
                pos %= p->bank->loops->frames;
                double gain = p->fade && t - p->at < FADE ? (t - p->at) / (double)FADE : 1;
                if (p->stop && t >= p->stop) {
                    gain *= 1 - (t - p->stop) / (double)FADE;
                }
                sample += p->bank->loops->samples[ECHOY_AUDIO_FULL][pos] * gain;
            }
            for (unsigned j = 0; j < CUES; j++) {
                struct cue *c = &b->scheduled[j];
                if (!c->used || c->tile != i || t < c->at) {
                    continue;
                }
                const echoy_audio_buffer_t *buffer = b->cues[c->kind];
                uint64_t pos = ((uint64_t)(t - c->at) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
                if (pos >= buffer->frames) {
                    c->used = false;
                    continue;
                }
                sample += buffer->samples[pos];
            }
            sum += sample * v->smoothed_volume;
        }
        out[k] = (float)(sum * .45);
    }
    b->render_until = at + (int64_t)(frames * 1000000 / ECHOY_AUDIO_SAMPLE_RATE);
}
