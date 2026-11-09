#ifndef CA_RINGBUF_H
#define CA_RINGBUF_H

/*
 * ca_ringbuf_t — Lock-Free Single-Producer Single-Consumer Ring Buffer
 * =====================================================================
 * Provides microsecond-precision telemetry event tracing for the Cerberus
 * runtime. Uses C11 stdatomic to ensure memory-order correctness without
 * any mutex, making it safe to call from hot loops and signal handlers.
 *
 * Wire format of each slot (ca_ring_event_t):
 *   - kind    : event category (THINK, PLAN, ACT, REFLECT, TOOL_CALL, etc.)
 *   - elapsed_ns : nanosecond duration of the event (0 if point-in-time)
 *   - agent_id   : compact 16-char agent name
 *   - label      : compact 48-char event label (tool name, error snippet, …)
 *   - ts_ns      : absolute monotonic timestamp (CLOCK_MONOTONIC) in ns
 *
 * Design constraints:
 *   - Buffer size MUST be a power of two so masking replaces modulo.
 *   - Producer and consumer pointers are kept on separate cache lines
 *     (64-byte aligned) to avoid false sharing.
 *   - All public API is reentrant-safe from the producer side.
 */

#ifdef __cplusplus
#include <atomic>
#include <cstdint>
#include <cstddef>
#define CA_ATOMIC_UINT64 std::atomic<uint64_t>
#define CA_ATOMIC_BOOL   std::atomic<bool>
#define CA_ALIGNAS(x)    alignas(x)
#else
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdbool.h>
#define CA_ATOMIC_UINT64 atomic_uint_least64_t
#define CA_ATOMIC_BOOL   atomic_bool
#define CA_ALIGNAS(x)    _Alignas(x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Event kind tags ────────────────────────────────────────────────────── */
typedef enum {
    CA_EVT_NONE        = 0,
    CA_EVT_THINK       = 1,
    CA_EVT_PLAN        = 2,
    CA_EVT_ACT         = 3,
    CA_EVT_REFLECT     = 4,
    CA_EVT_TOOL_CALL   = 5,
    CA_EVT_TOOL_OK     = 6,
    CA_EVT_TOOL_ERR    = 7,
    CA_EVT_SWARM_SPAWN = 8,
    CA_EVT_SWARM_JOIN  = 9,
    CA_EVT_VECTOR_PUSH = 10,
    CA_EVT_VECTOR_QUERY= 11,
    CA_EVT_CONTEXT_GC  = 12,
    CA_EVT_AGENT_DONE  = 13,
    CA_EVT_CUSTOM      = 255,
} ca_event_kind_t;

/* ── Single telemetry record (64 bytes, one cache-line) ─────────────────── */
typedef struct {
    uint64_t         ts_ns;          /* absolute monotonic start timestamp  */
    uint64_t         elapsed_ns;     /* duration (0 = instantaneous event)  */
    ca_event_kind_t  kind;           /* event category                      */
    uint8_t          _pad[4];
    char             agent_id[16];   /* null-terminated agent name          */
    char             label[24];      /* null-terminated detail label        */
} ca_ring_event_t;

/* static_assert kept C11-compatible */
typedef char _ca_ring_event_size_check[
    (sizeof(ca_ring_event_t) == 64) ? 1 : -1
];

/* ── Ring-buffer control block ──────────────────────────────────────────── */
#define CA_RING_MAX_CAPACITY  (1u << 20)  /* 1 M slots max (~64 MB)         */
#define CA_RING_DEFAULT_CAP   (1u << 14)  /* 16 k slots default (~1 MB)     */

typedef struct {
    /* Producer state — occupies its own cache line */
    CA_ALIGNAS(64) CA_ATOMIC_UINT64 head;      /* next write slot index */

    /* Consumer state — occupies its own cache line */
    CA_ALIGNAS(64) CA_ATOMIC_UINT64 tail;      /* next read  slot index */

    uint32_t         capacity;        /* number of slots (power-of-two)     */
    uint32_t         mask;            /* capacity - 1, for fast modulo       */
    ca_ring_event_t* slots;           /* heap-allocated slot array           */
    CA_ATOMIC_BOOL   dropped;         /* at least one event was dropped      */
} ca_ringbuf_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */
ca_ringbuf_t* ca_ring_create (uint32_t capacity);
void          ca_ring_destroy(ca_ringbuf_t* rb);
void          ca_ring_reset  (ca_ringbuf_t* rb);

/* ── Producer API ───────────────────────────────────────────────────────── */
/*
 * ca_ring_push — Append one event to the ring.
 * Returns true on success, false if the buffer is full (event dropped).
 * Never blocks. Safe to call from any thread/signal context.
 */
bool ca_ring_push(ca_ringbuf_t* rb, const ca_ring_event_t* evt);

/*
 * Convenience: record a point-in-time event with just a kind + label.
 * The timestamp is captured inside the call with clock_gettime.
 */
bool ca_ring_emit(ca_ringbuf_t* rb,
                  ca_event_kind_t kind,
                  const char* agent_id,
                  const char* label);

/*
 * Convenience: record a timed event.  Call ca_ring_begin() before the
 * operation and ca_ring_end() after; elapsed_ns is computed automatically.
 */
uint64_t ca_ring_begin(void);   /* returns ts_ns captured now              */
bool     ca_ring_end  (ca_ringbuf_t* rb,
                       ca_event_kind_t kind,
                       const char* agent_id,
                       const char* label,
                       uint64_t    begin_ts_ns);

/* ── Consumer API ───────────────────────────────────────────────────────── */
/*
 * ca_ring_pop — Pop one event from the ring into *out.
 * Returns false if empty.  Never blocks.
 */
bool ca_ring_pop (ca_ringbuf_t* rb, ca_ring_event_t* out);

/*
 * ca_ring_drain — Pop up to `max` events into caller-supplied array.
 * Returns number of events actually copied.
 */
uint32_t ca_ring_drain(ca_ringbuf_t* rb,
                       ca_ring_event_t* buf,
                       uint32_t         max);

/* ── Stats / Export ─────────────────────────────────────────────────────── */
uint64_t ca_ring_available(const ca_ringbuf_t* rb);   /* events ready to pop */
bool     ca_ring_dropped  (const ca_ringbuf_t* rb);   /* any events lost?    */
void     ca_ring_print    (ca_ringbuf_t* rb, uint32_t max_events);

/*
 * ca_ring_export_otel — Serialize drained events as OTLP-compatible
 * JSON lines to `fp`.  Each line is one LogRecord following the
 * OpenTelemetry Logs data model (simplified, no gRPC dependency).
 */
void ca_ring_export_otel(ca_ringbuf_t* rb, void* fp /* FILE* */);

/* ── Helper: monotonic nanosecond clock ─────────────────────────────────── */
uint64_t ca_now_ns(void);

/* ── Event kind name (for printing) ─────────────────────────────────────── */
const char* ca_event_kind_name(ca_event_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* CA_RINGBUF_H */
