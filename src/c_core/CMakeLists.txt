# Gather all C source files
file(GLOB_RECURSE C_CORE_SOURCES
    "memory/*.c"
    "threading/*.c"
    "executor/*.c"
    "io/*.c"
    "telemetry/*.c"
)

# Build as static library
add_library(coreagent_c STATIC ${C_CORE_SOURCES})

target_include_directories(coreagent_c PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/threading
    ${CMAKE_CURRENT_SOURCE_DIR}/executor
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
)

target_link_libraries(coreagent_c PUBLIC Threads::Threads)

# Compiler warnings
target_compile_options(coreagent_c PRIVATE
    -Wall -Wextra -Wpedantic
)