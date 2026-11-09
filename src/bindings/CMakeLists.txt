# Bindings — Phase 4
# ── pybind11 Python module ─────────────────────────────────
pybind11_add_module(_geryon_core
    pybind_main.cpp
)

# Link against both engine layers
target_link_libraries(_geryon_core PRIVATE
    geryon_cpp
    geryon_c
    Threads::Threads
)

# Include all C++ headers
target_include_directories(_geryon_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/agent
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/tools
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/memory
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/core
    ${CMAKE_SOURCE_DIR}/src/cpp_engine/parser
    ${CMAKE_SOURCE_DIR}/src/c_core/memory
    ${CMAKE_SOURCE_DIR}/src/c_core/threading
    ${CMAKE_SOURCE_DIR}/src/c_core/executor
)

# Wheel builds (scikit-build-core) use this install rule
install(TARGETS _geryon_core LIBRARY DESTINATION geryon)

# Local dev builds: also drop the module next to the Python sources so
# `PYTHONPATH=python` works without installing
if(NOT SKBUILD)
  add_custom_command(TARGET _geryon_core POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy
          $<TARGET_FILE:_geryon_core>
          ${CMAKE_SOURCE_DIR}/python/geryon/$<TARGET_FILE_NAME:_geryon_core>
      COMMENT "Copying _geryon_core into python/geryon/"
  )
endif()

message(STATUS "✅ Python bindings configured")