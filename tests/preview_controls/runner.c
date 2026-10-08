#include "echoy_ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static echoy_platform_t platform;
static bool fail_delete;
uint64_t gsp_sim_bridge_time_ms(void) { return 125; }
esp_gsp_err_t echoy_ui_create(esp_gsp_handle_t ui, const echoy_platform_t *p, echoy_ui_handle_t *out)
{
    (void)ui;
    platform = *p;
    *out = (echoy_ui_handle_t)&platform;
    return ESP_GSP_OK;
}
esp_gsp_err_t echoy_ui_get_error(echoy_ui_handle_t ui) { (void)ui; return 0; }
esp_gsp_err_t echoy_ui_delete(echoy_ui_handle_t ui) { (void)ui; return fail_delete ? -1 : 0; }
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui);
void gsp_bridge_app_deinit(esp_gsp_handle_t ui);

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (gsp_bridge_app_init(NULL)) return 1;
    printf("{\"simulator\":%s,\"battery\":%u,\"seed\":%u,\"condition\":%d,\"time\":%lld}\n",
           platform.is_simulator ? "true" : "false", platform.battery_percent,
           platform.initial_seed, platform.initial_condition, (long long)platform.now_us(platform.ctx));
    char command[128];
    while (fgets(command, sizeof(command), stdin)) {
        if (!strcmp(command, "poll\n")) {
            printf("%d\n", platform.poll_command ? platform.poll_command(platform.ctx) : ECHOY_COMMAND_NONE);
        } else if (!strcmp(command, "remove\n") || !strcmp(command, "restore\n")) {
            if (platform.set_removed) platform.set_removed(platform.ctx, command[0] == 'r' && command[2] == 'm');
            puts("true");
        } else if (!strncmp(command, "export", 6)) {
            echoy_plan_t plan;
            unsigned role = ECHOY_HARMONY, phrase = 3, mask = 0x1085;
            double density = .9;
            (void)sscanf(command, "export %u %u %lf %x", &role, &phrase, &density, &mask);
            if (!echoy_compose_mask((uint16_t)mask, (echoy_role_t)role, phrase, density, &plan)) return 3;
            echoy_snapshot_t snapshot = {.seed = (uint16_t)mask, .role = ECHOY_VOICE, .phrase = 2,
                .density = .3, .revision = 7, .committed_revision = 6, .plan_pending = true};
            if (strstr(command, "bad-kind")) plan.events[0].kind = (echoy_note_kind_t)-1;
            if (strstr(command, "bad-pitches")) plan.events[0].note_count = 4;
            if (strstr(command, "bad-nan")) plan.events[0].duration = NAN;
            bool ok = platform.export_session && platform.export_session(platform.ctx, &snapshot, &plan, .65);
            printf("%s\n", ok ? "true" : "false");
        } else if (!strcmp(command, "fail-delete\n")) {
            fail_delete = true;
            gsp_bridge_app_deinit(NULL);
            fail_delete = false;
            puts("true");
        } else if (!strcmp(command, "delete\n")) {
            gsp_bridge_app_deinit(NULL);
            puts("true");
            return 0;
        } else return 2;
    }
    gsp_bridge_app_deinit(NULL);
    return 0;
}
