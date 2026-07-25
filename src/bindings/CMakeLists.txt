# Bindings — Phase 4
# ── pybind11 Python module ─────────────────────────────────
pybind11_add_module(_coreagent_cpp
    pybind_main.cpp
)

# Link against both engine layers
target_link_libraries(_coreagent_cpp PRIVATE
    coreagent_cpp
    coreagent_c
    Threads::Threads
)

# Include all C++ headers
target_include_directories(_coreagent_cpp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/agent
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/tools
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/memory
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/core
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/parser
    ${CMAKE_SOURCE_DIR}/src/c_core/memory
    ${CMAKE_SOURCE_DIR}/src/c_core/threading
    ${CMAKE_SOURCE_DIR}/src/c_core/executor
)

# Copy the .so into python/coreagent/ after build
add_custom_command(TARGET _coreagent_cpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:_coreagent_cpp>
        ${CMAKE_SOURCE_DIR}/python/coreagent/$<TARGET_FILE_NAME:_coreagent_cpp>
    COMMENT "📦 Copying _coreagent_cpp → python/coreagent/"
)

message(STATUS "✅ Python bindings configured")