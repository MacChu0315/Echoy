idf_component_register(
    SRCS "radio.c" "contact.c" "storage.c" "battery.c" "diagnostics.c" "screen_mirror.c" "control.c" "touch_delivery.c" "runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES echoy_ui echoy_audio esp_iris nvs_flash mosaico_module_mgr echoy_core mosaico_network mosaico_peer_link mosaico_interaction esp-mosaico-bsp esp_timer
)
