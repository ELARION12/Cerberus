add_executable(bench_native bench_main.cpp)
target_link_libraries(bench_native PRIVATE geryon_cpp geryon_c)
