#define LINK_TEST_MAG_LOG
#include "../board_link/fake_hardware.h"

static unsigned records(void)
{
    unsigned n=0;
    const char *at=sensor_log;
    while ((at=strstr(at,"ECHOY_MAG_BOOT"))) { ++n; ++at; }
    return n;
}
static void rejected_samples_are_observable(int16_t left_x, const char *want)
{
    board_link_t *h=calloc(1,sizeof(*h));
    assert(h);
    h->sensors_ready=true;
    h->classifier.calibration_state=MOSAICO_MAG_CALIBRATING;
    reject_baseline=true;
    sensor_log_size=0; sensor_log[0]=0;
    sensor_samples[0]=(struct bmm150_mag_data){123,0,-45};
    sensor_samples[1]=(struct bmm150_mag_data){left_x,17,800};
    for (unsigned i=0;i<3;++i) {
        sample_sensors(h,i*40000);
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
        assert(records()==i+1);
#else
        assert(records()==0);
#endif
        sample_sensors(h,i*40000+1); /* Polling cannot duplicate a sensor window. */
    }
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    assert(strstr(sensor_log,want));
    assert(strstr(sensor_log,"right_x=123 right_y=0 right_z=-45"));
    assert(strstr(sensor_log,"attempt=3"));
#else
    (void)want;
#endif
    assert(!h->sensors_ready && h->classifier.calibration_state==MOSAICO_MAG_CALIBRATION_FAILED);
    unsigned count=records();
    sample_sensors(h,1000000);
    assert(records()==count); /* Failure remains closed; diagnostics cannot retry. */
    free(h);
}
static void accepted_and_failed_reads_do_not_emit_invalid_axes(void)
{
    board_link_t *h=calloc(1,sizeof(*h));assert(h);
    h->sensors_ready=true;reject_baseline=false;sensor_log_size=0;sensor_log[0]=0;
    sample_sensors(h,0);
    assert(h->sensors_ready && h->classifier.calibration_state==MOSAICO_MAG_CALIBRATION_READY);
    assert(records()==0);
    sensor_error=ESP_FAIL;sample_sensors(h,40000);
    assert(!h->sensors_ready && records()==0);
    sensor_error=ESP_OK;free(h);
}
int main(void)
{
    rejected_samples_are_observable(INT16_MIN,"left_x=-32768 left_y=17 left_z=800 left_sentinel_bits=1");
    rejected_samples_are_observable(31000,"left_x=31000 left_y=17 left_z=800 left_sentinel_bits=0");
    accepted_and_failed_reads_do_not_emit_invalid_axes();
    puts("PASS startup-only six-axis diagnostic, exact sentinel annotation, disabled default, bounded sampling and unchanged closed failure");
}
