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

if(CONFIG_ECHOY_PREBUILT_AUDIO)
    # A native compiler runs the unchanged portable synth on the build host.
    # Never use CMAKE_C_COMPILER here: it produces ESP executables.
    find_program(ECHOY_HOST_CC NAMES cc gcc clang REQUIRED NO_CMAKE_FIND_ROOT_PATH)
    get_filename_component(echoy_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    set(echoy_fixed_profile device)
    if(CONFIG_ECHOY_OFFICE_VOICING_TRIAL)
        set(echoy_fixed_profile office)
    endif()
    set(echoy_fixed_bin "${CMAKE_CURRENT_BINARY_DIR}/echoy_fixed_audio.bin")
    file(GLOB echoy_fixed_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.h")
    add_custom_command(OUTPUT "${echoy_fixed_bin}" "${CMAKE_CURRENT_BINARY_DIR}/echoy_fixed_audio.json"
        COMMAND "${PYTHON}" "${echoy_root}/tools/generate_fixed_audio.py"
            --cc "${ECHOY_HOST_CC}" --profile "${echoy_fixed_profile}" --output "${echoy_fixed_bin}"
        DEPENDS "${echoy_root}/tools/generate_fixed_audio.py" "${echoy_root}/tools/generate_fixed_audio.c"
            "${CMAKE_CURRENT_LIST_DIR}/echoy_audio.c" "${CMAKE_CURRENT_LIST_DIR}/echoy_music.c"
            "${CMAKE_CURRENT_LIST_DIR}/echoy_device_pcm.c" ${echoy_fixed_headers}
        VERBATIM)
    target_add_binary_data(${COMPONENT_LIB} "${echoy_fixed_bin}" BINARY ALIGN 4)
    target_sources(${COMPONENT_LIB} PRIVATE echoy_fixed_audio.c echoy_fixed_audio_embedded.c)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ECHOY_USE_FIXED_AUDIO=1)
    if(CONFIG_ECHOY_VERIFY_PREBUILT_AUDIO)
        target_sources(${COMPONENT_LIB} PRIVATE echoy_fixed_audio_verify.c)
    endif()
endif()
