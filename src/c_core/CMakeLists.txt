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
add_library(geryon_c STATIC ${C_CORE_SOURCES})

target_include_directories(geryon_c PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/threading
    ${CMAKE_CURRENT_SOURCE_DIR}/executor
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/util
)

target_link_libraries(geryon_c PUBLIC Threads::Threads)
if(GERYON_RT_LIB)
    target_link_libraries(geryon_c PUBLIC ${GERYON_RT_LIB})
endif()

# Compiler warnings
target_compile_options(geryon_c PRIVATE
    -Wall -Wextra -Wpedantic
)