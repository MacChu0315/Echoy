/* Explicit diagnostic build only: compare host-generated assets with the same
 * synth compiled for the actual device before handing the band to PCM. */
#include "echoy_fixed_audio.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

bool echoy_fixed_audio_verify(const echoy_fixed_audio_t *view, bool speaker, const float gains[4])
{
    for (unsigned i=0; i<7; i++) {
        echoy_audio_buffer_t *expected=NULL,*actual=NULL;
        echoy_audio_preview_pool_t reference={0},fixed={0};
        bool ok;
        if (i<3) {
            ok=echoy_audio_render_cue(i,&expected)==ECHOY_AUDIO_OK &&
               echoy_fixed_audio_cue(view,i,&actual)==ECHOY_AUDIO_OK;
        } else {
            ok=echoy_audio_render_preview_pool(i-3,speaker,gains?gains[i-3]:1,&reference)==ECHOY_AUDIO_OK &&
               echoy_fixed_audio_preview(view,i-3,&fixed)==ECHOY_AUDIO_OK;
            expected=reference.phrase; actual=fixed.phrase;
            ok=ok && !memcmp(reference.source_frames,fixed.source_frames,sizeof(fixed.source_frames)) &&
                     !memcmp(reference.short_frames,fixed.short_frames,sizeof(fixed.short_frames));
        }
        double error=0,energy=0,peak=0;
        ok=ok && expected && actual && expected->frames==actual->frames;
        if (ok) for (size_t j=0;j<actual->frames;j++) {
            double difference=(double)actual->samples[j]-expected->samples[j];
            error+=difference*difference; energy+=(double)expected->samples[j]*expected->samples[j];
            peak=fmax(peak,fabs(difference));
        }
        double relative=energy>0?sqrt(error/energy):0;
        ok=ok && peak<=1e-5 && relative<=1e-5;
        ESP_LOGI("echoy_fixed", "parity entry=%u max=%.9g relative=%.9g pass=%u",i,peak,relative,ok);
        echoy_audio_free_buffer(expected); echoy_audio_free_buffer(actual);
        if (!ok) return false;
    }
    return true;
}
