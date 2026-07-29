#pragma once

/*
 * SwarmOrchestrator — Hierarchical C++ Async Fan-Out / Fan-In
 * ============================================================
 * Enables an orchestrator Agent to decompose a task into independent
 * sub-tasks, dispatch them to worker Agents on the C++ thread pool in
 * parallel (fan-out), then collect and merge their outputs (fan-in).
 *
 * Architecture
 * ────────────
 *
 *                      ┌──────────────────────────┐
 *                      │   SwarmOrchestrator       │
 *                      │  (owns thread pool ref)   │
 *                      └────────┬─────────────────┘
 *                               │  fanOut()
 *                    ┌──────────┼──────────┐
 *                    ▼          ▼          ▼
 *              WorkerSlot  WorkerSlot  WorkerSlot
 *             (future<>)  (future<>)  (future<>)
 *                    │          │          │
 *                    └──────────┼──────────┘
 *                               │  fanIn() / await()
 *                               ▼
 *                        SwarmResult[]
 *
 * Design goals
 * ────────────
 *  • Fan-out dispatches N subtasks onto `ca_thread_pool_t` concurrently.
 *  • Fan-in blocks (with timeout) until all workers complete.
 *  • Each WorkerSlot holds a std::packaged_task + std::future, zero copies.
 *  • SwarmResult carries the worker name, output, success flag, and latency.
 *  • Fully exception-safe: a worker exception is captured and stored in the
 *    result rather than propagating to the orchestrator thread.
 *  • Telemetry: every spawn and join emits a ca_ring_event_t if a ring
 *    buffer is wired in.
 *
 * Usage (C++)
 * ───────────
 *   SwarmOrchestrator swarm(thread_pool_, ring_buf_);
 *
 *   swarm.spawn("summariser", [&](){ return summariser_.run(subtask1); });
 *   swarm.spawn("coder",      [&](){ return coder_.run(subtask2);      });
 *   swarm.spawn("reviewer",   [&](){ return reviewer_.run(subtask3);   });
 *
 *   auto results = swarm.await(5000);   // 5-second timeout
 *   for (auto& r : results)
 *       shared_memory.write(r.worker_name, r.output);
 */

#include <string>
#include <vector>
#include <future>
#include <functional>
#include <stdexcept>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "ringbuf.h"
extern "C" {
    #include "thread_pool.h"
}

namespace coreagent {

/* ── Per-worker result ──────────────────────────────────────────────────── */
struct SwarmResult {
    std::string worker_name;    /* name supplied to spawn()      */
    std::string output;         /* result string from the worker */
    bool        success;        /* false if worker threw          */
    std::string error;          /* exception message on failure   */
    uint64_t    elapsed_ms;     /* wall-clock time for this slot  */
};

/* ── Merged / summarised output ─────────────────────────────────────────── */
struct SwarmSummary {
    std::vector<SwarmResult> results;
    uint32_t                 succeeded;
    uint32_t                 failed;
    uint64_t                 total_elapsed_ms;

    /* Concatenate all successful outputs, separated by newlines */
    std::string combined() const {
        std::string out;
        for (const auto& r : results) {
            if (r.success) {
                out += "[" + r.worker_name + "]: " + r.output + "\n";
            }
        }
        return out;
    }
};

/* ── Internal slot (one per spawned worker) ─────────────────────────────── */
struct SwarmSlot {
    std::string                  worker_name;
    std::future<SwarmResult>     future;
    uint64_t                     spawn_ts_ns;  /* for telemetry */
};

/* ── Thread-pool task wrapper ────────────────────────────────────────────── */
struct SwarmTask {
    std::packaged_task<SwarmResult()>* task_ptr;
};

/* ── SwarmOrchestrator ──────────────────────────────────────────────────── */
class SwarmOrchestrator {
public:
    /*
     * pool     — thread pool to dispatch workers onto (owned externally)
     * ring     — optional telemetry ring buffer; may be nullptr
     * name     — orchestrator name (appears in telemetry + logs)
     */
    explicit SwarmOrchestrator(ca_thread_pool_t* pool,
                               ca_ringbuf_t*     ring    = nullptr,
                               const char*       name    = "Orchestrator")
        : pool_(pool), ring_(ring), name_(name)
    {
        if (!pool_)
            throw std::invalid_argument("[SwarmOrchestrator] pool must not be null");
    }

    ~SwarmOrchestrator() { slots_.clear(); }

    SwarmOrchestrator(const SwarmOrchestrator&)            = delete;
    SwarmOrchestrator& operator=(const SwarmOrchestrator&) = delete;

