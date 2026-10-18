/* Compile this unchanged driver against retained and candidate production C. */
#include "echoy_band.h"
#include "echoy_device_pcm.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv)
{
    assert(argc==2);FILE *file=fopen(argv[1],"wb");assert(file);
    const uint16_t masks[]={5,2,0x50,0x8000};
    for(unsigned role=0;role<4;role++)for(unsigned phrase=1;phrase<=3;phrase++)
    for(unsigned d=0;d<3;d++)for(unsigned m=0;m<4;m++) {
        echoy_band_t *band=echoy_band_create();assert(band);
        echoy_arrange_t *arrange=(echoy_arrange_t *)echoy_band_arrangement(band);
        echoy_seed_note_t seed[16];size_t count=0;
        for(unsigned i=0;i<16;i++)if(masks[m]&(1u<<i))seed[count++]=(echoy_seed_note_t){i,.74,ECHOY_KICK};
        assert(echoy_arrange_set_seed(arrange,0,seed,count));
        echoy_arrange_patch_t patch={.set_role=true,.role=role,.set_phrase=true,.phrase=phrase,.set_density=true,.density=d*.5};
        assert(echoy_arrange_configure(arrange,0,&patch));
        echoy_band_job_t *j=echoy_band_take_job(band);assert(j);
        assert(echoy_band_prepare_job(j)&&echoy_band_voice_for_speaker_job(j)&&echoy_band_calibrate_job(j,echoy_office_role_gains));
        assert(echoy_band_finish_job(band,j,0));echoy_band_tick(band,100000);
        assert(echoy_band_action(band,0,ECHOY_ACT_TRANSPORT,0,100000));echoy_band_tick(band,200000);
        echoy_control_view_t view;assert(echoy_band_read(band,0,&view));
        echoy_device_pcm_t processor;echoy_device_pcm_reset(&processor);
        for(size_t at=0;at<60000;at+=240) {
            float raw[240];int16_t pcm[240];echoy_device_pcm_stats_t stats;
            echoy_band_render(band,view.state.start_us+(int64_t)(at*1000000/24000),raw,240);
            assert(echoy_device_pcm_convert(&processor,raw,pcm,240,&stats));
            assert(fwrite(pcm,sizeof(int16_t),240,file)==240);
        }
        echoy_band_delete(band);
    }
    fclose(file);
}
