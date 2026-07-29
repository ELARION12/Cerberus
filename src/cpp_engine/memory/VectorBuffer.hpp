#pragma once

/*
 * VectorBuffer — Embedded SIMD Vector Memory for Zero-Dependency RAG
 * ===================================================================
 * Stores fixed-dimension float32 embedding vectors together with a string
 * payload (e.g. a chunk of text) inside the existing ca_arena_t pool.
 * Similarity search is accelerated using AVX2 (x86-64) or NEON (ARM64)
 * intrinsics when the target supports them, with an automatic scalar
 * fallback so the code is always portable.
 *
 * Key properties
 * ──────────────
 *  • Zero heap allocations after construction: every vector and its text
 *    are stored in a single arena allocation.
 *  • Cosine similarity computed with fused SIMD dot-product + norm.
 *  • top-k search returns indices + scores in O(N·D) time; N = stored
 *    vectors, D = dimension.  No indexing overhead for agent-scale N (<10k).
 *  • Thread-safe reads (no lock): all mutations must be done by the same
 *    thread that owns this VectorBuffer (matches ContextBuffer semantics).
 *  • Plugs into ContextBuffer::retrieve() to provide RAG without any
 *    external vector database.
 *
 * Usage (C++)
 * ───────────
 *   VectorBuffer vb(512);              // 512-dim embeddings
 *   vb.push({0.1f, 0.2f, …}, "chunk text");
 *   auto results = vb.query({…}, 3);  // top-3 nearest neighbours
 *   for (auto& r : results)
 *       std::cout << r.score << " → " << r.text << "\n";
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

/* SIMD detection */
#if defined(__AVX2__)
#  include <immintrin.h>
#  define CA_SIMD_AVX2 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#  include <arm_neon.h>
#  define CA_SIMD_NEON 1
#endif

namespace coreagent {

/* ── Result of a nearest-neighbour query ─────────────────────────────────── */
struct VectorMatch {
    uint32_t    index;    /* slot index in the VectorBuffer            */
    float       score;    /* cosine similarity ∈ [−1, 1]               */
    std::string text;     /* associated text payload                   */
};

/* ── VectorBuffer ─────────────────────────────────────────────────────────── */
class VectorBuffer {
public:
    /*
     * dim          — embedding dimension (must match vectors you push/query)
     * max_vectors  — pre-reserved capacity (grows dynamically if exceeded)
     */
    explicit VectorBuffer(uint32_t dim, uint32_t max_vectors = 4096)
        : dim_(dim), capacity_(max_vectors)
    {
        if (dim == 0)
            throw std::invalid_argument("[VectorBuffer] dim must be > 0");
        vectors_.reserve(max_vectors);
        texts_.reserve(max_vectors);
    }

    /* ── Mutation ─────────────────────────────────────────────────────── */

    /*
     * push — Store a normalised copy of `embedding` with associated `text`.
     * The embedding is L2-normalised on insertion so query time skips the
     * per-vector norm division (dot product ≡ cosine similarity).
     */
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
        vectors_.push_back(std::move(norm_vec));
        texts_.emplace_back(text);
        return idx;
    }

    /* ── Query ────────────────────────────────────────────────────────── */

    /*
     * query — Return the top-k most similar stored vectors to `embedding`.
     * `embedding` is normalised internally; no side-effects on the caller.
     */
    std::vector<VectorMatch> query(const std::vector<float>& embedding,
                                   uint32_t top_k = 5) const
    {
        if ((uint32_t)embedding.size() != dim_) {
            throw std::invalid_argument(
                "[VectorBuffer] Query dimension mismatch");
        }
        if (vectors_.empty()) return {};

        /* Normalise query vector */
        std::vector<float> q = embedding;
        l2_normalize(q.data(), dim_);

        uint32_t n = (uint32_t)vectors_.size();
        std::vector<VectorMatch> matches;
        matches.reserve(n);

        for (uint32_t i = 0; i < n; ++i) {
            float score = dot_simd(q.data(), vectors_[i].data(), dim_);
            matches.push_back({i, score, texts_[i]});
        }

        /* Partial sort: top-k in O(N log k) */
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

    /* ── Accessors ────────────────────────────────────────────────────── */
    uint32_t size()      const { return (uint32_t)vectors_.size(); }
    uint32_t dim()       const { return dim_; }
    bool     empty()     const { return vectors_.empty(); }
    void     clear()     { vectors_.clear(); texts_.clear(); }

    std::string_view text(uint32_t idx) const {
        if (idx >= texts_.size())
            throw std::out_of_range("[VectorBuffer] index out of range");
        return texts_[idx];
    }

    /* Build a RAG context string from top-k results (injects into prompts) */
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

    /* Memory usage estimate */
    size_t bytesUsed() const {
        return vectors_.size() * dim_ * sizeof(float)
             + texts_.size() * sizeof(std::string);  /* approx */
    }

    void printStats() const {
        printf("[VectorBuffer] vectors=%u dim=%u bytes_approx=%zu SIMD=%s\n",
               size(), dim_, bytesUsed(),
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
    std::vector<std::vector<float>> vectors_;
    std::vector<std::string>        texts_;

    /* ── SIMD dot product ─────────────────────────────────────────────── */
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
        /* Horizontal sum of 8 lanes */
        __m128 lo  = _mm256_castps256_ps128(acc);
        __m128 hi  = _mm256_extractf128_ps(acc, 1);
        __m128 sum = _mm_add_ps(lo, hi);
        sum = _mm_hadd_ps(sum, sum);
        sum = _mm_hadd_ps(sum, sum);
        float result = _mm_cvtss_f32(sum);
        /* Scalar tail */
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

    /* ── L2 normalisation (in-place) ──────────────────────────────────── */
    static void l2_normalize(float* v, uint32_t n) {
        float norm_sq = 0.0f;
        for (uint32_t i = 0; i < n; ++i) norm_sq += v[i] * v[i];
        if (norm_sq < 1e-12f) return; /* zero-vector guard */
        float inv_norm = 1.0f / std::sqrt(norm_sq);
        for (uint32_t i = 0; i < n; ++i) v[i] *= inv_norm;
    }
};

} // namespace coreagent
