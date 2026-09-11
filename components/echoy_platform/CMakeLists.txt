idf_component_register(
    SRCS "radio.c" "contact.c"
    INCLUDE_DIRS "include"
    REQUIRES echoy_core mosaico_network mosaico_peer_link mosaico_interaction esp-mosaico-bsp esp_timer
)
