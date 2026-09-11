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
