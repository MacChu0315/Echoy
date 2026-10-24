#include "echoy_audio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <stdlib.h>

echoy_audio_result_t reference_echoy_audio_prepare(const echoy_plan_t *,bool,echoy_audio_loops_t **);
bool reference_echoy_audio_voice_for_speaker(const echoy_plan_t *,echoy_audio_loops_t *);
void reference_echoy_audio_free_loops(echoy_audio_loops_t *);

static bool equal_audio(const echoy_audio_loops_t *a,const echoy_audio_loops_t *b,
                        double *maximum,double *relative)
{
    if(!a || !b || a->frames!=b->frames)return false;
    bool ok=true;
    for(unsigned k=0;k<3;k++) {
        double error=0,energy=0,peak=0;
        for(size_t i=0;i<a->frames;i++) {
            double x=a->samples[k][i],y=b->samples[k][i];
            if(!isfinite(x) || !isfinite(y))return false;
            double delta=x-y;error+=delta*delta;energy+=x*x;peak=fmax(peak,fabs(delta));
        }
        double ratio=energy>0?sqrt(error/energy):(error==0?0:INFINITY);
        *maximum=fmax(*maximum,peak);*relative=fmax(*relative,ratio);
        if(peak>1e-5 || ratio>1e-5)ok=false;
    }
    return ok && fabs(a->target_rms-b->target_rms)<=1e-5 &&
           fabs(a->full_gain-b->full_gain)<=1e-5*fmax(1,fabs(a->full_gain));
}

/* Explicit startup diagnostic, before PCM activation. Never plays any sample. */
bool echoy_audio_check_preparation(void)
{
    echoy_plan_t *plan=heap_caps_calloc(1,sizeof(*plan),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!plan)return false;
    for(unsigned role=0;role<4;role++) for(unsigned phrase=1;phrase<=3;phrase++) {
        echoy_audio_loops_t *reference=NULL,*candidate=NULL;
        bool original=role==ECHOY_PULSE && phrase==1;
        bool ok=echoy_compose_mask(0x2555,role,phrase,.75,plan);
        int64_t old_us=0,new_us=0,start;
        /* Alternate order to expose first-run/cache bias in the paired logs. */
        for(unsigned order=0;ok && order<2;order++) {
            bool old=(order==(phrase&1));start=esp_timer_get_time();
            if(old) {
                ok=reference_echoy_audio_prepare(plan,original,&reference)==ECHOY_AUDIO_OK;
                old_us=esp_timer_get_time()-start;
            } else {
                ok=echoy_audio_prepare(plan,original,&candidate)==ECHOY_AUDIO_OK;
                new_us=esp_timer_get_time()-start;
            }
        }
        double maximum=0,relative=0;
        if(ok)ok=equal_audio(reference,candidate,&maximum,&relative);
        if(ok) {
            start=esp_timer_get_time();ok=reference_echoy_audio_voice_for_speaker(plan,reference);
            old_us+=esp_timer_get_time()-start;
        }
        if(ok) {
            start=esp_timer_get_time();ok=echoy_audio_voice_for_speaker(plan,candidate);
            new_us+=esp_timer_get_time()-start;
        }
        if(ok)ok=equal_audio(reference,candidate,&maximum,&relative);
        ESP_LOGI("echoy_prepare_check","role=%u phrase=%u baseline_us=%lld candidate_us=%lld max=%.9g relative=%.9g pass=%u",
                 role,phrase,(long long)old_us,(long long)new_us,maximum,relative,ok);
        reference_echoy_audio_free_loops(reference);echoy_audio_free_loops(candidate);
        if(!ok) { free(plan);return false; }
    }
    free(plan);return true;
}