    /* ── Fan-out: spawn a worker ────────────────────────────────────────── */
    /*
     * spawn — Schedule `worker_fn` as an async task on the thread pool.
     *
     * worker_name   : human-readable label for this worker (logged + returned)
     * worker_fn     : callable () -> std::string (the worker's result)
     *
     * The callable is captured by value so callers can safely pass lambdas
     * with captured references as long as lifetimes are managed correctly.
     *
     * Returns *this for chaining: swarm.spawn(...).spawn(...)
     */
    template <typename Fn>
    SwarmOrchestrator& spawn(const std::string& worker_name, Fn&& worker_fn) {
        uint64_t ts = ca_now_ns();

        /* Wrap the worker function so exceptions are caught and stored */
        auto safe_fn = [worker_name, fn = std::forward<Fn>(worker_fn), ts]() -> SwarmResult {
            SwarmResult res;
            res.worker_name = worker_name;
            auto start = std::chrono::steady_clock::now();
            try {
                res.output  = fn();
                res.success = true;
            } catch (const std::exception& e) {
                res.output  = "";
                res.success = false;
                res.error   = e.what();
            } catch (...) {
                res.output  = "";
                res.success = false;
                res.error   = "[SwarmOrchestrator] unknown exception in worker";
            }
            auto end = std::chrono::steady_clock::now();
            res.elapsed_ms = (uint64_t)
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            return res;
        };

        /* Package into a std::packaged_task allocated on the heap so the
         * lambda survives until the thread pool executes it. */
        auto* pt = new std::packaged_task<SwarmResult()>(std::move(safe_fn));
        std::future<SwarmResult> fut = pt->get_future();

        /* Submit to thread pool via C trampoline */
        int rc = ca_pool_submit(pool_, &SwarmOrchestrator::trampoline,
                                static_cast<void*>(pt));
        if (rc != 0) {
            /* Pool queue full: run inline as fallback */
            fprintf(stderr, "[SwarmOrchestrator] pool queue full — running '%s' inline\n",
                    worker_name.c_str());
            (*pt)();
        }

        slots_.push_back({worker_name, std::move(fut), ts});

        if (ring_) {
            ca_ring_emit(ring_, CA_EVT_SWARM_SPAWN,
                         name_.c_str(), worker_name.c_str());
        }

        printf("[%s ⚡] Spawned worker: %s\n", name_.c_str(), worker_name.c_str());
        return *this;
    }

    /* ── Fan-in: collect results ─────────────────────────────────────────── */
    /*
     * await — Block until all spawned workers finish (or timeout_ms elapses).
     *
     * timeout_ms   : per-worker wait timeout in milliseconds.
     *                If a worker doesn't complete in time its result is marked
     *                as failed with error "timeout".
     *
     * After await() returns the orchestrator is reset and can be reused.
     */
    SwarmSummary await(uint32_t timeout_ms = 30000) {
        SwarmSummary summary;
        summary.succeeded        = 0;
        summary.failed           = 0;
        summary.total_elapsed_ms = 0;

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeout_ms);

        for (auto& slot : slots_) {
            SwarmResult res;
            auto wait_until = deadline;
            auto status = slot.future.wait_until(wait_until);

            if (status == std::future_status::ready) {
                res = slot.future.get();
            } else {
                /* Timeout */
                res.worker_name = slot.worker_name;
                res.output      = "";
                res.success     = false;
                res.error       = "timeout after " + std::to_string(timeout_ms) + "ms";
                res.elapsed_ms  = timeout_ms;
            }

            if (res.success) {
                summary.succeeded++;
                printf("[%s ✅] Worker '%s' done in %llums\n",
                       name_.c_str(), res.worker_name.c_str(),
                       (unsigned long long)res.elapsed_ms);
            } else {
                summary.failed++;
                fprintf(stderr, "[%s ❌] Worker '%s' failed: %s\n",
                        name_.c_str(), res.worker_name.c_str(), res.error.c_str());
            }

            summary.total_elapsed_ms += res.elapsed_ms;
            summary.results.push_back(std::move(res));

            if (ring_) {
                ca_ring_emit(ring_, CA_EVT_SWARM_JOIN,
                             name_.c_str(), slot.worker_name.c_str());
            }
        }

        slots_.clear();

        printf("[%s] 🏁 Swarm complete: %u succeeded, %u failed, total=%llums\n",
               name_.c_str(), summary.succeeded, summary.failed,
               (unsigned long long)summary.total_elapsed_ms);

        return summary;
    }

    /* ── Helpers ─────────────────────────────────────────────────────────── */
    size_t pendingCount() const { return slots_.size(); }
    void   reset()             { slots_.clear(); }

private:
    ca_thread_pool_t* pool_;
    ca_ringbuf_t*     ring_;
    std::string       name_;
    std::vector<SwarmSlot> slots_;

    /* C-compatible trampoline for ca_pool_submit */
    static void trampoline(void* arg) {
        auto* pt = static_cast<std::packaged_task<SwarmResult()>*>(arg);
        (*pt)();
        delete pt;
    }
};

} // namespace coreagent
