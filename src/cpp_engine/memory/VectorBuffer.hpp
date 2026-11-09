#pragma once

/*
 * VectorBuffer — Embedded SIMD Vector Memory with SQ8 Quantization & HNSW Indexing
 * =================================================================================
 * Stores fixed-dimension float32 embedding vectors together with a string
 * payload (e.g. a chunk of text) inside memory.
 *
 * Upgrades in v1.4.0:
 *  • SQ8 Scalar Quantization: 8-bit int quantization for 4x memory reduction.
 *  • HNSW Graph Indexing: Native C++ HNSW graph for O(log N) fast top-k queries.
 */

#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cassert>
#include <stdexcept>
#include <cstdint>
#include <memory>

#include "SQ8Quantizer.hpp"
#include "HNSWIndex.hpp"

/* SIMD detection */
#if defined(__AVX2__)
#  include <immintrin.h>
#  define CA_SIMD_AVX2 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#  include <arm_neon.h>
#  define CA_SIMD_NEON 1
#endif

namespace cerberus {

/* ── Result of a nearest-neighbour query ─────────────────────────────────── */
struct VectorMatch {
    uint32_t    index;    /* slot index in the VectorBuffer            */
    float       score;    /* cosine similarity ∈ [−1, 1]               */
    std::string text;     /* associated text payload                   */
};

/* ── VectorBuffer ─────────────────────────────────────────────────────────── */
class VectorBuffer {
public:
    explicit VectorBuffer(uint32_t dim, uint32_t max_vectors = 4096)
        : dim_(dim), capacity_(max_vectors), use_sq8_(false)
    {
        if (dim == 0)
            throw std::invalid_argument("[VectorBuffer] dim must be > 0");
        vectors_.reserve(max_vectors);
        texts_.reserve(max_vectors);
    }

    void enableSQ8(bool enable = true) {
        use_sq8_ = enable;
        if (use_sq8_ && sq8_vectors_.size() < vectors_.size()) {
            sq8_vectors_.resize(vectors_.size());
            for (size_t i = 0; i < vectors_.size(); ++i) {
                sq8_vectors_[i] = SQ8Quantizer::quantize(vectors_[i].data(), dim_);
            }
        }
    }

    void buildHNSWIndex(uint32_t M = 16, uint32_t ef_construction = 64, uint32_t ef_search = 32) {
        hnsw_index_ = std::make_unique<HNSWIndex>(dim_, M, ef_construction, ef_search);
        for (uint32_t i = 0; i < vectors_.size(); ++i) {
            hnsw_index_->insert(i, vectors_[i]);
        }
    }

    bool hasHNSW() const { return hnsw_index_ != nullptr; }
    bool isSQ8Enabled() const { return use_sq8_; }

    uint32_t push(const std::vector<float>& embedding, std::string_view text) {
        if ((uint32_t)embedding.size() != dim_) {
            throw std::invalid_argument(
                "[VectorBuffer] Dimension mismatch: expected " +
                std::to_string(dim_) + " got " +
                std::to_string(embedding.size()));
        }

        std::vector<float> norm_vec = embedding;
        l2_normalize(norm_vec.data(), dim_);

        uint32_t idx = (uint32_t)vectors_.size();
        vectors_.push_back(norm_vec);
        texts_.emplace_back(text);

        if (use_sq8_) {
            sq8_vectors_.push_back(SQ8Quantizer::quantize(norm_vec.data(), dim_));
        }

        if (hnsw_index_) {
            hnsw_index_->insert(idx, norm_vec);
        }

        return idx;
    }

    std::vector<VectorMatch> query(const std::vector<float>& embedding,
                                   uint32_t top_k = 5) const
    {
        if ((uint32_t)embedding.size() != dim_) {
            throw std::invalid_argument("[VectorBuffer] Query dimension mismatch");
        }
        if (vectors_.empty()) return {};

        std::vector<float> q = embedding;
        l2_normalize(q.data(), dim_);

        if (hnsw_index_) {
            auto hnsw_results = hnsw_index_->search(q, top_k, use_sq8_);
            std::vector<VectorMatch> matches;
            matches.reserve(hnsw_results.size());
            for (const auto& res : hnsw_results) {
                float sim = 1.0f - res.distance;
                matches.push_back({res.id, sim, texts_[res.id]});
            }
            return matches;
        }

        uint32_t n = (uint32_t)vectors_.size();
        std::vector<VectorMatch> matches;
        matches.reserve(n);

        for (uint32_t i = 0; i < n; ++i) {
            float score = 0.0f;
            if (use_sq8_ && i < sq8_vectors_.size()) {
                score = SQ8Quantizer::dot_product(q.data(), sq8_vectors_[i], dim_);
            } else {
                score = dot_simd(q.data(), vectors_[i].data(), dim_);
            }
            matches.push_back({i, score, texts_[i]});
        }

        uint32_t k = std::min(top_k, n);
        std::partial_sort(
            matches.begin(),
            matches.begin() + k,
            matches.end(),
            [](const VectorMatch& a, const VectorMatch& b) {
                return a.score > b.score;
            }
        );
        matches.resize(k);
        return matches;
    }

