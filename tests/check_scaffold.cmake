# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED ECHOY_PROJECT_ROOT)
    message(FATAL_ERROR "ECHOY_PROJECT_ROOT is required")
endif()

foreach(required_path IN ITEMS
        "main/main.c"
        "pc/CMakeLists.txt")
    if(NOT EXISTS "${ECHOY_PROJECT_ROOT}/${required_path}")
        message(FATAL_ERROR "missing Echoy scaffold entry: ${required_path}")
    endif()
endforeach()
