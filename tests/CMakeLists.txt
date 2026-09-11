add_test(
    NAME scaffold_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DECHOY_PROJECT_ROOT=${CMAKE_CURRENT_LIST_DIR}/.."
            -P "${CMAKE_CURRENT_LIST_DIR}/check_scaffold.cmake")

add_test(NAME core_runner_smoke
         COMMAND $<TARGET_FILE:echoy_core_runner> --self-test)
set_tests_properties(core_runner_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "Echoy core runner ready")
