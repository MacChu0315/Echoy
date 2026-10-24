# Shared by ESP-IDF and the native desk; compiler must execute on the build host.
function(echoy_add_triangle_tables target root compiler python)
    set(table_dir "${CMAKE_BINARY_DIR}/echoy_triangle_tables")
    if(NOT TARGET echoy_triangle_tables)
        file(GLOB table_headers CONFIGURE_DEPENDS "${root}/projects/echoy/main/*.h")
        add_custom_command(OUTPUT "${table_dir}/echoy_triangle_tables.h"
            "${table_dir}/echoy_triangle_tables.bin" "${table_dir}/echoy_triangle_tables.json"
            COMMAND "${python}" "${root}/tools/generate_triangle_tables.py"
                --cc "${compiler}" --output-dir "${table_dir}"
            DEPENDS "${root}/tools/generate_triangle_tables.py" "${root}/tools/generate_triangle_tables.c"
                "${root}/projects/echoy/main/echoy_audio.c" "${root}/projects/echoy/main/echoy_music.c"
                ${table_headers}
            VERBATIM)
        add_custom_target(echoy_triangle_tables DEPENDS "${table_dir}/echoy_triangle_tables.h"
            "${table_dir}/echoy_triangle_tables.bin" "${table_dir}/echoy_triangle_tables.json")
    endif()
    add_dependencies(${target} echoy_triangle_tables)
    target_include_directories(${target} PRIVATE "${table_dir}")
    target_compile_definitions(${target} PRIVATE ECHOY_USE_PREBUILT_TRIANGLES=1)
endfunction()
