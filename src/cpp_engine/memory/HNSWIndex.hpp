#pragma once

/*
 * HNSWIndex — Native C++ Hierarchical Navigable Small World Graph Index
 * ===================================================================
 * Logarithmic-time O(log N) approximate nearest neighbour search over vector
 * embeddings for agent-scale and production RAG contexts.
 *
 * Parameters:
 *  • M: Max number of bidirectional links per node in graph layers (default: 16)
 *  • ef_construction: Beam search depth during graph build (default: 64)
 *  • ef_search: Beam search depth during query (default: 32)
 */

#include <vector>
#include <queue>
#include <unordered_set>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include "SQ8Quantizer.hpp"

namespace geryon {

struct HNSWNode {
    uint32_t id;
    std::vector<float> vector;
    SQ8Vector sq8_vector;
    std::vector<std::vector<uint32_t>> neighbors; // neighbors per layer
};

struct HNSWSearchResult {
    uint32_t id;
    float distance; // Cosine distance = 1.0 - cosine_similarity
};

class HNSWIndex {
public:
    HNSWIndex(uint32_t dim, uint32_t M = 16, uint32_t ef_construction = 64, uint32_t ef_search = 32)
        : dim_(dim), M_(M), M0_(2 * M), ef_construction_(ef_construction), ef_search_(ef_search),
          mult_(1.0 / std::log(1.0 * M)), enter_node_id_(-1), max_level_(-1), rng_(42) {}

    uint32_t dim() const { return dim_; }
    size_t size()  const { return nodes_.size(); }

    /* Insert vector into HNSW index */
    void insert(uint32_t id, const std::vector<float>& vec) {
        if (vec.size() != dim_) {
            throw std::invalid_argument("[HNSWIndex] Vector dimension mismatch");
        }

        std::lock_guard<std::mutex> lock(mutex_);

        int level = getRandomLevel();
        auto node = std::make_shared<HNSWNode>();
        node->id = id;
        node->vector = vec;
        node->sq8_vector = SQ8Quantizer::quantize(vec.data(), dim_);
        node->neighbors.resize(level + 1);

        if (nodes_.size() <= id) {
            nodes_.resize(id + 1);
        }
        nodes_[id] = node;

        if (enter_node_id_ == -1) {
            enter_node_id_ = id;
            max_level_ = level;
            return;
        }

        int curr_obj = enter_node_id_;
        float curr_dist = computeDistance(vec, nodes_[curr_obj]->vector);

        for (int l = max_level_; l > level; --l) {
            bool changed = true;
            while (changed) {
                changed = false;
                for (uint32_t neighbor : nodes_[curr_obj]->neighbors[l]) {
                    float d = computeDistance(vec, nodes_[neighbor]->vector);
                    if (d < curr_dist) {
                        curr_dist = d;
                        curr_obj = neighbor;
                        changed = true;
                    }
                }
            }
        }

        for (int l = std::min(level, max_level_); l >= 0; --l) {
            auto top_candidates = searchLayer(vec, curr_obj, ef_construction_, l);
            uint32_t max_m = (l == 0) ? M0_ : M_;

            /* Select neighbors */
            std::vector<uint32_t> neighbors;
            for (const auto& res : top_candidates) {
                neighbors.push_back(res.id);
                if (neighbors.size() >= max_m) break;
            }

            node->neighbors[l] = neighbors;

            /* Add bidirectional links */
            for (uint32_t neighbor : neighbors) {
                auto& n_list = nodes_[neighbor]->neighbors[l];
                n_list.push_back(id);
                if (n_list.size() > max_m) {
                    /* Prune extra connections */
                    std::sort(n_list.begin(), n_list.end(), [&](uint32_t a, uint32_t b) {
                        return computeDistance(nodes_[neighbor]->vector, nodes_[a]->vector) <
                               computeDistance(nodes_[neighbor]->vector, nodes_[b]->vector);
                    });
                    n_list.resize(max_m);
                }
            }

            if (!top_candidates.empty()) {
                curr_obj = top_candidates[0].id;
            }
        }

        if (level > max_level_) {
            max_level_ = level;
            enter_node_id_ = id;
        }
    }

