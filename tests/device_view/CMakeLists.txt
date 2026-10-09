cmake_minimum_required(VERSION 3.20)
project(echoy_device_view_test LANGUAGES C)
get_filename_component(APP "../../projects/echoy" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(ESP_GSP_COMPONENT_DIR "${APP}/managed_components/espressif__esp-gsp")
add_subdirectory("${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge" sim_bridge)
gsp_add_backend(echoy_backend
    SOURCES health_fixture.c
        "${APP}/main/echoy_ui.c" "${APP}/main/echoy_strip.c" "${APP}/main/echoy_strip_image.c"
        "${APP}/main/echoy_model.c" "${APP}/main/echoy_music.c" "${APP}/main/echoy_view_cache.c"
        "${APP}/main/echoy_art.c" "${APP}/main/echoy_art_image.c"
    INCLUDE_DIRECTORIES "${APP}/main"
    SCENES "${APP}/ui/main.json")
target_link_libraries(echoy_backend PRIVATE m)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(echoy_backend PRIVATE "${APP}/pc/loopback_transport.c")
    target_link_options(echoy_backend PRIVATE "-Wl,--wrap=recv")
endif()
