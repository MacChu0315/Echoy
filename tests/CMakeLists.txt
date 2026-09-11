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
