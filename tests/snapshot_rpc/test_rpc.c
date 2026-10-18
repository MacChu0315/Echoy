#include "echoy_snapshot_diagnostics.h"
#include "echoy_link_codec.h"
#include "board_touch_trace.h"
#include "board_link.h"
#include "esp_iris.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static esp_iris_rpc_handler_t handlers[6];
static void *contexts[6];
static unsigned fail_method, fail_unreg, reads, allocations, releases;
static bool fail_alloc, fail_read, recurse;
static echoy_control_view_t view;
static bool trace_available, trace_empty, trace_recurse;
static unsigned trace_reads;
static esp_err_t call(unsigned, const uint8_t *, size_t, uint8_t *, size_t, size_t *);

#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
static bool link_available,link_recurse;
static unsigned link_reads;
bool board_link_diagnostics_snapshot(board_link_diagnostics_t *out)
{
    ++link_reads;
    if(link_recurse) {
        uint8_t tiny[1];size_t size=9;
        assert(call(4,NULL,0,tiny,sizeof(tiny),&size)==ESP_ERR_INVALID_STATE && !size);
    }
    if(!link_available)return false;
    *out=(board_link_diagnostics_t){.phrase={.generation=2,.graph=0x1234,.destination=0x30eda0f4459aULL,.plan=0x5678,
         .identity=42,.attempt=7,.queued=3,.first_sent=3,.acked=2,.ack_samples=2,.failed=1,
         .retransmitted=4,.submit_failures=1,.queue_us_sum=UINT64_MAX,.queue_us_max=123,
         .ack_us_sum=456,.ack_us_max=321,.first_queued_us=100,.last_acked_us=900},
        .timing={-1,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX},.sequence=123,.sampled_us=INT64_C(1234567890123),
        .sample_valid=true,.sensors_ready=true,.samples=8,.calibration=2,
        .raw={{INT16_MIN,17,623},{123,0,-45}},.filtered={{-31000,17,623},{123,0,-45}},
        .classified=10,.contacts=8,.reported=8,.peers={UINT64_C(0x30eda0f4459a),0},
        .sessions={77,0},.group_count=2,.group_rank=1,.group_phase=5,.clock_mapped=true,
        .group_identity=0xaabbccdd,.attempt=7,.radio_known=3,.radio_admitted=true,
        .radio_fresh=true,.radio_graph=UINT64_C(0x123456789abcdef0),.radio_queue_drops=2};
    return true;
}
#endif

bool board_touch_trace_snapshot(board_touch_trace_summary_t *summary,
                                board_touch_trace_event_t *events, size_t event_capacity,
                                board_touch_irq_event_t *irqs, size_t irq_capacity)
{
    ++trace_reads;
    assert(event_capacity==512 && irq_capacity==64 && events && irqs);
    if (trace_recurse) {
        uint8_t tiny[1]; size_t size=123;
        assert(call(3,NULL,0,tiny,sizeof(tiny),&size)==ESP_ERR_INVALID_STATE && size==0);
    }
    if (!trace_available) return false;
    *summary=(board_touch_trace_summary_t){0};
    if (trace_empty) return true;
    *summary=(board_touch_trace_summary_t){.irq_count=70,.irq_overwritten=6,
        .reads=777,.read_errors=2,.raw_reports=778,.raw_presses=24,.raw_releases=23,
        .observers=50,.observer_presses=25,.submitted=21,.same_grid=4,
        .multiple_irq_windows=3,.irq_during_read=1,.event_count=47,.event_overwritten=7,
        .busy_drops=3,.events_copied=40,.irqs_copied=64,.state=BOARD_TOUCH_TRACE_FROZEN,.max_read_gap_us=999,
        .max_read_duration_us=888,.max_latest_irq_age_us=777,.max_raw_to_observer_us=666};
    for (unsigned i=0;i<40;++i) events[i]=(board_touch_trace_event_t){
        .seq=i+1,.kind=i%2?BOARD_TOUCH_TRACE_RAW:BOARD_TOUCH_TRACE_OBSERVER,
        .edge=1,.pressed=true,.at_us=INT64_C(1234567890123)+i,.irq_at_us=INT64_C(1234567890000)+i,
        .raw_at_us=INT64_C(1234567890100)+i,.capture_at_us=INT64_C(1234567000000),
        .read_seq=500+i,.raw_seq=600+i,.irq_seq=700+i,.phase=2,.grid=(int8_t)(i%16),
        .pending_seed=0x4001,.submitted=true,.raw_match=true,.same_grid=i==38};
    memset(irqs,0,irq_capacity*sizeof(*irqs));
    return true;
}

