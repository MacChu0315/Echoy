idf_component_register(
    SRCS "main.c" "echoy_ui.c" "echoy_strip.c" "echoy_strip_image.c" "echoy_model.c" "echoy_music.c" "echoy_view_cache.c" "echoy_art.c" "echoy_art_image.c" "echoy_audio.c" "echoy_arrange.c" "echoy_band.c" "board_display.c"
    INCLUDE_DIRS "."
    REQUIRES esp_iris esp-mosaico-bsp esp_mosaico_app_recovery
        esp_mosaico_gsp_bundle esp_mosaico_gsp_iris freertos nvs_flash
    PRIV_REQUIRES esp_timer esp-gsp esp_display_present esp_lcd)
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_bundle.cmake")
mosaico_gsp_add_ui_bundle(${COMPONENT_LIB} "../ui/main.json")
