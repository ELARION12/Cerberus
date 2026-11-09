# Bindings — Phase 4
# ── pybind11 Python module ─────────────────────────────────
pybind11_add_module(_cerberus_core
    pybind_main.cpp
)

# Link against both engine layers
target_link_libraries(_cerberus_core PRIVATE
    cerberus_cpp
    cerberus_c
    Threads::Threads
)

# Include all C++ headers
target_include_directories(_cerberus_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/agent
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/tools
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/memory
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/core
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/parser
    ${CMAKE_SOURCE_DIR}/src/c_core/memory
    ${CMAKE_SOURCE_DIR}/src/c_core/threading
    ${CMAKE_SOURCE_DIR}/src/c_core/executor
)

# Copy the .so into python/cerberus_agent/ after build
add_custom_command(TARGET _cerberus_core POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:_cerberus_core>
        ${CMAKE_SOURCE_DIR}/python/cerberus_agent/$<TARGET_FILE_NAME:_cerberus_core>
    COMMENT "📦 Copying _cerberus_core → python/cerberus_agent/"
)

message(STATUS "✅ Python bindings configured")