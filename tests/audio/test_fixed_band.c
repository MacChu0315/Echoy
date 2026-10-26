#include "echoy_band.h"
#include "echoy_device_pcm.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static unsigned allocation, fail_at;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count,size_t size)
{ return ++allocation==fail_at ? NULL : __real_calloc(count,size); }
echoy_audio_result_t __wrap_echoy_audio_render_cue(echoy_audio_cue_t cue,echoy_audio_buffer_t **out)
{ (void)cue; (void)out; abort(); }
echoy_audio_result_t __wrap_echoy_audio_render_preview_pool(echoy_role_t role,bool speaker,float gain,
                                                          echoy_audio_preview_pool_t *out)
{ (void)role; (void)speaker; (void)gain; (void)out; abort(); }

int main(void)
{
    echoy_band_t *band=echoy_band_create_configured(true,echoy_office_role_gains); assert(band);
    unsigned count=allocation; echoy_band_delete(band);
    assert(count>=7);
    for (unsigned i=1; i<=count; i++) {
        allocation=0; fail_at=i;
        band=echoy_band_create_configured(true,echoy_office_role_gains);
        assert(!band);
    }
    fail_at=0;
    assert(!echoy_band_create_configured(true,echoy_device_role_gains));
    band=echoy_band_create_configured(true,echoy_office_role_gains); assert(band);
    echoy_control_view_t before,after;
    assert(echoy_band_read(band,ECHOY_TILE_A,&before));
    for (unsigned i=1;i<=4;i++) {
        allocation=0; fail_at=i;
        assert(!echoy_band_prepare_previews(band,true,echoy_office_role_gains));
        assert(echoy_band_read(band,ECHOY_TILE_A,&after));
        assert(before.state.seed==after.state.seed && before.state.phase==after.state.phase);
    }
    fail_at=0;
    assert(echoy_band_action(band,ECHOY_TILE_A,ECHOY_ACT_TAP,0,1000000));
    float samples[24000]; echoy_band_render(band,1000000,samples,24000);
    double energy=0; for(unsigned i=0;i<24000;i++)energy+=samples[i]*samples[i];
    assert(energy>1e-5);
    echoy_band_delete(band);
    puts("fixed band: no startup synthesis, allocation rollback and retained audition passed");
}
