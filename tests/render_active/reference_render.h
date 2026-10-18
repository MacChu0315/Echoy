/* Pre-optimization scheduler from 5fa0d8f, used only as a PCM oracle.
 * Interval-adaptive preview cues read/fade windows of immutable palettes.
 * Original voice/timing-cue arithmetic and the full-scan scheduler are retained. */
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
                uint64_t pos = ((uint64_t)(t - c->at) * ECHOY_AUDIO_SAMPLE_RATE + 500000) / 1000000;
                size_t frames=c->pad_preview ? c->frames : c->buffer->frames;
                if (pos >= frames) {
                    finish_cue(c);
                    continue;
                }
                if(c->pad_preview){
                    double gain=pos<48 ? pos/48.0 : pos+PREVIEW_FADE_FRAMES>=frames ?
                                (frames-1-pos)/(double)PREVIEW_FADE_FRAMES : 1;
                    if(c->stop_at && t>=c->stop_at){
                        uint64_t stop=((uint64_t)(t-c->stop_at)*ECHOY_AUDIO_SAMPLE_RATE+500000)/1000000;
                        if(stop>=PREVIEW_FADE_FRAMES){finish_cue(c);continue;}
                        gain*=1-stop/(double)PREVIEW_FADE_FRAMES;
                    }
                    size_t source=c->offset+(size_t)pos;
                    if(source>=c->buffer->frames)source-=c->buffer->frames;
                    sample+=c->buffer->samples[source]*gain;
                }else sample += c->buffer->samples[pos];
            }
            sum += sample * v->smoothed_volume;
        }
        out[k] = (float)(sum * .45);
    }
    b->render_until = at + (int64_t)(frames * 1000000 / ECHOY_AUDIO_SAMPLE_RATE);
}
