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
static void pinned_capture_horizontal_results(void)
{
    /* Measured centroids from pinned BSP test_mosaico_mag_classifier.c:
     * "cardinal capture centroids" and "both measured devices". Keep only
     * horizontal expectations because Echoy explicitly excludes vertical. */
    static const struct { int16_t lx,ly,lz,rx,ry; unsigned mask; } cases[]={
        {-1198,-32768,484,-731,-14,0}, {-1179,1229,575,-613,-41,0},
        {-1198,1188,524,-832,164,0}, {-1184,1225,538,-881,-286,0},
        {-32768,1034,570,-499,-76,8}, {-32768,843,694,-431,-94,8},
        {-32768,-32768,735,-452,-18,8}, {-984,-32768,480,-32768,-72,2},
        {-12289,-32768,473,-671,-388,0}, {-32768,-32768,560,-560,-404,0},
        {-1063,742,438,-691,305,0}, {-1026,909,446,-661,-32,0},
        {-32768,-32768,743,-388,-390,8},
        {-1149,-32768,589,-644,-308,0}, {-927,1213,577,-32768,45,2},
        {-885,663,586,-548,333,0}, {-32768,-32768,934,-446,25,8},
        {-678,-32768,283,-155,-191,0}, {-353,1185,282,-32768,169,2},
        {-549,601,347,-302,472,0}, {-32768,1274,729,125,187,8},
        {-976,-32768,553,-32768,-272,2}, {-32768,-32768,892,-399,-347,8},
        {-694,628,537,-32768,353,2}, {-32768,738,980,-307,312,8},
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        sensor_log_size=0;sensor_log[0]=0;
        assert(board_link_start(read_snapshot,receive_snapshot,NULL)==ESP_OK);
        instance->sensors_ready=true;
        sample(instance,(struct bmm150_mag_data){cases[i].lx,cases[i].ly,cases[i].lz},
               (struct bmm150_mag_data){cases[i].rx,cases[i].ry,0},12);
        assert(instance->contacts==cases[i].mask);
        assert(instance->presence.attached_mask==cases[i].mask);
        atomic_store(&instance->worker_exited,true);assert(board_link_stop(0)==ESP_OK);
    }
}
int main(void)
{
    pinned_capture_horizontal_results();
    const struct bmm150_mag_data quiet={100,0,200},left={INT16_MIN,17,800},right={INT16_MIN,0,200};
    /* Captured on A/f45936 in the user-confirmed B-C-A pose. Both X/Y
     * saturated, filtered Z=620..623: the old 650 gate rejects a real seam. */
    assert(board_link_start(read_snapshot,receive_snapshot,NULL)==ESP_OK);
    instance->sensors_ready=true;
    sample(instance,(struct bmm150_mag_data){INT16_MIN,INT16_MIN,621},
           (struct bmm150_mag_data){-77,2,192},12);
    assert(instance->contacts==MOSAICO_EDGE_MASK_LEFT);
    atomic_store(&instance->worker_exited,true);assert(board_link_stop(0)==ESP_OK);
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
        assert(h->classifier.calibration==&echoy_horizontal_profile);
        assert(h->classifier.calibration->saturation_limit==30000);
        assert(h->classifier.calibration->left_reject_z==600);
        assert(MOSAICO_MAG_CALIBRATION_S31_V1.left_reject_z==650);
        assert(!strstr(sensor_log,"baseline captured") && !strstr(sensor_log,"baseline rejected"));
        /* A brief signal cannot bypass the existing filtering/debounce. */
        sample(h,quiet,quiet,12);assert(!h->contacts);
        sample(h,left,right,1);assert(!h->contacts);
        sample(h,quiet,quiet,12);assert(!h->contacts);
        sample(h,left,right,12);assert(h->contacts==(MOSAICO_EDGE_MASK_LEFT|MOSAICO_EDGE_MASK_RIGHT));
        /* Preserve the pinned left-side rejection predicate and ignore vertical output. */
        sample(h,(struct bmm150_mag_data){INT16_MIN,INT16_MIN,200},quiet,12);assert(!h->contacts);
        sample(h,(struct bmm150_mag_data){INT16_MIN,INT16_MIN,599},quiet,12);assert(!h->contacts);
        sample(h,(struct bmm150_mag_data){INT16_MIN,INT16_MIN,600},quiet,12);assert(h->contacts==MOSAICO_EDGE_MASK_LEFT);
        sample(h,(struct bmm150_mag_data){100,1000,200},(struct bmm150_mag_data){100,1000,500},12);
        assert(!h->presence.attached_mask && !h->contacts);
        sample(h,left,quiet,12);assert(h->contacts==MOSAICO_EDGE_MASK_LEFT);
        sensor_error=ESP_FAIL;sample(h,left,quiet,1);
        assert(!h->sensors_ready && !h->contacts && !h->reported_contacts);
        sensor_error=ESP_OK;atomic_store(&h->worker_exited,true);
        assert(board_link_stop(0)==ESP_OK);
    }
    puts("PASS real BSP horizontal thresholds: recorded BCA seam, 25 pinned capture projections, boundary guards, all boot contacts, filtering/debounce, vertical exclusion and failed-read release");
}
