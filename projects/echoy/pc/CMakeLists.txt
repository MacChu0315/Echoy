cmake_minimum_required(VERSION 3.20)
project(echoy_backend LANGUAGES C)

if(NOT ESP_GSP_COMPONENT_DIR AND DEFINED ENV{ESP_GSP_COMPONENT_DIR})
    set(ESP_GSP_COMPONENT_DIR "$ENV{ESP_GSP_COMPONENT_DIR}")
endif()
if(NOT ESP_GSP_COMPONENT_DIR)
    foreach(candidate IN ITEMS
            "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__esp-gsp")
        get_filename_component(candidate "${candidate}" ABSOLUTE)
        if(EXISTS "${candidate}/tools/sim_bridge/CMakeLists.txt")
            set(ESP_GSP_COMPONENT_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge/CMakeLists.txt")
    message(FATAL_ERROR
        "Install espressif/esp-gsp first, or set ESP_GSP_COMPONENT_DIR")
endif()

add_subdirectory("${ESP_GSP_COMPONENT_DIR}/tools/sim_bridge" sim_bridge)
gsp_add_backend(echoy_backend
    SOURCES ../main/echoy_ui.c ../main/echoy_strip.c ../main/echoy_strip_image.c ../main/echoy_model.c ../main/echoy_music.c ../main/echoy_view_cache.c ../main/echoy_art.c ../main/echoy_art_image.c platform_pc.c
    INCLUDE_DIRECTORIES ../main
    SCENES ../ui/main.json)

target_link_libraries(echoy_backend PRIVATE m)
target_sources(echoy_backend PRIVATE ../main/echoy_display.c)

find_package(Threads REQUIRED)
target_sources(echoy_backend PRIVATE ../main/echoy_art_producer.c)
target_link_libraries(echoy_backend PRIVATE Threads::Threads)
add_executable(echoy_desk desk_main.c
    ../main/echoy_ui.c ../main/echoy_strip.c ../main/echoy_strip_image.c ../main/echoy_model.c ../main/echoy_music.c
    ../main/echoy_view_cache.c ../main/echoy_art.c ../main/echoy_art_image.c
    ../main/echoy_band.c ../main/echoy_audio.c ../main/echoy_pickup.c ../main/echoy_arrange.c)
add_dependencies(echoy_desk echoy_backend_bundle)
target_sources(echoy_desk PRIVATE ../main/echoy_display.c)
target_sources(echoy_desk PRIVATE ../main/echoy_art_producer.c)
target_include_directories(echoy_desk PRIVATE ../main "${CMAKE_CURRENT_BINARY_DIR}/echoy_backend_assets")
target_link_libraries(echoy_desk PRIVATE esp_gsp::sim_bridge Threads::Threads m)
target_compile_options(echoy_desk PRIVATE -Wall -Wextra)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach(target echoy_backend echoy_desk)
        target_sources(${target} PRIVATE loopback_transport.c)
        target_link_options(${target} PRIVATE "-Wl,--wrap=recv")
    endforeach()
endif()

# Separate native executable with three actual protocol/band/PCM owners. The
# existing shared-band reference desk remains a separate target and oracle.
get_target_property(echoy_chain_sources echoy_desk SOURCES)
add_executable(echoy_chain_desk ${echoy_chain_sources} chain_owner.c
    ../main/echoy_radio.c ../main/echoy_group.c ../main/echoy_link_protocol.c ../main/echoy_link_codec.c)
add_dependencies(echoy_chain_desk echoy_backend_bundle)
target_compile_definitions(echoy_chain_desk PRIVATE ECHOY_INDEPENDENT_CHAIN=1)
target_include_directories(echoy_chain_desk PRIVATE ../main "${CMAKE_CURRENT_BINARY_DIR}/echoy_backend_assets")
target_link_libraries(echoy_chain_desk PRIVATE esp_gsp::sim_bridge Threads::Threads m)
target_compile_options(echoy_chain_desk PRIVATE -Wall -Wextra)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(echoy_chain_desk PRIVATE "-Wl,--wrap=recv")
endif()

option(ECHOY_PREBUILT_AUDIO "Exercise build-time fixed audio in the native desk" OFF)
if(ECHOY_PREBUILT_AUDIO)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(echoy_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    set(fixed_source "${CMAKE_CURRENT_BINARY_DIR}/echoy_fixed_audio_data.c")
    file(GLOB fixed_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../main/*.h")
    add_custom_command(OUTPUT "${fixed_source}" "${CMAKE_CURRENT_BINARY_DIR}/echoy_fixed_audio.bin"
        "${CMAKE_CURRENT_BINARY_DIR}/echoy_fixed_audio.json"
        COMMAND "${Python3_EXECUTABLE}" "${echoy_root}/tools/generate_fixed_audio.py"
            --cc "${CMAKE_C_COMPILER}" --profile reference
            --output "${CMAKE_CURRENT_BINARY_DIR}/echoy_fixed_audio.bin" --c-source "${fixed_source}"
        DEPENDS "${echoy_root}/tools/generate_fixed_audio.py" "${echoy_root}/tools/generate_fixed_audio.c"
            ../main/echoy_audio.c ../main/echoy_music.c ../main/echoy_device_pcm.c ${fixed_headers}
        VERBATIM)
    add_library(echoy_fixed_audio STATIC "${fixed_source}" ../main/echoy_fixed_audio.c
        ../main/echoy_fixed_audio_embedded.c)
    target_include_directories(echoy_fixed_audio PRIVATE ../main)
    foreach(target echoy_desk echoy_chain_desk)
        target_compile_definitions(${target} PRIVATE ECHOY_USE_FIXED_AUDIO=1)
        target_link_libraries(${target} PRIVATE echoy_fixed_audio)
    endforeach()
endif()
