# Gather all C source files
file(GLOB_RECURSE C_CORE_SOURCES
    "memory/*.c"
    "threading/*.c"
    "executor/*.c"
    "io/*.c"
    "telemetry/*.c"
    "util/*.c"
)

# Build as static library
add_library(cerberus_c STATIC ${C_CORE_SOURCES})

target_include_directories(cerberus_c PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/threading
    ${CMAKE_CURRENT_SOURCE_DIR}/executor
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/util
)

target_link_libraries(cerberus_c PUBLIC Threads::Threads)
if(CERBERUS_RT_LIB)
    target_link_libraries(cerberus_c PUBLIC ${CERBERUS_RT_LIB})
endif()

# Compiler warnings
target_compile_options(cerberus_c PRIVATE
    -Wall -Wextra -Wpedantic
)