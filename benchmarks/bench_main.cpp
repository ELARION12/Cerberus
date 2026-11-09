// Native timings for the context buffer and the arena allocator.
//
//   cmake -B build -DCMAKE_BUILD_TYPE=Release -DCERBERUS_BUILD_BENCHMARKS=ON
//   cmake --build build --target bench_native
//   ./build/benchmarks/bench_native
//
// Single-threaded, one run. Treat the numbers as rough; run it a few times.

#include "memory/ContextBuffer.hpp"
#include <chrono>
#include <cstdio>
#include <string>


using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

/* ── Bench 1: ContextBuffer push throughput ─────────────── */
void bench_context_push(size_t num_messages, size_t msg_len) {
    const size_t arena = num_messages * (msg_len + 64) * 2;
    cerberus::ContextBuffer buf(arena, 1 << 20, 1'000'000'000); // token budget too large to ever trim
    std::string sample(msg_len, 'x');

    auto start = Clock::now();
    for (size_t i = 0; i < num_messages; ++i) {
        if (i % 2 == 0) buf.addUser(sample);
        else            buf.addAssistant(sample);
    }
    auto end = Clock::now();

    double ms = elapsed_ms(start, end);
    printf("[push]   %zu msgs x %zu chars : %.3f ms  (%.1f ns/msg)\n",
           num_messages, msg_len, ms, (ms * 1e6) / num_messages);
}

/* ── Bench 2: buildPrompt() throughput ──────────────────── */
void bench_build_prompt(size_t num_messages, size_t msg_len) {
    const size_t arena = num_messages * (msg_len + 64) * 2;
    cerberus::ContextBuffer buf(arena, arena, 1'000'000'000);
    std::string sample(msg_len, 'x');
    for (size_t i = 0; i < num_messages; ++i) buf.addUser(sample);

    auto start = Clock::now();
    std::string_view prompt = buf.buildPrompt();
    auto end = Clock::now();

    double ms = elapsed_ms(start, end);
    printf("[build]  %zu msgs -> %zu byte prompt : %.3f ms\n",
           num_messages, prompt.size(), ms);
}

/* ── Bench 3: trim() under pressure (O(1) with deque) ───── */
void bench_trim_pressure(size_t max_tokens, size_t num_messages, size_t msg_len) {
    cerberus::ContextBuffer buf(4 << 20, 1 << 20, max_tokens);
    std::string sample(msg_len, 'x');

    auto start = Clock::now();
    for (size_t i = 0; i < num_messages; ++i) buf.addUser(sample);
    auto end = Clock::now();

    double ms = elapsed_ms(start, end);
    printf("[trim]   %zu msgs into %zu-token window : %.3f ms  (final count=%zu)\n",
           num_messages, max_tokens, ms, buf.messageCount());
}

extern "C" {
    #include "../src/c_core/memory/allocator.h"
}

/* ── Bench 4: Arena Allocator SIMD Alignment & Throughput ─ */
void bench_arena(size_t num_allocs, size_t alloc_size) {
    // Spin up a 512 MB arena
    ca_arena_t* arena = ca_arena_create(512 * 1024 * 1024); 
    
    auto start = Clock::now();
    for (size_t i = 0; i < num_allocs; ++i) {
        void* ptr = ca_arena_alloc(arena, alloc_size);
        
        // Trap unaligned pointers immediately
        if (reinterpret_cast<uintptr_t>(ptr) % 32 != 0) {
            printf("[arena]  FAILURE: Pointer at alloc %zu is NOT 32-byte aligned for AVX2!\n", i);
            break;
        }
    }
    auto end = Clock::now();

    double ms = elapsed_ms(start, end);
    printf("[arena]  %zu allocs x %zu bytes : %.3f ms (%.2f ns/alloc)\n",
           num_allocs, alloc_size, ms, (ms * 1e6) / num_allocs);

    ca_arena_destroy(arena);
}

int main() {
    printf("=== Cerberus native benchmarks ===\n\n");

    bench_context_push(10'000, 100);
    bench_context_push(10'000, 1000);
    bench_build_prompt(10'000, 200);
    bench_trim_pressure(4096, 5'000, 100);   // window stays small -> lots of erases
    bench_trim_pressure(1'000'000, 5'000, 100); // window huge -> no trims, control case
    bench_arena(1'000'000, 128); // 1 million allocations of 128 bytes
    return 0;
}