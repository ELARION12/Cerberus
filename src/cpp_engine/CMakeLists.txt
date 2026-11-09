# C++ Engine — Phase 2, V1.2.0 & V1.3.0
# Collect all C++ engine source files
file(GLOB_RECURSE CPP_ENGINE_SOURCES
    "agent/*.cpp"
    "tools/*.cpp"
    "parser/*.cpp"
    "swarm/*.cpp"
)

# Build as static library
add_library(geryon_cpp STATIC ${CPP_ENGINE_SOURCES})

# Expose all subdirectory headers
target_include_directories(geryon_cpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/parser
    ${CMAKE_CURRENT_SOURCE_DIR}/swarm
)

# Link against C core (inherits its include dirs too)
target_link_libraries(geryon_cpp PUBLIC
    geryon_c
    Threads::Threads
)

target_compile_options(geryon_cpp PRIVATE
    -Wall -Wextra
)

message(STATUS "✅ C++ Engine layer configured")