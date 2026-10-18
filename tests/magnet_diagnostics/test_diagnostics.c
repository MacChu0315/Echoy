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
static void cached_snapshot_is_nonblocking_and_marks_failed_reads(void)
{
    board_link_diagnostics_t d={0};
    assert(!board_link_diagnostics_snapshot(NULL));
    assert(!board_link_diagnostics_snapshot(&d));
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    board_link_t *h=calloc(1,sizeof(*h));assert(h);
    h->sensors_ready=true;h->classifier.sample_count=8;
    h->contacts=h->reported_contacts=MOSAICO_EDGE_MASK_LEFT;
    h->seams[0].peer=0x30eda0f4459aULL;h->seams[0].session=77;
    for(unsigned i=0;i<8;i++) {
        h->classifier.left_x[i]=-31000;h->classifier.left_z[i]=623;
        h->classifier.right_x[i]=123;h->classifier.right_z[i]=-45;
    }
    valid_classification=true;reject_baseline=false;sensor_error=ESP_OK;
    sensor_samples[0]=(struct bmm150_mag_data){123,0,-45};
    sensor_samples[1]=(struct bmm150_mag_data){INT16_MIN,17,623};
    atomic_store(&diagnostic_active,true);publish_diagnostic(h);
    assert(!board_link_diagnostics_snapshot(&d)); /* No sample yet. */
    sample_sensors(h,100000000);publish_diagnostic(h);
    assert(board_link_diagnostics_snapshot(&d) && d.sequence==1 && d.sampled_us==100000000);
    assert(d.sample_valid && d.sensors_ready && d.samples==8);
    assert(d.raw[0][0]==INT16_MIN && d.filtered[0][0]==-31000 && d.filtered[0][2]==623);
    assert(d.contacts==MOSAICO_EDGE_MASK_LEFT && d.reported==d.contacts && d.classified==d.contacts);
    assert(d.peers[0]==0x30eda0f4459aULL && d.sessions[0]==77);
    atomic_flag_test_and_set(&diagnostic_lock);
    assert(!board_link_diagnostics_snapshot(&d));
    sample_sensors(h,100040000);publish_diagnostic(h); /* Worker skips a busy cache. */
    atomic_flag_clear(&diagnostic_lock);
    assert(board_link_diagnostics_snapshot(&d) && d.sequence==1);
    publish_diagnostic(h);assert(board_link_diagnostics_snapshot(&d) && d.sequence==2);
    sensor_error=ESP_FAIL;sample_sensors(h,100080000);publish_diagnostic(h);
    assert(board_link_diagnostics_snapshot(&d) && d.sequence==3);
    assert(!d.sample_valid && !d.sensors_ready && !d.contacts && !d.peers[0]);
    assert(d.raw[0][0]==0 && d.filtered[0][0]==0);
    sample_sensors(h,101000000);publish_diagnostic(h);
    assert(board_link_diagnostics_snapshot(&d) && d.sequence==3); /* No new I2C retry. */
    atomic_store(&diagnostic_active,false);assert(!board_link_diagnostics_snapshot(&d));
    memset(&h->diagnostic,0,sizeof(h->diagnostic));
    atomic_flag_test_and_set(&diagnostic_lock);publish_diagnostic(h);
    assert(!board_link_diagnostics_snapshot(&d)); /* Restart cannot expose an old cache. */
    atomic_flag_clear(&diagnostic_lock);publish_diagnostic(h);
    assert(!board_link_diagnostics_snapshot(&d));
    atomic_store(&diagnostic_active,false);
    free(h);sensor_error=ESP_OK;valid_classification=false;
#endif
}
int main(void)
{
    cached_snapshot_is_nonblocking_and_marks_failed_reads();
    rejected_samples_are_observable(INT16_MIN,"left_x=-32768 left_y=17 left_z=800 left_sentinel_bits=1");
    rejected_samples_are_observable(31000,"left_x=31000 left_y=17 left_z=800 left_sentinel_bits=0");
    accepted_and_failed_reads_do_not_emit_invalid_axes();
    puts("PASS default-off magnetic snapshots: axes/masks, nonblocking cache, failed reads, inactive/restart fencing and startup diagnostics");
}