void *heap_caps_calloc(size_t n, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail_alloc) return NULL;
    ++allocations;
    return calloc(n, size);
}
void snapshot_test_free(void *ptr) { if (ptr) ++releases; free(ptr); }
esp_err_t esp_iris_rpc_register(uint16_t service, uint16_t method, esp_iris_rpc_handler_t fn, void *ctx)
{
    assert(service == ECHOY_SNAPSHOT_SERVICE_ID && method >= 1 && method <= 5);
    if (method == fail_method) return ESP_FAIL;
    if (handlers[method]) return ESP_ERR_INVALID_STATE;
    handlers[method] = fn; contexts[method] = ctx; return ESP_OK;
}
esp_err_t esp_iris_rpc_unregister(uint16_t service, uint16_t method)
{
    assert(service == ECHOY_SNAPSHOT_SERVICE_ID && method >= 1 && method <= 5);
    if (method == fail_unreg) return ESP_ERR_INVALID_STATE;
    if (!handlers[method]) return ESP_ERR_NOT_FOUND;
    handlers[method] = NULL; contexts[method] = NULL; return ESP_OK;
}
static esp_err_t call(unsigned method, const uint8_t *payload, size_t payload_size,
                      uint8_t *out, size_t capacity, size_t *size)
{
    esp_iris_rpc_request_t req = {.service_id=ECHOY_SNAPSHOT_SERVICE_ID,
        .method_id=method, .payload=payload, .payload_size=payload_size};
    return handlers[method](&req, out, capacity, size, contexts[method]);
}
static bool read_view(void *ctx, echoy_control_view_t *out)
{
    assert(ctx == &view); ++reads;
    if (recurse) {
        uint8_t tiny[1]; size_t size = 999;
        assert(call(1, NULL, 0, tiny, sizeof(tiny), &size) == ESP_ERR_INVALID_STATE && size == 0);
    }
    if (fail_read) return false;
    *out = view;
    return true;
}
static uint64_t token(const uint8_t *metadata)
{
    const char *at = strstr((const char *)metadata, "\"token\":\"");
    assert(at); return strtoull(at + 9, NULL, 16);
}
static void request(uint8_t out[14], uint64_t id, uint32_t offset, unsigned length)
{
    for (unsigned i=0; i<8; ++i) out[i]=(uint8_t)(id>>(i*8));
    for (unsigned i=0; i<4; ++i) out[8+i]=(uint8_t)(offset>>(i*8));
    out[12]=(uint8_t)length; out[13]=(uint8_t)(length>>8);
}
int main(void)
{
    echoy_platform_t platform = {.read_control=read_view, .ctx=&view};
    echoy_snapshot_diagnostics_handle_t handle = NULL;
    assert(echoy_snapshot_diagnostics_start(NULL, &handle) == ESP_ERR_INVALID_ARG);
    fail_alloc=true;
    assert(echoy_snapshot_diagnostics_start(&platform, &handle) == ESP_ERR_NO_MEM && !handle);
    fail_alloc=false; fail_method=2;
    assert(echoy_snapshot_diagnostics_start(&platform, &handle) == ESP_FAIL && !handle);
    assert(!handlers[1] && allocations == releases);
    fail_method=3;
    assert(echoy_snapshot_diagnostics_start(&platform, &handle)==ESP_FAIL && !handle);
    assert(!handlers[1] && !handlers[2] && allocations==releases);
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    fail_method=5;
    assert(echoy_snapshot_diagnostics_start(&platform,&handle)==ESP_FAIL && !handle);
    assert(!handlers[5] && allocations==releases);
    fail_method=4;
    assert(echoy_snapshot_diagnostics_start(&platform,&handle)==ESP_FAIL && !handle);
    assert(!handlers[1] && !handlers[2] && !handlers[3] && !handlers[4] && allocations==releases);
#endif
    fail_method=2;
    fail_unreg=1;
    assert(echoy_snapshot_diagnostics_start(&platform, &handle) == ESP_FAIL && handle);
    uint8_t reply[1024], part[512], req[14], assembled[ECHOY_LINK_PLAN_MAX_BYTES]; size_t size=999;
    assert(call(1, NULL, 0, reply, sizeof(reply), &size) == ESP_ERR_INVALID_STATE && size == 0);
    assert(echoy_snapshot_diagnostics_stop(&handle) == ESP_ERR_INVALID_STATE && handle);
    fail_unreg=0;
    assert(echoy_snapshot_diagnostics_stop(&handle) == ESP_OK && !handle && allocations == releases);
    fail_method=0;
    assert(echoy_snapshot_diagnostics_start(&platform, &handle) == ESP_OK && handle);
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    assert(handlers[4]); /* Diagnostics must remain readable after startup. */
#endif
    assert(handlers[1](NULL,reply,sizeof(reply),&size,contexts[1])==ESP_ERR_INVALID_ARG && size==0);
    esp_iris_rpc_request_t unsupported={.service_id=ECHOY_SNAPSHOT_SERVICE_ID,.method_id=9};
    assert(handlers[1](&unsupported,reply,sizeof(reply),&size,contexts[1])==ESP_ERR_NOT_SUPPORTED);
    assert(echoy_compose_mask(0x40aa, ECHOY_HARMONY, 2, .5, &view.plan));
    view.volume=.34234234234234234; view.state.phase=ECHOY_PLAYING;
    view.state.revision=12; view.state.committed_revision=11; view.state.plan_pending=true;
    view.state.generation=9; view.source=1; view.cooperation=ECHOY_COOP_FOLLOWING;
    request(req, 1, 0, 1);
    assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_STATE && size==0);
    assert(call(1,NULL,1,reply,sizeof(reply),&size)==ESP_ERR_INVALID_SIZE && reads==0);
    assert(call(1,NULL,0,reply,8,&size)==ESP_ERR_INVALID_SIZE && reads==0);
    recurse=true;
    assert(call(1,NULL,0,reply,sizeof(reply),&size)==ESP_OK);
    recurse=false;
    assert(size==strlen((char *)reply)); puts((char *)reply);
    uint64_t first=token(reply); assert(first);
    uint8_t expected[ECHOY_LINK_PLAN_MAX_BYTES];
    size_t expected_size=echoy_link_plan_encode(&view.plan,expected,sizeof(expected)); assert(expected_size);
    view.plan.events[0].note=117; view.volume=.99;
    request(req,first,0,(unsigned)expected_size);
    assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_SIZE);
    for(size_t at=0;at<expected_size;at+=512){
        unsigned n=expected_size-at<512?expected_size-at:512;
        request(req,first,(uint32_t)at,n);
        assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_OK && size==n);
        memcpy(assembled+at,part,n);
    }
    assert(memcmp(assembled,expected,expected_size)==0 && reads==1);
    echoy_plan_t decoded; assert(echoy_link_plan_decode(assembled,expected_size,&decoded));
    assert(decoded.events[0].note!=117);
    request(req,first,UINT32_MAX,1);
    assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_SIZE && size==0);
    request(req,first,(uint32_t)expected_size,1);
    assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_SIZE);
    request(req,first,0,0); assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_SIZE);
    request(req,first,0,10); assert(call(2,req,sizeof(req),part,9,&size)==ESP_ERR_INVALID_SIZE);
    assert(call(2,NULL,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_SIZE);
    assert(call(2,req,sizeof(req)-1,part,sizeof(part),&size)==ESP_ERR_INVALID_SIZE);
    fail_read=true;
    assert(call(1,NULL,0,reply,sizeof(reply),&size)==ESP_ERR_INVALID_STATE && size==0);
    fail_read=false; view.volume=NAN;
    assert(call(1,NULL,0,reply,sizeof(reply),&size)==ESP_ERR_INVALID_STATE);
    request(req,first,0,10); assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_OK);
    view.volume=.3; view.plan.count=65;
    assert(call(1,NULL,0,reply,sizeof(reply),&size)==ESP_ERR_INVALID_STATE && size==0);
    request(req,first,0,10); assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_OK);
    view.volume=.3; view.plan.count=64;
    for(unsigned i=1;i<64;++i) view.plan.events[i]=view.plan.events[0];
    view.plan.source_count=16;
    for(unsigned i=1;i<16;++i) view.plan.source[i]=view.plan.source[0];
    assert(call(1,NULL,0,reply,sizeof(reply),&size)==ESP_OK);
    uint64_t second=token(reply); assert(second!=first);
    request(req,first,0,1); assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_STATE);
    expected_size=echoy_link_plan_encode(&view.plan,expected,sizeof(expected));
    assert(expected_size==4208); /* 32 + 16*17 + 64*61; buffer still reserves 8192. */
    for(size_t at=0;at<expected_size;at+=512){
        unsigned n=expected_size-at<512?expected_size-at:512;
        request(req,second,(uint32_t)at,n);
        assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_OK && size==n);
        memcpy(assembled+at,part,n);
    }
    assert(memcmp(assembled,expected,expected_size)==0);
    assert(echoy_link_plan_decode(assembled,expected_size,&decoded) && decoded.count==64 && decoded.source_count==16);
    assert(handlers[3]); /* Missing read-only trace method must fail here. */
    unsigned reads_before_trace=reads;
    uint8_t touch[4096];
    assert(call(3,NULL,1,touch,sizeof(touch),&size)==ESP_ERR_INVALID_SIZE && trace_reads==0);
    assert(call(3,NULL,0,touch,8,&size)==ESP_ERR_INVALID_SIZE && trace_reads==0);
    assert(call(3,NULL,0,touch,sizeof(touch),&size)==ESP_OK);
    assert(strstr((char *)touch,"\"available\":false") && strstr((char *)touch,"inactive_or_busy"));
    fail_alloc=true;
    assert(call(3,NULL,0,touch,sizeof(touch),&size)==ESP_OK && strstr((char *)touch,"no_psram"));
    fail_alloc=false; trace_available=trace_empty=true;
    assert(call(3,NULL,0,touch,sizeof(touch),&size)==ESP_OK); puts((char *)touch);
    trace_empty=false; trace_recurse=true;
    assert(call(3,NULL,0,touch,sizeof(touch),&size)==ESP_OK && size<3500); puts((char *)touch);
    trace_recurse=false;
    assert(call(3,NULL,0,touch,1024,&size)==ESP_OK && size<1024); puts((char *)touch);
    assert(reads==reads_before_trace); /* No extra cached view, touch or audio access. */
    request(req,second,0,1); assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_OK);
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    assert(call(4,NULL,1,reply,sizeof(reply),&size)==ESP_ERR_INVALID_SIZE && !link_reads);
    assert(call(4,NULL,0,reply,32,&size)==ESP_ERR_INVALID_SIZE && !link_reads);
    assert(call(4,NULL,0,reply,sizeof(reply),&size)==ESP_OK && strstr((char *)reply,"\"available\":false"));
    link_available=true;fail_alloc=true;
    unsigned before_link_reads=link_reads;
    assert(call(4,NULL,0,reply,sizeof(reply),&size)==ESP_OK && strstr((char *)reply,"no_psram"));
    assert(link_reads==before_link_reads);fail_alloc=false;link_recurse=true;
    unsigned old_reads=reads;
    assert(call(4,NULL,0,reply,sizeof(reply),&size)==ESP_OK && size<1024);
    assert(strstr((char *)reply,"\"radio_admitted\":"));
    puts((char *)reply);assert(reads==old_reads);
    request(req,second,0,1);assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_OK);
    assert(handlers[5]);
    assert(call(5,NULL,1,reply,sizeof(reply),&size)==ESP_ERR_INVALID_SIZE);
    assert(call(5,NULL,0,reply,32,&size)==ESP_ERR_INVALID_SIZE);
    fail_alloc=true;
    assert(call(5,NULL,0,reply,sizeof(reply),&size)==ESP_OK && strstr((char *)reply,"no_psram"));
    fail_alloc=false;
    assert(call(5,NULL,0,reply,sizeof(reply),&size)==ESP_OK && size<1024);
    puts((char *)reply);assert(reads==old_reads);
    request(req,second,0,1);assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_OK);
    fail_unreg=5;
