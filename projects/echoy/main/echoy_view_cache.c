#include "echoy_view_cache.h"
#define GSP_BUNDLE_ENABLE_RAW_IDS
#include "bundle_gsp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    const char *name;
    gsp_component_key_t key;
    int visible, enabled, color, value, y;
    const char *text;
} view_default_t;
#include "echoy_view_keys.h"
#define VIEW_COUNT (sizeof(view_defaults) / sizeof(view_defaults[0]))
struct cached_value {
    int visible, enabled, color, value, y;
    char *text;
};
struct echoy_view_cache {
    esp_gsp_handle_t gsp;
    struct cached_value values[VIEW_COUNT];
};
static int lookup(const char *name)
{
    if (!name) {
        return -1;
    }
    size_t a = 0, b = VIEW_COUNT;
    while (a < b) {
        size_t i = a + (b - a) / 2;
        int c = strcmp(name, view_defaults[i].name);
        if (!c) {
            return (int)i;
        }
        if (c < 0) {
            b = i;
        } else {
            a = i + 1;
        }
    }
    fprintf(stderr, "ECHOY_VIEW unknown object: %s\n", name);
    return -1;
}
echoy_view_cache_t echoy_view_cache_create(esp_gsp_handle_t gsp)
{
    if (!gsp) {
        return NULL;
    }
    echoy_view_cache_t v = calloc(1, sizeof(*v));
    if (!v) {
        return NULL;
    }
    v->gsp = gsp;
    for (size_t i = 0; i < VIEW_COUNT; i++) {
        v->values[i] = (struct cached_value){.visible = view_defaults[i].visible,
                                             .enabled = view_defaults[i].enabled,
                                             .color = view_defaults[i].color,
                                             .value = view_defaults[i].value,
                                             .y = view_defaults[i].y};
    }
    return v;
}
void echoy_view_cache_delete(echoy_view_cache_t v)
{
    if (v) {
        for (size_t i = 0; i < VIEW_COUNT; i++) {
            free(v->values[i].text);
        }
        free(v);
    }
}
esp_gsp_err_t echoy_view_show(echoy_view_cache_t v, const char *name, bool value)
{
    int i = lookup(name);
    if (!v || i < 0) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    if (v->values[i].visible == value) {
        return ESP_GSP_OK;
    }
    esp_gsp_err_t e = esp_gsp_component_set_visible(v->gsp, view_defaults[i].key, value);
    if (e == ESP_GSP_OK) {
        v->values[i].visible = value;
    }
    return e;
}
esp_gsp_err_t echoy_view_enable(echoy_view_cache_t v, const char *name, bool value)
{
    int i = lookup(name);
    if (!v || i < 0) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    if (v->values[i].enabled == value) {
        return ESP_GSP_OK;
    }
    esp_gsp_err_t e = esp_gsp_component_set_enabled(v->gsp, view_defaults[i].key, value);
    if (e == ESP_GSP_OK) {
        v->values[i].enabled = value;
    }
    return e;
}
esp_gsp_err_t echoy_view_color(echoy_view_cache_t v, const char *name, uint32_t value)
{
    int i = lookup(name);
    if (!v || i < 0) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    if (v->values[i].color == (int)value) {
        return ESP_GSP_OK;
    }
    esp_gsp_err_t e = esp_gsp_component_set_color_rgb888(v->gsp, view_defaults[i].key, value);
    if (e == ESP_GSP_OK) {
        v->values[i].color = (int)value;
    }
    return e;
}
esp_gsp_err_t echoy_view_value(echoy_view_cache_t v, const char *name, int32_t value)
{
    int i = lookup(name);
    if (!v || i < 0) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    if (v->values[i].value == value) {
        return ESP_GSP_OK;
    }
    esp_gsp_err_t e = esp_gsp_component_set_value(v->gsp, view_defaults[i].key, value);
    if (e == ESP_GSP_OK) {
        v->values[i].value = value;
    }
    return e;
}
esp_gsp_err_t echoy_view_y(echoy_view_cache_t v, const char *name, int32_t value)
{
    int i = lookup(name);
    if (!v || i < 0) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    if (v->values[i].y == value) {
        return ESP_GSP_OK;
    }
    const gsp_value_t property = {.type = GSP_VALUE_I32, .data = {.i32 = value}};
    esp_gsp_err_t e = esp_gsp_component_set_property(v->gsp, view_defaults[i].key,
                                                     GSP_ECHOY_PROP_KEY_Y, &property);
    if (e == ESP_GSP_OK) {
        v->values[i].y = value;
    }
    return e;
}
esp_gsp_err_t echoy_view_text(echoy_view_cache_t v, const char *name, const char *text)
{
    int i = lookup(name);
    if (!v || i < 0 || !text) {
        return ESP_GSP_ERR_INVALID_ARG;
    }
    const char *old = v->values[i].text ? v->values[i].text : view_defaults[i].text;
    if (old && !strcmp(old, text)) {
        return ESP_GSP_OK;
    }
    size_t bytes = strlen(text) + 1;
    char *copy = malloc(bytes);
    if (!copy) {
        return ESP_GSP_ERR_NO_MEM;
    }
    memcpy(copy, text, bytes);
    esp_gsp_err_t e = esp_gsp_component_set_text(v->gsp, view_defaults[i].key, copy);
    if (e == ESP_GSP_OK) {
        free(v->values[i].text);
        v->values[i].text = copy;
    } else {
        free(copy);
    }
    return e;
}
