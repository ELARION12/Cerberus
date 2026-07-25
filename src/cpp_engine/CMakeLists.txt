# C++ Engine — Phase 2 & V1.2.0
# Collect all C++ engine source files
file(GLOB_RECURSE CPP_ENGINE_SOURCES
    "agent/*.cpp"
    "tools/*.cpp"
    "parser/*.cpp"
)

# Build as static library
add_library(coreagent_cpp STATIC ${CPP_ENGINE_SOURCES})

# Expose all subdirectory headers
target_include_directories(coreagent_cpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/parser
)

# Link against C core (inherits its include dirs too)
target_link_libraries(coreagent_cpp PUBLIC
    coreagent_c
    Threads::Threads
)

target_compile_options(coreagent_cpp PRIVATE
    -Wall -Wextra
)

message(STATUS "✅ C++ Engine layer configured")