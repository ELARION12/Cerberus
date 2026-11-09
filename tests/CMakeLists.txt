# ── C Core tests ───────────────────────────────────────────
add_executable(test_c_core
    c_tests/test_c_core.c
)
target_link_libraries(test_c_core PRIVATE cerberus_c)
# assert() is the whole test harness, so keep it on even in Release builds
target_compile_options(test_c_core PRIVATE -UNDEBUG)
add_test(NAME CCoreTests COMMAND test_c_core)

# ── C++ Engine tests ───────────────────────────────────────
add_executable(test_cpp_engine
    cpp_tests/test_cpp_engine.cpp
)
target_link_libraries(test_cpp_engine PRIVATE
    cerberus_cpp
    cerberus_c
)
target_compile_options(test_cpp_engine PRIVATE -UNDEBUG)
add_test(NAME CppEngineTests COMMAND test_cpp_engine)

# Same suite again with the vector kernels switched off
add_test(NAME CppEngineTestsScalar COMMAND test_cpp_engine)
set_tests_properties(CppEngineTestsScalar PROPERTIES ENVIRONMENT "CERBERUS_SIMD=scalar")