#else
    assert(!handlers[4]);
    fail_unreg=2;
#endif
    assert(echoy_snapshot_diagnostics_stop(&handle)==ESP_ERR_INVALID_STATE && handle);
#ifdef CONFIG_ECHOY_MAGNET_DIAGNOSTICS
    assert(call(5,NULL,0,reply,sizeof(reply),&size)==ESP_ERR_INVALID_STATE && size==0);
#else
    request(req,second,0,1);
    assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_STATE && size==0);
#endif
    fail_unreg=0;
    assert(echoy_snapshot_diagnostics_stop(&handle)==ESP_OK && !handle && allocations==releases);
    assert(echoy_snapshot_diagnostics_start(&platform,&handle)==ESP_OK);
    assert(call(1,NULL,0,reply,sizeof(reply),&size)==ESP_OK && token(reply)!=second);
    request(req,second,0,1);assert(call(2,req,sizeof(req),part,sizeof(part),&size)==ESP_ERR_INVALID_STATE);
    assert(echoy_snapshot_diagnostics_stop(&handle)==ESP_OK && allocations==releases);
    assert(echoy_snapshot_diagnostics_stop(&handle)==ESP_OK);
    assert(echoy_snapshot_diagnostics_start(&platform,&handle)==ESP_OK);
    handlers[1]=handlers[2]=handlers[3]=handlers[4]=handlers[5]=NULL; /* Iris already stopped and removed its entries. */
    assert(echoy_snapshot_diagnostics_stop(&handle)==ESP_OK && !handle && allocations==releases);
    fprintf(stderr,"PASS immutable cached snapshot, exact ECHP, bounded chunks, stale tokens, PSRAM, concurrency and lifecycle failures\n");
}
