add_test(
    NAME scaffold_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DECHOY_PROJECT_ROOT=${CMAKE_CURRENT_LIST_DIR}/.."
            -P "${CMAKE_CURRENT_LIST_DIR}/check_scaffold.cmake")

add_test(NAME core_runner_smoke
         COMMAND $<TARGET_FILE:echoy_core_runner> --self-test)
set_tests_properties(core_runner_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "Echoy core runner ready")

add_executable(test_music test_music.c)
target_link_libraries(test_music PRIVATE echoy_core)
target_compile_options(test_music PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME music_unit COMMAND test_music)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME music_oracle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/compare_oracle.py"
    --runner $<TARGET_FILE:echoy_core_runner>)
add_test(NAME music_voicings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_voicings.py" --check)
add_test(NAME music_runner_validation COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/test_music_runner.py" $<TARGET_FILE:echoy_core_runner>)

add_executable(test_state test_state.c)
target_link_libraries(test_state PRIVATE echoy_core)
target_compile_options(test_state PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME state_unit COMMAND test_state)
add_executable(state_runner state_runner.c)
target_link_libraries(state_runner PRIVATE echoy_core)
target_compile_options(state_runner PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME state_oracle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/compare_state.py" $<TARGET_FILE:state_runner>)

add_executable(test_dsp test_dsp.c)
target_link_libraries(test_dsp PRIVATE echoy_audio)
target_compile_options(test_dsp PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME dsp_unit COMMAND test_dsp)

add_executable(test_dsp_output test_dsp_output.c)
target_link_libraries(test_dsp_output PRIVATE echoy_audio)
target_compile_options(test_dsp_output PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME dsp_output COMMAND test_dsp_output)
add_test(NAME dsp_artifacts COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/test_dsp_artifacts.py" $<TARGET_FILE:echoy_audio_render>)
add_executable(test_dsp_density test_dsp_density.c)
target_link_libraries(test_dsp_density PRIVATE echoy_audio)
target_compile_options(test_dsp_density PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME dsp_density COMMAND test_dsp_density)

add_executable(test_ui_touch test_ui_touch.c ../components/echoy_ui/touch.c)
target_include_directories(test_ui_touch PRIVATE ../components/echoy_ui/include)
target_compile_options(test_ui_touch PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME ui_touch COMMAND test_ui_touch)
add_executable(test_ui_view test_ui_view.c)
target_link_libraries(test_ui_view PRIVATE echoy_ui)
target_compile_options(test_ui_view PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME ui_view COMMAND test_ui_view)
add_test(NAME ui_fonts COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_ui_fonts.py" --check)
add_test(NAME ui_visual COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/visual_compare.py"
    --cases "${CMAKE_CURRENT_LIST_DIR}/ui_cases.json"
    --renderer $<TARGET_FILE:echoy_ui_runner>)

foreach(name wire clock session)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE echoy_core)
    target_compile_options(test_${name} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME ${name}_unit COMMAND test_${name})
endforeach()

set(ECHOY_BSP_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../esp-mosaico-bsp/components")
add_executable(test_contact test_contact.c ../components/echoy_platform/contact.c
    "${ECHOY_BSP_COMPONENTS}/mosaico_interaction/mosaico_mag_classifier.c")
target_include_directories(test_contact PRIVATE platform_stubs
    ../components/echoy_platform/include
    "${ECHOY_BSP_COMPONENTS}/mosaico_network/include"
    "${ECHOY_BSP_COMPONENTS}/mosaico_peer_link/include"
    "${ECHOY_BSP_COMPONENTS}/mosaico_interaction/include"
    "${ECHOY_BSP_COMPONENTS}/esp-mosaico-bsp/include"
    ../managed_components/tangyumei3535__bmm150_sensorapi)
target_link_libraries(test_contact PRIVATE echoy_core)
target_compile_options(test_contact PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME session_contact COMMAND test_contact)
add_executable(test_radio test_radio.c ../components/echoy_platform/radio.c)
target_include_directories(test_radio PRIVATE platform_stubs ../components/echoy_platform/include
    "${ECHOY_BSP_COMPONENTS}/mosaico_network/include"
    "${ECHOY_BSP_COMPONENTS}/mosaico_peer_link/include"
    "${ECHOY_BSP_COMPONENTS}/mosaico_interaction/include")
target_link_libraries(test_radio PRIVATE echoy_core)
target_compile_options(test_radio PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME wire_radio COMMAND test_radio)

add_test(NAME session_clock_freshness COMMAND test_session freshness)
add_test(NAME session_clock_unstable COMMAND test_session unstable)
add_test(NAME session_clock_long COMMAND test_session long)

add_library(echoy_services STATIC ../components/echoy_platform/storage.c ../components/echoy_platform/battery.c ../components/echoy_platform/screen_mirror.c)
target_include_directories(echoy_services PUBLIC ../components/echoy_platform/include)
target_link_libraries(echoy_services PUBLIC echoy_core)
target_compile_options(echoy_services PRIVATE -Wall -Wextra -Werror)
add_executable(test_services test_services.c)
target_link_libraries(test_services PRIVATE echoy_services)
target_compile_options(test_services PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME platform_services COMMAND test_services)
add_library(echoy_control STATIC ../components/echoy_platform/control.c)
target_include_directories(echoy_control PUBLIC ../components/echoy_ui/include)
target_link_libraries(echoy_control PUBLIC echoy_services)
target_compile_options(echoy_control PRIVATE -Wall -Wextra -Werror)
add_executable(test_control test_control.c)
target_link_libraries(test_control PRIVATE echoy_control)
target_compile_options(test_control PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME platform_control COMMAND test_control)
add_executable(test_diagnostics test_diagnostics.c ../components/echoy_platform/diagnostics.c)
target_link_libraries(test_diagnostics PRIVATE echoy_control echoy_audio)
target_compile_options(test_diagnostics PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME platform_diagnostics COMMAND test_diagnostics)

add_test(NAME platform_radio_drain COMMAND test_control queued)
