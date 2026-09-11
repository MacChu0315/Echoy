cmake_minimum_required(VERSION 3.16)
project(echoy_host LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(echoy_core STATIC
    ../components/echoy_core/phrase.c
    ../components/echoy_core/response.c
    ../components/echoy_core/harmony.c
    ../components/echoy_core/core.c
    ../components/echoy_core/session.c)
target_include_directories(echoy_core PUBLIC ../components/echoy_core/include)
target_compile_options(echoy_core PRIVATE -Wall -Wextra -Werror)
target_link_libraries(echoy_core PUBLIC m)
add_executable(echoy_core_runner core_runner.c)
target_link_libraries(echoy_core_runner PRIVATE echoy_core)
target_compile_options(echoy_core_runner PRIVATE -Wall -Wextra -Werror)

include(CTest)
add_library(echoy_audio STATIC ../components/echoy_audio/audio.c ../components/echoy_audio/output.c)
target_include_directories(echoy_audio PUBLIC ../components/echoy_audio/include)
target_link_libraries(echoy_audio PUBLIC echoy_core m)
target_compile_options(echoy_audio PRIVATE -Wall -Wextra -Werror)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../tests"
                 "${CMAKE_CURRENT_BINARY_DIR}/tests")

add_executable(echoy_audio_render audio_render.c)
target_link_libraries(echoy_audio_render PRIVATE echoy_audio)
target_compile_options(echoy_audio_render PRIVATE -Wall -Wextra -Werror -UNDEBUG)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(../managed_components/lvgl__lvgl ${CMAKE_CURRENT_BINARY_DIR}/lvgl)
add_library(echoy_ui STATIC ../components/echoy_ui/touch.c ../components/echoy_ui/i18n.c ../components/echoy_ui/fonts.c ../components/echoy_ui/view.c)
target_include_directories(echoy_ui PUBLIC ../components/echoy_ui/include)
target_link_libraries(echoy_ui PUBLIC echoy_core lvgl m)
target_compile_options(echoy_ui PRIVATE -Wall -Wextra -Werror)
add_executable(echoy_ui_runner ui_runner.c)
target_link_libraries(echoy_ui_runner PRIVATE echoy_ui)
