# ── C Core tests ───────────────────────────────────────────
add_executable(test_c_core
    c_tests/test_c_core.c
)
target_link_libraries(test_c_core PRIVATE cerberus_c)
add_test(NAME CCoreTests COMMAND test_c_core)

# ── C++ Engine tests ───────────────────────────────────────
add_executable(test_cpp_engine
    cpp_tests/test_cpp_engine.cpp
)
target_link_libraries(test_cpp_engine PRIVATE
    cerberus_cpp
    cerberus_c
)
add_test(NAME CppEngineTests COMMAND test_cpp_engine)