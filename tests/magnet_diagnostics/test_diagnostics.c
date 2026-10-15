#define LINK_TEST_MAG_LOG
#define ECHOY_GROUP_ADAPTER_TEST
#include "../board_link/fake_hardware.h"

static unsigned records(void)
{
    unsigned n=0;
    const char *at=sensor_log;
    while ((at=strstr(at,"ECHOY_MAG_BOOT"))) { ++n; ++at; }
    return n;
}
static unsigned live_records(void)
{
    unsigned n=0; const char *at=sensor_log;
    while ((at=strstr(at,"ECHOY_MAG_LIVE"))) { ++n; ++at; }
    return n;
}
static void live_readback_is_valid_rate_limited_and_bounded(void)
{
    board_link_t *h=calloc(1,sizeof(*h));assert(h);
    h->sensors_ready=true;h->classifier.sample_count=8;
    h->contacts=h->reported_contacts=MOSAICO_EDGE_MASK_LEFT;
    for (unsigned i=0;i<8;++i) {
        h->classifier.left_x[i]=-31000;h->classifier.left_y[i]=17;h->classifier.left_z[i]=800;
        h->classifier.right_x[i]=123;h->classifier.right_y[i]=0;h->classifier.right_z[i]=-45;
    }
    reject_baseline=false;valid_classification=true;sensor_log_size=0;sensor_log[0]=0;
    sensor_samples[0]=(struct bmm150_mag_data){123,0,-45};
    sensor_samples[1]=(struct bmm150_mag_data){INT16_MIN,17,800};
    for (unsigned i=0;i<100;++i) {
        sample_sensors(h,(int64_t)i*1000000);
        sample_sensors(h,(int64_t)i*1000000+40000);
    }
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    assert(live_records()==90);
    assert(strstr(sensor_log,"raw_L=-32768,17,800"));
    assert(strstr(sensor_log,"filtered_L=-31000,17,800"));
    assert(strstr(sensor_log,"edges=08 stable=08 reported=08"));
    /* One self-contained group record per sample must fit Iris's 240-byte
     * record, including log prefix. Splitting a long axes/group line across
     * records loses correlation for line-oriented hardware diagnostics. */
    unsigned group_records=0;
    const char *line=sensor_log;
    while ((line=strstr(line,"ECHOY_GROUP_LIVE"))) {
        const char *end=strchr(line,'\n');assert(end);
        assert((size_t)(end-line)+32<240);
        const char *members=strstr(line,"members=");
        const char *deadline=strstr(line,"deadline=");
        assert(members && members<end && deadline && deadline<end);
        ++group_records;line=end;
    }
    assert(group_records==90);
    assert(strstr(sensor_log,"ECHOY_GROUP_LIVE n=90"));
#else
    assert(live_records()==0);
    assert(!strstr(sensor_log,"ECHOY_GROUP_LIVE"));
#endif
    unsigned count=live_records();
    sensor_error=ESP_FAIL;sample_sensors(h,101000000);
    assert(!h->sensors_ready && live_records()==count);
    free(h);
    h=calloc(1,sizeof(*h));assert(h);h->sensors_ready=true;
    sensor_log_size=0;sensor_log[0]=0;
    sample_sensors(h,200000000); /* A failed read before the cap emits no axes. */
    assert(!h->sensors_ready && live_records()==0);
    sensor_error=ESP_OK;valid_classification=false;free(h);
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
    live_readback_is_valid_rate_limited_and_bounded();
    puts("PASS default-off startup/live magnetic readback, valid axes/filter/contact fields, one-per-second/90-record limit and unchanged closed failure");
}
