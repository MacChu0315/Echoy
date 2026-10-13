#include "echoy_art.h"
#include "echoy_art_image.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int64_t controlled_clock(void *ctx)
{
    static const int64_t times[] = {100, 140, 160};
    unsigned *calls = ctx;
    assert(*calls < 3);
    return times[(*calls)++];
}
int main(void)
{
    struct echoy_art_workspace *workspace = echoy_art_workspace_create();
    assert(workspace);
    for (unsigned role = 0; role < 4; role++) {
        for (unsigned phrase = 1; phrase <= 3; phrase++) {
            echoy_plan_t plan;
            assert(echoy_compose_mask(0x44a0, role, phrase, .5, &plan));
            echoy_art_frame_t frame = {.plan = &plan, .workspace = workspace, .role = role,
                .width = 206, .height = 101, .background = 0xe5e7de};
            uint8_t *plain = NULL, *profiled = NULL;
            size_t plain_size = 0, profiled_size = 0;
            unsigned calls = 0;
            echoy_art_encode_cost_t cost = {1, 2};
            assert(echoy_art_encode_qoi(&frame, &plain, &plain_size));
            assert(echoy_art_encode_qoi_profiled(&frame, &profiled, &profiled_size,
                                                 controlled_clock, &calls, &cost));
            assert(calls == 3 && cost.draw_us == 40 && cost.encode_us == 20);
            assert(plain_size == profiled_size && !memcmp(plain, profiled, plain_size));
            free(plain);
            free(profiled);
        }
    }
    echoy_art_encode_cost_t cost = {1, 2};
    unsigned calls = 0;
    uint8_t *image = NULL;
    size_t size = 0;
    assert(!echoy_art_encode_qoi_profiled(NULL, &image, &size, controlled_clock, &calls, &cost));
    assert(!calls && !cost.draw_us && !cost.encode_us && !image && !size);
    echoy_art_workspace_delete(workspace);
    return 0;
}