    /* Query top-k nearest neighbors using HNSW graph traversal */
    std::vector<HNSWSearchResult> search(const std::vector<float>& query, uint32_t top_k, bool use_sq8 = false) const {
        if (query.size() != dim_) {
            throw std::invalid_argument("[HNSWIndex] Query dimension mismatch");
        }
        if (enter_node_id_ == -1 || nodes_.empty()) return {};

        std::lock_guard<std::mutex> lock(mutex_);

        int curr_obj = enter_node_id_;
        float curr_dist = computeDistance(query, nodes_[curr_obj]->vector);

        for (int l = max_level_; l > 0; --l) {
            bool changed = true;
            while (changed) {
                changed = false;
                for (uint32_t neighbor : nodes_[curr_obj]->neighbors[l]) {
                    float d = computeDistance(query, nodes_[neighbor]->vector);
                    if (d < curr_dist) {
                        curr_dist = d;
                        curr_obj = neighbor;
                        changed = true;
                    }
                }
            }
        }

        auto top_candidates = searchLayer(query, curr_obj, std::max(ef_search_, top_k), 0, use_sq8);
        if (top_candidates.size() > top_k) {
            top_candidates.resize(top_k);
        }
        return top_candidates;
    }

private:
    uint32_t dim_;
    uint32_t M_;
    uint32_t M0_;
    uint32_t ef_construction_;
    uint32_t ef_search_;
    double   mult_;
    int      enter_node_id_;
    int      max_level_;
    std::mt19937 rng_;
    mutable std::mutex mutex_;

    std::vector<std::shared_ptr<HNSWNode>> nodes_;

    int getRandomLevel() {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        double r = dist(rng_);
        if (r == 0.0) r = 0.0000001;
        return static_cast<int>(-std::log(r) * mult_);
    }

    static float computeDistance(const std::vector<float>& a, const std::vector<float>& b) {
        float dot = 0.0f;
        for (size_t i = 0; i < a.size(); ++i) {
            dot += a[i] * b[i];
        }
        /* Assuming normalized vectors, Cosine distance = 1.0 - cosine_similarity */
        return 1.0f - dot;
    }

    std::vector<HNSWSearchResult> searchLayer(const std::vector<float>& query, uint32_t entry_id,
                                               uint32_t ef, int level, bool use_sq8 = false) const {
        std::vector<HNSWSearchResult> results;
        std::unordered_set<uint32_t> visited;

        auto comp_far = [](const HNSWSearchResult& a, const HNSWSearchResult& b) { return a.distance < b.distance; };
        auto comp_near = [](const HNSWSearchResult& a, const HNSWSearchResult& b) { return a.distance > b.distance; };

        std::priority_queue<HNSWSearchResult, std::vector<HNSWSearchResult>, decltype(comp_far)> W(comp_far);
        std::priority_queue<HNSWSearchResult, std::vector<HNSWSearchResult>, decltype(comp_near)> C(comp_near);

        float dist_entry = use_sq8 ? (1.0f - SQ8Quantizer::dot_product(query.data(), nodes_[entry_id]->sq8_vector, dim_))
                                   : computeDistance(query, nodes_[entry_id]->vector);

        HNSWSearchResult entry_res{entry_id, dist_entry};
        vist_node:
        visited.insert(entry_id);
        C.push(entry_res);
        W.push(entry_res);

        while (!C.empty()) {
            auto curr = C.top();
            C.pop();

            auto furthest = W.top();
            if (curr.distance > furthest.distance) break;

            for (uint32_t neighbor : nodes_[curr.id]->neighbors[level]) {
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);

                    furthest = W.top();
                    float d = use_sq8 ? (1.0f - SQ8Quantizer::dot_product(query.data(), nodes_[neighbor]->sq8_vector, dim_))
                                      : computeDistance(query, nodes_[neighbor]->vector);

                    if (d < furthest.distance || W.size() < ef) {
                        HNSWSearchResult n_res{neighbor, d};
                        C.push(n_res);
                        W.push(n_res);
                        if (W.size() > ef) {
                            W.pop();
                        }
                    }
                }
            }
        }

        while (!W.empty()) {
            results.push_back(W.top());
            W.pop();
        }
        std::reverse(results.begin(), results.end());
        return results;
    }
};

} // namespace geryon
