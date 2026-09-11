cmake_minimum_required(VERSION 3.16)
project(echoy_host LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(echoy_core_runner core_runner.c)
target_compile_options(echoy_core_runner PRIVATE -Wall -Wextra -Werror)

include(CTest)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../tests"
                 "${CMAKE_CURRENT_BINARY_DIR}/tests")
