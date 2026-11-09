#pragma once

/*
 * Telemetry — C++ RAII wrapper around ca_ringbuf_t
 * =================================================
 * Provides a clean C++ interface to the lockless ring buffer for use
 * inside Agent, SwarmOrchestrator, and VectorBuffer.
 *
 * The wrapper automatically initialises and destroys the underlying
 * C ring buffer, and exposes typed emit() overloads so callers never
 * have to touch the C API directly.
 *
 * Thread safety: emit() is lock-free and safe to call from any thread.
 * drain() / exportOtel() are NOT thread-safe and must be called from
 * a single consumer thread.
 */

#include <string>
#include <vector>
#include <cstdio>
#include <stdexcept>

#include "ringbuf.h"

namespace geryon {

/* ── RAII guard for a timed span ─────────────────────────────────────────── */
class TelemetrySpan {
public:
    TelemetrySpan(ca_ringbuf_t*   ring,
                  ca_event_kind_t kind,
                  const char*     agent_id,
                  const char*     label)
        : ring_(ring), kind_(kind)
    {
        if (agent_id) strncpy(agent_id_, agent_id, 15);
        if (label)    strncpy(label_,    label,    47);
        begin_ns_ = ca_ring_begin();
    }

    ~TelemetrySpan() {
        ca_ring_end(ring_, kind_, agent_id_, label_, begin_ns_);
    }

    TelemetrySpan(const TelemetrySpan&)            = delete;
    TelemetrySpan& operator=(const TelemetrySpan&) = delete;

private:
    ca_ringbuf_t*   ring_;
    ca_event_kind_t kind_;
    char            agent_id_[16] = {};
    char            label_[48]    = {};
    uint64_t        begin_ns_     = 0;
};

/* ── Telemetry ────────────────────────────────────────────────────────────── */
class Telemetry {
public:
    explicit Telemetry(uint32_t capacity = CA_RING_DEFAULT_CAP) {
        ring_ = ca_ring_create(capacity);
        if (!ring_)
            throw std::bad_alloc();
    }

    ~Telemetry() {
        ca_ring_destroy(ring_);
    }

    Telemetry(const Telemetry&)            = delete;
    Telemetry& operator=(const Telemetry&) = delete;

    /* ── Point-in-time events ─────────────────────────────────────────── */
    bool emit(ca_event_kind_t kind,
              const std::string& agent_id,
              const std::string& label = "")
    {
        return ca_ring_emit(ring_, kind, agent_id.c_str(), label.c_str());
    }

    /* ── Timed span (RAII) ────────────────────────────────────────────── */
    TelemetrySpan span(ca_event_kind_t  kind,
                       const std::string& agent_id,
                       const std::string& label = "")
    {
        return TelemetrySpan(ring_, kind, agent_id.c_str(), label.c_str());
    }

    /* ── Consumer ─────────────────────────────────────────────────────── */
    uint64_t available() const { return ca_ring_available(ring_); }
    bool     dropped()   const { return ca_ring_dropped(ring_); }

    std::vector<ca_ring_event_t> drain(uint32_t max = 4096) {
        std::vector<ca_ring_event_t> buf(max);
        uint32_t n = ca_ring_drain(ring_, buf.data(), max);
        buf.resize(n);
        return buf;
    }

    void print(uint32_t max_events = 256) {
        ca_ring_print(ring_, max_events);
    }

    /* Export as OTLP-compatible NDJSON to stdout or a file */
    void exportOtel(FILE* fp = stdout) {
        ca_ring_export_otel(ring_, static_cast<void*>(fp));
    }

    void exportOtelToFile(const std::string& path) {
        FILE* fp = fopen(path.c_str(), "a");
        if (!fp)
            throw std::runtime_error("[Telemetry] Cannot open file: " + path);
        ca_ring_export_otel(ring_, static_cast<void*>(fp));
        fclose(fp);
    }

    void reset() { ca_ring_reset(ring_); }

    /* Raw pointer for passing to SwarmOrchestrator / Agent */
    ca_ringbuf_t* raw() { return ring_; }

private:
    ca_ringbuf_t* ring_;
};

} // namespace geryon
