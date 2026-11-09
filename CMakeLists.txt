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

# ── Options ────────────────────────────────────────────────
option(CERBERUS_BUILD_TESTS "Build the C and C++ test programs" ON)
option(CERBERUS_NATIVE_ARCH "Tune for this machine with -march=native (never for builds you ship)" OFF)

# ── Compiler Flags ─────────────────────────────────────────
# No -march here: the build must run on any CPU of its architecture. AVX2 and
# NEON kernels are picked at run time instead (see src/cpp_engine/core/Simd.hpp).
set(CMAKE_C_FLAGS_RELEASE   "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
if(CERBERUS_NATIVE_ARCH)
  add_compile_options(-march=native)
endif()
set(CMAKE_C_FLAGS_DEBUG     "-g -fsanitize=address -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -fsanitize=address -Wall -Wextra")

# ── Find Packages ──────────────────────────────────────────
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

# shm_open lives in librt on older glibc (manylinux2014); newer glibc has it in libc
find_library(CERBERUS_RT_LIB rt)

# ── pybind11: use an installed copy, download one only if none is found ──
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.13.6
  )
  FetchContent_MakeAvailable(pybind11)
endif()

# ── Subdirectories ─────────────────────────────────────────
add_subdirectory(src/c_core)
add_subdirectory(src/cpp_engine)
add_subdirectory(src/bindings)

# ── Tests ──────────────────────────────────────────────────
if(CERBERUS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "✅ Cerberus configured: ${CMAKE_BUILD_TYPE}")
message(STATUS "   C compiler  : ${CMAKE_C_COMPILER}")
message(STATUS "   C++ compiler: ${CMAKE_CXX_COMPILER}")