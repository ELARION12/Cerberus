#pragma once

/*
 * SQ8Quantizer — Scalar 8-Bit Quantization for SIMD Vector Compression
 * ===================================================================
 * Compresses 32-bit float vector embeddings into 8-bit int8/uint8 codes
 * with per-vector min and scale offsets.
 *
 * Provides:
 *  • 4x Memory Reduction (float32 → uint8_t)
 *  • Fast SIMD Integer Dot-Product (AVX2 & NEON) with automatic scalar fallback
 */

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

#if defined(__AVX2__)
#  include <immintrin.h>
#  define CA_SIMD_AVX2 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#  include <arm_neon.h>
#  define CA_SIMD_NEON 1
#endif

namespace coreagent {

struct SQ8Vector {
    std::vector<uint8_t> codes;
    float min_val;
    float scale;
};

class SQ8Quantizer {
public:
    static SQ8Vector quantize(const float* vec, uint32_t dim) {
        if (dim == 0) return {{}, 0.0f, 0.0f};

        float min_val = vec[0];
        float max_val = vec[0];
        for (uint32_t i = 1; i < dim; ++i) {
            min_val = std::min(min_val, vec[i]);
            max_val = std::max(max_val, vec[i]);
        }

        float range = max_val - min_val;
        float scale = (range > 1e-8f) ? (255.0f / range) : 0.0f;

        SQ8Vector sq;
        sq.codes.resize(dim);
        sq.min_val = min_val;
        sq.scale   = scale;

        if (scale > 0.0f) {
            for (uint32_t i = 0; i < dim; ++i) {
                float q = (vec[i] - min_val) * scale;
                sq.codes[i] = static_cast<uint8_t>(std::clamp(std::round(q), 0.0f, 255.0f));
            }
        } else {
            std::fill(sq.codes.begin(), sq.codes.end(), 0);
        }

        return sq;
    }

    static void dequantize(const SQ8Vector& sq, float* out, uint32_t dim) {
        if (sq.scale > 0.0f) {
            float inv_scale = 1.0f / sq.scale;
            for (uint32_t i = 0; i < dim; ++i) {
                out[i] = sq.min_val + (static_cast<float>(sq.codes[i]) * inv_scale);
            }
        } else {
            for (uint32_t i = 0; i < dim; ++i) {
                out[i] = sq.min_val;
            }
        }
    }

    /* Compute approximate dot product between float query and SQ8 quantized vector */
    static float dot_product(const float* q, const SQ8Vector& sq, uint32_t dim) {
        if (sq.scale <= 0.0f) return 0.0f;
        float inv_scale = 1.0f / sq.scale;

        float sum_q = 0.0f;
        float dot_raw = 0.0f;

        const uint8_t* codes = sq.codes.data();
        uint32_t i = 0;

#if defined(CA_SIMD_AVX2)
        __m256 v_dot = _mm256_setzero_ps();
        __m256 v_sum = _mm256_setzero_ps();

        for (; i + 8 <= dim; i += 8) {
            __m256 vq = _mm256_loadu_ps(q + i);
            v_sum = _mm256_add_ps(v_sum, vq);

            /* Convert 8 uint8_t codes to float */
            __m128i c8 = _mm_loadu_si64(codes + i);
            __m256i c32 = _mm256_cvtepu8_epi32(c8);
            __m256 vc = _mm256_cvtepi32_ps(c32);

            v_dot = _mm256_fmadd_ps(vq, vc, v_dot);
        }

        /* Horizontal sums */
        __m128 lo_d = _mm256_castps256_ps128(v_dot);
        __m128 hi_d = _mm256_extractf128_ps(v_dot, 1);
        __m128 sum_d = _mm_add_ps(lo_d, hi_d);
        sum_d = _mm_hadd_ps(sum_d, sum_d);
        sum_d = _mm_hadd_ps(sum_d, sum_d);
        dot_raw = _mm_cvtss_f32(sum_d);

        __m128 lo_s = _mm256_castps256_ps128(v_sum);
        __m128 hi_s = _mm256_extractf128_ps(v_sum, 1);
        __m128 sum_s = _mm_add_ps(lo_s, hi_s);
        sum_s = _mm_hadd_ps(sum_s, sum_s);
        sum_s = _mm_hadd_ps(sum_s, sum_s);
        sum_q = _mm_cvtss_f32(sum_s);

#elif defined(CA_SIMD_NEON)
        float32x4_t v_dot = vdupq_n_f32(0.0f);
        float32x4_t v_sum = vdupq_n_f32(0.0f);

        for (; i + 4 <= dim; i += 4) {
            float32x4_t vq = vld1q_f32(q + i);
            v_sum = vaddq_f32(v_sum, vq);

            uint8x8_t c8 = vld1_u8(codes + i);
            uint16x4_t c16 = vget_low_u16(vmovl_u8(c8));
            uint32x4_t c32 = vmovl_u16(c16);
            float32x4_t vc = vcvtq_f32_u32(c32);

            v_dot = vmlaq_f32(v_dot, vq, vc);
        }
        dot_raw = vaddvq_f32(v_dot);
        sum_q   = vaddvq_f32(v_sum);
#endif

        /* Scalar tail processing */
        for (; i < dim; ++i) {
            sum_q += q[i];
            dot_raw += q[i] * static_cast<float>(codes[i]);
        }

        return (sq.min_val * sum_q) + (inv_scale * dot_raw);
    }
};

} // namespace coreagent