    uint32_t size()      const { return (uint32_t)vectors_.size(); }
    uint32_t dim()       const { return dim_; }
    bool     empty()     const { return vectors_.empty(); }
    void     clear()     {
        vectors_.clear();
        sq8_vectors_.clear();
        texts_.clear();
        hnsw_index_.reset();
    }

    std::string_view text(uint32_t idx) const {
        if (idx >= texts_.size())
            throw std::out_of_range("[VectorBuffer] index out of range");
        return texts_[idx];
    }

    std::string ragContext(const std::vector<float>& query_vec,
                           uint32_t top_k = 3) const
    {
        auto results = query(query_vec, top_k);
        if (results.empty()) return {};
        std::string out;
        out.reserve(512 * results.size());
        out += "[Retrieved Context — Vector RAG]\n";
        for (size_t i = 0; i < results.size(); ++i) {
            out += "  [" + std::to_string(i + 1) + "] ";
            out += "(score=" + std::to_string(results[i].score).substr(0, 5) + ") ";
            out += results[i].text + "\n";
        }
        return out;
    }

    size_t bytesUsed() const {
        size_t bytes = vectors_.size() * dim_ * sizeof(float)
                     + texts_.size() * sizeof(std::string);
        if (use_sq8_) {
            bytes += sq8_vectors_.size() * (dim_ * sizeof(uint8_t) + 2 * sizeof(float));
        }
        if (hnsw_index_) {
            bytes += hnsw_index_->size() * sizeof(HNSWNode);
        }
        return bytes;
    }

    void printStats() const {
        printf("[VectorBuffer] vectors=%u dim=%u bytes_approx=%zu SQ8=%s HNSW=%s SIMD=%s\n",
               size(), dim_, bytesUsed(),
               use_sq8_ ? "enabled" : "disabled",
               hnsw_index_ ? "enabled" : "disabled",
#if defined(CA_SIMD_AVX2)
               "AVX2"
#elif defined(CA_SIMD_NEON)
               "NEON"
#else
               "scalar"
#endif
        );
    }

private:
    uint32_t dim_;
    uint32_t capacity_;
    bool     use_sq8_;

    std::vector<std::vector<float>> vectors_;
    std::vector<SQ8Vector>         sq8_vectors_;
    std::vector<std::string>        texts_;
    std::unique_ptr<HNSWIndex>      hnsw_index_;

    static float dot_simd(const float* __restrict__ a,
                          const float* __restrict__ b,
                          uint32_t n)
    {
#if defined(CA_SIMD_AVX2)
        __m256 acc = _mm256_setzero_ps();
        uint32_t i = 0;
        for (; i + 8 <= n; i += 8) {
            __m256 va = _mm256_loadu_ps(a + i);
            __m256 vb = _mm256_loadu_ps(b + i);
            acc = _mm256_fmadd_ps(va, vb, acc);
        }
        __m128 lo  = _mm256_castps256_ps128(acc);
        __m128 hi  = _mm256_extractf128_ps(acc, 1);
        __m128 sum = _mm_add_ps(lo, hi);
        sum = _mm_hadd_ps(sum, sum);
        sum = _mm_hadd_ps(sum, sum);
        float result = _mm_cvtss_f32(sum);
        for (; i < n; ++i) result += a[i] * b[i];
        return result;

#elif defined(CA_SIMD_NEON)
        float32x4_t acc = vdupq_n_f32(0.0f);
        uint32_t i = 0;
        for (; i + 4 <= n; i += 4) {
            float32x4_t va = vld1q_f32(a + i);
            float32x4_t vb = vld1q_f32(b + i);
            acc = vmlaq_f32(acc, va, vb);
        }
        float result = vaddvq_f32(acc);
        for (; i < n; ++i) result += a[i] * b[i];
        return result;

#else
        float result = 0.0f;
        for (uint32_t i = 0; i < n; ++i) result += a[i] * b[i];
        return result;
#endif
    }

    static void l2_normalize(float* v, uint32_t n) {
        float norm_sq = 0.0f;
        for (uint32_t i = 0; i < n; ++i) norm_sq += v[i] * v[i];
        if (norm_sq < 1e-12f) return;
        float inv_norm = 1.0f / std::sqrt(norm_sq);
        for (uint32_t i = 0; i < n; ++i) v[i] *= inv_norm;
    }
};

} // namespace cerberus
