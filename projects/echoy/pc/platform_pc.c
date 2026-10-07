#include "gsp_sim_bridge.h"
#include "echoy_ui.h"
#include <stdio.h>
static echoy_ui_handle_t state;
static int64_t now_us(void *ctx)
{
    (void)ctx;
    return (int64_t)gsp_sim_bridge_time_ms() * 1000;
}
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    const echoy_platform_t platform = {.now_us = now_us};
    fprintf(stderr, "Echoy UI foundation: simulated output, no audio synthesis or wireless.\n");
    const esp_gsp_err_t err = echoy_ui_create(ui, &platform, &state);
    if (err == ESP_GSP_OK) {
        fprintf(stderr, "ECHOY_READY\n");
    }
    return err;
}
void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;
    fprintf(stderr, "Echoy UI last_error=%d\n", (int)echoy_ui_get_error(state));
    const esp_gsp_err_t err = echoy_ui_delete(state);
    fprintf(stderr, "Echoy UI cleanup_error=%d\n", (int)err);
    if (err == ESP_GSP_OK) {
        state = NULL;
    }
}
