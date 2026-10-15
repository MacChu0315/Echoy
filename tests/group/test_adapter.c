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
    h->sensors_ready=true; h->contacts=MOSAICO_EDGE_MASK_LEFT | MOSAICO_EDGE_MASK_RIGHT;
    link_event_t e={.received_us=1000000,.event={.type=MOSAICO_NETWORK_EVENT_NEIGHBOR_ATTACHED,
        .data.neighbor={.peer_id=22,.session_id=100,.local_edge=MOSAICO_EDGE_LEFT,.peer_edge=MOSAICO_EDGE_RIGHT}}};
    process_event(h,&e);
    e.event.data.neighbor=(mosaico_network_neighbor_event_t){.peer_id=33,.session_id=101,
        .local_edge=MOSAICO_EDGE_RIGHT,.peer_edge=MOSAICO_EDGE_LEFT};
    process_event(h,&e);
    assert(h->seams[0].peer==22 && h->seams[1].peer==33);
    e.event.type=MOSAICO_NETWORK_EVENT_NEIGHBOR_DETACHED;
    process_event(h,&e);
    assert(h->seams[1].peer==33); /* Radio cannot release physical membership. */
    mosaico_mag_presence_event_t physical={.attached_mask=MOSAICO_EDGE_MASK_LEFT};
    presence_event(&physical,h);
    assert(h->seams[0].peer==22 && !h->seams[1].peer);
    e.event.type=MOSAICO_NETWORK_EVENT_NEIGHBOR_ATTACHED;
    e.event.data.neighbor.local_edge=MOSAICO_EDGE_TOP;
    process_event(h,&e); assert(!h->seams[1].peer);
    assert(board_link_stop(0)==ESP_ERR_TIMEOUT && instance==h);
    atomic_store(&h->worker_exited,true); stop_error=ESP_FAIL;
    assert(board_link_stop(5000)==ESP_FAIL && instance==h && h->group);
    stop_error=ESP_OK;
    assert(board_link_stop(5000)==ESP_OK && !instance);
    assert(board_link_stop(5000)==ESP_OK);
    puts("PASS active v3 physical adapter: two seams, radio retention, immediate release, invalid edge and stop retry ownership");
}
