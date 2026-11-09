cmake_minimum_required(VERSION 3.18)
project(Cerberus VERSION 1.4.0 LANGUAGES C CXX)

# ── Standards ─────────────────────────────────────────────
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Build Type ─────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Compiler Flags ─────────────────────────────────────────
set(CMAKE_C_FLAGS_RELEASE   "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG     "-g -fsanitize=address -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -fsanitize=address -Wall -Wextra")

# ── Find Packages ──────────────────────────────────────────
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# ── pybind11 (auto-download if missing) ───────────────────
include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.11.1
)
FetchContent_MakeAvailable(pybind11)

# ── Subdirectories ─────────────────────────────────────────
add_subdirectory(src/c_core)
add_subdirectory(src/cpp_engine)
add_subdirectory(src/bindings)

# ── Tests ──────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

message(STATUS "✅ Cerberus configured: ${CMAKE_BUILD_TYPE}")
message(STATUS "   C compiler  : ${CMAKE_C_COMPILER}")
message(STATUS "   C++ compiler: ${CMAKE_CXX_COMPILER}")