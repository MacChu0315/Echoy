#include "echoy_band.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const float gains[4]={1,1,.0672f,2.38f};
static void render(unsigned role,bool calibrated,bool tap,float *out)
{
    echoy_band_t *b=echoy_band_create();assert(b);
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_ROLE,role,0));
    assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_EXAMPLE,0,0));
    echoy_band_job_t *j=echoy_band_take_job(b);assert(j);
    assert(!echoy_band_calibrate_job(j,gains)); /* Not prepared yet. */
    assert(echoy_band_prepare_job(j));
    assert(!echoy_band_calibrate_job(j,NULL));
    if(calibrated) {
        float invalid[4]={1,1,NAN,1};
        assert(!echoy_band_calibrate_job(j,invalid));
        invalid[2]=-1;assert(!echoy_band_calibrate_job(j,invalid));
        invalid[2]=5;assert(!echoy_band_calibrate_job(j,invalid));
        assert(echoy_band_calibrate_job(j,gains));
        assert(!echoy_band_calibrate_job(j,gains)); /* No repeated attenuation. */
    }
    assert(echoy_band_finish_job(b,j,0));
    echoy_band_tick(b,45000);
    echoy_control_view_t v;assert(echoy_band_read(b,ECHOY_TILE_A,&v));
    assert(v.plan.role==role);
    if(tap){
        echoy_band_tick(b,3000000);
        assert(echoy_band_action(b,ECHOY_TILE_A,ECHOY_ACT_TAP,0,3100000));
        echoy_band_render(b,3100000,out,60000);
    }else echoy_band_render(b,v.state.start_us,out,60000);
    echoy_band_delete(b);
}
int main(void)
{
    assert(!echoy_band_calibrate_job(NULL,gains));
    float *base=malloc(60000*sizeof(float)),*calibrated=malloc(60000*sizeof(float));
    assert(base&&calibrated);
    for(unsigned role=0;role<4;role++){
        render(role,false,false,base);render(role,true,false,calibrated);
        double energy=0;
        for(unsigned i=0;i<60000;i++){
            energy+=base[i]*base[i];
            assert(fabsf(calibrated[i]-base[i]*gains[role])<1e-6f);
        }
        assert(energy>.01);
        render(role,false,true,base);render(role,true,true,calibrated);
        /* Random preview uses its own prepared role palette; changing a
         * recorded bank's calibration must not retune that palette. */
        assert(!memcmp(base,calibrated,60000*sizeof(float)));
        energy=0;
        for(unsigned i=0;i<60000;i++)energy+=base[i]*base[i];
        assert(energy>.00001);
    }
    free(base);free(calibrated);
    puts("PASS musical bank calibration leaves the separate random preview palette unchanged");
}
