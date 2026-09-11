idf_component_register(
    SRCS "audio.c" "output.c" "speaker.c"
    INCLUDE_DIRS "include"
    REQUIRES echoy_core esp-mosaico-bsp esp_timer freertos
)
