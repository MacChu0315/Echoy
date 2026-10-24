idf_component_register(
    SRCS "main.c" "echoy_ui.c" "echoy_strip.c" "echoy_strip_image.c" "echoy_model.c" "echoy_music.c" "echoy_view_cache.c" "echoy_art.c" "echoy_art_image.c" "echoy_audio.c" "echoy_pickup.c" "echoy_arrange.c" "echoy_band.c" "board_display.c"
        "platform_device.c" "board_audio.c" "board_health.c"
        "echoy_audio_probe.c" "echoy_audio_diagnostics.c"
        "echoy_output_measurement.c" "echoy_measurement_diagnostics.c"
        "echoy_snapshot_diagnostics.c" "board_touch_trace.c"
        "echoy_art_producer.c"
        "echoy_device_pcm.c"
        "echoy_display.c" "board_display_io.c"
        "board_link.c" "echoy_radio.c" "echoy_link_codec.c" "echoy_link_protocol.c" "echoy_group.c"
    INCLUDE_DIRS "."
    REQUIRES esp_iris esp-mosaico-bsp esp_mosaico_app_recovery
        esp_mosaico_gsp_bundle esp_mosaico_gsp_iris freertos nvs_flash
    PRIV_REQUIRES esp_timer esp_wifi esp-gsp esp_display_present esp_lcd esp_driver_i2s esp_driver_gpio esp_driver_i2c esp_codec_dev mosaico_interaction mosaico_peer_link)
# Register one header: the pinned SDK's multi-header generator otherwise omits
# ESP-NOW names, hiding specific send failures behind the generic "ERROR" text.
idf_build_get_property(echoy_idf_path IDF_PATH)
idf_define_esp_err_codes(HEADERS "${echoy_idf_path}/components/esp_wifi/include/esp_now.h")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_bundle.cmake")
mosaico_gsp_add_ui_bundle(${COMPONENT_LIB} "../ui/main.json")
# Apply the application-specific LCD queue budget through the public IO
# constructor without changing the shared BSP or its DMA transport policy.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_lcd_new_panel_io_spi"
    "-Wl,-u,__wrap_esp_lcd_new_panel_io_spi")
