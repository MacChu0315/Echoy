/* Actual pinned classifier and presence tracker, with the production adapter.
 * Only physical reads, radio, RTOS and logging are substituted. */
#define ECHOY_GROUP_ADAPTER_TEST
#define LINK_TEST_MAG_LOG
#define LINK_TEST_REAL_MAG
#include "../board_link/fake_hardware.h"
static bool read_snapshot(void *ctx,echoy_link_view_t *out)
{ (void)ctx; memset(out,0,sizeof(*out)); return true; }
static bool receive_snapshot(void *ctx,const echoy_link_update_t *u)
{ (void)ctx; (void)u; return true; }
static void sample(board_link_t *h,struct bmm150_mag_data left,
                   struct bmm150_mag_data right,unsigned frames)
{
    sensor_samples[0]=right;sensor_samples[1]=left;
    for (unsigned i=0;i<frames;++i) {
        fake_now+=40000; sample_sensors(h,fake_now);report_contacts(h,fake_now);
    }
}
int main(void)
{
    const struct bmm150_mag_data quiet={100,0,200},left={INT16_MIN,17,800},right={INT16_MIN,0,200};
    const unsigned masks[]={0,MOSAICO_EDGE_MASK_LEFT,MOSAICO_EDGE_MASK_RIGHT,
                            MOSAICO_EDGE_MASK_LEFT|MOSAICO_EDGE_MASK_RIGHT};
    /* Every boot topology: separated, right endpoint, left endpoint, middle. */
    for (unsigned pass=0;pass<4;++pass) {
        unsigned mode=(pass+1)%4;
        sensor_log_size=0;sensor_log[0]=0;
        assert(board_link_start(read_snapshot,receive_snapshot,NULL)==ESP_OK);
        board_link_t *h=instance;h->sensors_ready=true;
        sample(h,(mode&1) ? left : quiet,(mode&2) ? right : quiet,12);
        assert(h->sensors_ready && h->contacts==masks[mode] && h->reported_contacts==masks[mode]);
        assert(h->classifier.calibration==&MOSAICO_MAG_CALIBRATION_S31_V1);
        assert(h->classifier.calibration->saturation_limit==30000);
        assert(!strstr(sensor_log,"baseline captured") && !strstr(sensor_log,"baseline rejected"));
        /* A brief signal cannot bypass the existing filtering/debounce. */
        sample(h,quiet,quiet,12);assert(!h->contacts);
        sample(h,left,right,1);assert(!h->contacts);
        sample(h,quiet,quiet,12);assert(!h->contacts);
        sample(h,left,right,12);assert(h->contacts==(MOSAICO_EDGE_MASK_LEFT|MOSAICO_EDGE_MASK_RIGHT));
        /* Preserve the pinned left-side rejection predicate and ignore vertical output. */
        sample(h,(struct bmm150_mag_data){INT16_MIN,INT16_MIN,200},quiet,12);assert(!h->contacts);
        sample(h,(struct bmm150_mag_data){100,1000,200},(struct bmm150_mag_data){100,1000,500},12);
        assert(!h->presence.attached_mask && !h->contacts);
        sample(h,left,quiet,12);assert(h->contacts==MOSAICO_EDGE_MASK_LEFT);
        sensor_error=ESP_FAIL;sample(h,left,quiet,1);
        assert(!h->sensors_ready && !h->contacts && !h->reported_contacts);
        sensor_error=ESP_OK;atomic_store(&h->worker_exited,true);
        assert(board_link_stop(0)==ESP_OK);
    }
    puts("PASS real BSP horizontal thresholds: all boot contacts without baseline capture, unchanged thresholds, filtering/debounce, vertical exclusion and failed-read release");
}
