cmake_minimum_required(VERSION 3.16)

# Keep generated configuration out of the source tree.
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS
        "sdkconfig.defaults;sdkconfig.application.defaults")
endif()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../esp-mosaico-vibe/components/esp_mosaico_app_recovery"
    "${CMAKE_CURRENT_LIST_DIR}/../esp-mosaico-bsp/components/mosaico_network"
    "${CMAKE_CURRENT_LIST_DIR}/../esp-mosaico-bsp/components/mosaico_peer_link"
    "${CMAKE_CURRENT_LIST_DIR}/../esp-mosaico-bsp/components/mosaico_interaction")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(echoy VERSION 0.1.0)

include("${CMAKE_CURRENT_LIST_DIR}/../esp-mosaico-vibe/cmake/system_update.cmake")
