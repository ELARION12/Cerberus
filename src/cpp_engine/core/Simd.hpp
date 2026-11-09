#pragma once

/*
 * Simd.hpp - picks the vector backend once, at run time.
 *
 * x86-64:  AVX2 + FMA kernels are always compiled (through a per-function
 *          target attribute) and only used if the CPU reports both features.
 *          The rest of the build stays at the plain x86-64 baseline, so a
 *          binary built here still starts on an older CPU.
 * aarch64: NEON is part of the ISA, so it is always used.
 * anything else: scalar code.
 *
 * Set CERBERUS_SIMD=scalar in the environment to force the scalar path.
 */

#include <cstdlib>
#include <cstring>

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#  include <immintrin.h>
#  define CA_SIMD_X86 1
#  define CA_TARGET_AVX2 __attribute__((target("avx2,fma")))
#elif defined(__aarch64__)
#  include <arm_neon.h>
#  define CA_SIMD_NEON 1
#endif

namespace cerberus {
namespace simd {

enum class Backend { Scalar, AVX2, NEON };

inline Backend detect() {
    const char* force = std::getenv("CERBERUS_SIMD");
    if (force && std::strcmp(force, "scalar") == 0) return Backend::Scalar;
#if defined(CA_SIMD_X86)
    if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma"))
        return Backend::AVX2;
    return Backend::Scalar;
#elif defined(CA_SIMD_NEON)
    return Backend::NEON;
#else
    return Backend::Scalar;
#endif
}

inline Backend active() {
    static const Backend b = detect();
    return b;
}

inline const char* name(Backend b) {
    switch (b) {
        case Backend::AVX2: return "avx2";
        case Backend::NEON: return "neon";
        default:            return "scalar";
    }
}

} // namespace simd
} // namespace cerberus
