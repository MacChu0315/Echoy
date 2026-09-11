idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        echoy_ui
        echoy_audio
        esp_mosaico_app_recovery
        esp_iris
        freertos
        nvs_flash
)
