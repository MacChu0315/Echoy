#define ECHOY_GROUP_ADAPTER_TEST
#include "../board_link/fake_hardware.h"
static bool read_snapshot(void *ctx,echoy_link_view_t *out)
{ (void)ctx; memset(out,0,sizeof(*out)); return true; }
static bool receive_snapshot(void *ctx,const echoy_link_update_t *u)
{ (void)ctx; (void)u; return true; }
int main(void)
{
    assert(board_link_start(read_snapshot,receive_snapshot,NULL)==ESP_OK);
    board_link_t *h=instance; assert(h && h->self==11 && h->group);
    assert(board_link_start(read_snapshot,receive_snapshot,NULL)==ESP_ERR_INVALID_STATE);
    h->sensors_ready=h->samples_ready=true;
    h->contacts=MOSAICO_EDGE_MASK_LEFT | MOSAICO_EDGE_MASK_RIGHT;
    radio_edge(h,true,22,100,1000000);radio_edge(h,false,33,101,1000000);
    assert(h->seams[0].peer==22 && h->seams[1].peer==33);
    radio_lost(h,false,1000000);
    assert(h->seams[1].peer==33);
    mosaico_mag_presence_event_t physical={.attached_mask=MOSAICO_EDGE_MASK_LEFT};
    presence_event(&physical,h);
    assert(h->seams[0].peer==22 && !h->seams[1].peer);
    /* Generic SDK claims, including vertical/180-degree pairs, cannot bypass
     * the new versioned reciprocal admission layer. */
    link_event_t e={.received_us=1000000,.message={.type=MOSAICO_PEER_MSG_CONTACT_CLAIM,
        .source_id=44,.source_boot_id=10,.local_edge=MOSAICO_EDGE_TOP}};
    process_event(h,&e);assert(!h->seams[1].peer);
    uint8_t bytes[8]={0};defer_send_callback=true;
    assert(radio_available(h));assert(radio_send(h,0,bytes,sizeof(bytes)));
    assert(!radio_available(h));assert(!radio_send(h,0,bytes,sizeof(bytes)));
    assert(fake_send_callback);fake_send_callback(NULL,ESP_NOW_SEND_FAIL);
    assert(radio_available(h) && atomic_load(&radio_completed)==1 && atomic_load(&radio_mac_failed)==1);
    send_error=ESP_FAIL;assert(!radio_send(h,0,bytes,sizeof(bytes)) && radio_available(h));
    send_error=ESP_OK;defer_send_callback=false;
    assert(radio_send(h,0,bytes,sizeof(bytes)) && radio_available(h));
    assert(board_link_stop(0)==ESP_ERR_TIMEOUT && instance==h);
    atomic_store(&h->worker_exited,true); unregister_send_error=ESP_FAIL;
    assert(board_link_stop(5000)==ESP_FAIL && instance==h && h->group);
    unregister_send_error=ESP_OK;
    assert(board_link_stop(5000)==ESP_OK && !instance);
    assert(board_link_stop(5000)==ESP_OK);
    assert(board_link_start(read_snapshot,receive_snapshot,NULL)==ESP_OK);
    h=instance;atomic_store(&h->worker_exited,true);stop_error=ESP_FAIL;
    assert(board_link_stop(5000)==ESP_FAIL && instance==h && h->events);
    unsigned stopped=stop_calls;stop_error=ESP_OK;
    assert(board_link_stop(5000)==ESP_FAIL && instance==h && stop_calls==stopped);
    assert(board_link_start(read_snapshot,receive_snapshot,NULL)==ESP_ERR_INVALID_STATE);
    unsigned queued=h->events->used;
    radio_event(NULL,0,&e.message,h);assert(h->events->used==queued);
    /* Test-only cleanup: fake hardware has no receive thread. Production must
     * retain this context until reboot because SDK retry cannot prove quiescence. */
    vQueueDelete(h->events);free(h->group);free(h->radio);free(h);instance=NULL;atomic_store(&owned,false);
    puts("PASS active horizontal adapter: admitted seams, radio retention, immediate release, legacy rejection and stop retry ownership");
}
