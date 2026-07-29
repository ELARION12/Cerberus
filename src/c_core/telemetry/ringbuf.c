/*
 * ca_ringbuf_t — Lockless SPSC Ring Buffer Implementation
 * ========================================================
 * See ringbuf.h for the full API contract and design notes.
 */

#include "ringbuf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ── Monotonic clock ────────────────────────────────────────────────────── */
uint64_t ca_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── Event kind name ────────────────────────────────────────────────────── */
const char* ca_event_kind_name(ca_event_kind_t kind) {
    switch (kind) {
        case CA_EVT_NONE:         return "NONE";
        case CA_EVT_THINK:        return "THINK";
        case CA_EVT_PLAN:         return "PLAN";
        case CA_EVT_ACT:          return "ACT";
        case CA_EVT_REFLECT:      return "REFLECT";
        case CA_EVT_TOOL_CALL:    return "TOOL_CALL";
        case CA_EVT_TOOL_OK:      return "TOOL_OK";
        case CA_EVT_TOOL_ERR:     return "TOOL_ERR";
        case CA_EVT_SWARM_SPAWN:  return "SWARM_SPAWN";
        case CA_EVT_SWARM_JOIN:   return "SWARM_JOIN";
        case CA_EVT_VECTOR_PUSH:  return "VECTOR_PUSH";
        case CA_EVT_VECTOR_QUERY: return "VECTOR_QUERY";
        case CA_EVT_CONTEXT_GC:   return "CONTEXT_GC";
        case CA_EVT_AGENT_DONE:   return "AGENT_DONE";
        case CA_EVT_CUSTOM:       return "CUSTOM";
        default:                  return "UNKNOWN";
    }
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */
ca_ringbuf_t* ca_ring_create(uint32_t capacity) {
    /* Round up to next power of two */
    if (capacity == 0) capacity = CA_RING_DEFAULT_CAP;
    if (capacity > CA_RING_MAX_CAPACITY) capacity = CA_RING_MAX_CAPACITY;

    uint32_t pow2 = 1;
    while (pow2 < capacity) pow2 <<= 1;

    ca_ringbuf_t* rb = (ca_ringbuf_t*)aligned_alloc(64, sizeof(ca_ringbuf_t));
    if (!rb) return NULL;
    memset(rb, 0, sizeof(ca_ringbuf_t));

    rb->slots = (ca_ring_event_t*)calloc(pow2, sizeof(ca_ring_event_t));
    if (!rb->slots) { free(rb); return NULL; }

    rb->capacity = pow2;
    rb->mask     = pow2 - 1;
    atomic_store_explicit(&rb->head,    0, memory_order_relaxed);
    atomic_store_explicit(&rb->tail,    0, memory_order_relaxed);
    atomic_store_explicit(&rb->dropped, false, memory_order_relaxed);
    return rb;
}

void ca_ring_destroy(ca_ringbuf_t* rb) {
    if (!rb) return;
    free(rb->slots);
    free(rb);
}

void ca_ring_reset(ca_ringbuf_t* rb) {
    if (!rb) return;
    atomic_store_explicit(&rb->head, 0, memory_order_seq_cst);
    atomic_store_explicit(&rb->tail, 0, memory_order_seq_cst);
    atomic_store_explicit(&rb->dropped, false, memory_order_relaxed);
    memset(rb->slots, 0, rb->capacity * sizeof(ca_ring_event_t));
}

/* ── Producer ───────────────────────────────────────────────────────────── */
bool ca_ring_push(ca_ringbuf_t* rb, const ca_ring_event_t* evt) {
    if (!rb || !evt) return false;

    uint64_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    /* Buffer full: head has lapped tail by capacity */
    if ((head - tail) >= (uint64_t)rb->capacity) {
        atomic_store_explicit(&rb->dropped, true, memory_order_relaxed);
        return false;
    }

    rb->slots[head & rb->mask] = *evt;

    /* Release: make the write visible before advancing head */
    atomic_store_explicit(&rb->head, head + 1, memory_order_release);
    return true;
}

bool ca_ring_emit(ca_ringbuf_t* rb,
                  ca_event_kind_t kind,
                  const char* agent_id,
                  const char* label)
{
    ca_ring_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.ts_ns      = ca_now_ns();
    evt.elapsed_ns = 0;
    evt.kind       = kind;
    if (agent_id) strncpy(evt.agent_id, agent_id, sizeof(evt.agent_id) - 1);
    if (label)    strncpy(evt.label,    label,    sizeof(evt.label) - 1);
    return ca_ring_push(rb, &evt);
}

uint64_t ca_ring_begin(void) {
    return ca_now_ns();
}

bool ca_ring_end(ca_ringbuf_t* rb,
                 ca_event_kind_t kind,
                 const char* agent_id,
                 const char* label,
                 uint64_t    begin_ts_ns)
{
    ca_ring_event_t evt;
    memset(&evt, 0, sizeof(evt));
    uint64_t now   = ca_now_ns();
    evt.ts_ns      = begin_ts_ns;
    evt.elapsed_ns = now - begin_ts_ns;
    evt.kind       = kind;
    if (agent_id) strncpy(evt.agent_id, agent_id, sizeof(evt.agent_id) - 1);
    if (label)    strncpy(evt.label,    label,    sizeof(evt.label) - 1);
    return ca_ring_push(rb, &evt);
}

/* ── Consumer ───────────────────────────────────────────────────────────── */
bool ca_ring_pop(ca_ringbuf_t* rb, ca_ring_event_t* out) {
    if (!rb || !out) return false;

    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&rb->head, memory_order_acquire);

    if (tail == head) return false; /* empty */

    *out = rb->slots[tail & rb->mask];

    atomic_store_explicit(&rb->tail, tail + 1, memory_order_release);
    return true;
}

uint32_t ca_ring_drain(ca_ringbuf_t* rb, ca_ring_event_t* buf, uint32_t max) {
    uint32_t n = 0;
    while (n < max && ca_ring_pop(rb, &buf[n])) n++;
    return n;
}

/* ── Stats ──────────────────────────────────────────────────────────────── */
uint64_t ca_ring_available(const ca_ringbuf_t* rb) {
    if (!rb) return 0;
    uint64_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return head - tail;
}

bool ca_ring_dropped(const ca_ringbuf_t* rb) {
    if (!rb) return false;
    return atomic_load_explicit(&rb->dropped, memory_order_relaxed);
}

void ca_ring_print(ca_ringbuf_t* rb, uint32_t max_events) {
    if (!rb) return;
    ca_ring_event_t* buf = (ca_ring_event_t*)malloc(max_events * sizeof(ca_ring_event_t));
    if (!buf) return;
    uint32_t n = ca_ring_drain(rb, buf, max_events);
    printf("\n[ca_ringbuf] Telemetry dump (%u events):\n", n);
    printf("%-6s  %-14s  %-14s  %-12s  %s\n",
           "SEQ", "KIND", "AGENT", "ELAPSED_US", "LABEL");
    printf("──────────────────────────────────────────────────────────────\n");
    for (uint32_t i = 0; i < n; i++) {
        ca_ring_event_t* e = &buf[i];
        printf("%-6u  %-14s  %-14s  %-12.3f  %s\n",
               i,
               ca_event_kind_name(e->kind),
               e->agent_id,
               (double)e->elapsed_ns / 1000.0,
               e->label);
    }
    free(buf);
}

/* ── OpenTelemetry OTLP JSON export ─────────────────────────────────────── */
/*
 * Emits NDJSON lines compatible with the OTLP/HTTP JSON encoding of
 * LogRecord objects (opentelemetry-proto LogsData schema).
 * Each line is self-contained so log collectors can ingest them directly.
 */
void ca_ring_export_otel(ca_ringbuf_t* rb, void* fp_void) {
    FILE* fp = (FILE*)fp_void;
    if (!rb || !fp) return;

    ca_ring_event_t evt;
    while (ca_ring_pop(rb, &evt)) {
        /* RFC 3339 timestamp (approximate, from ns) */
        time_t sec = (time_t)(evt.ts_ns / 1000000000ULL);
        long   ns  = (long)(evt.ts_ns % 1000000000ULL);

        fprintf(fp,
            "{\"timeUnixNano\":\"%llu\","
            "\"severityText\":\"%s\","
            "\"body\":{\"stringValue\":\"%s\"},"
            "\"attributes\":["
              "{\"key\":\"agent.id\",\"value\":{\"stringValue\":\"%s\"}},"
              "{\"key\":\"elapsed.ns\",\"value\":{\"intValue\":\"%llu\"}}"
            "],"
            "\"timestamp_sec\":%ld,\"timestamp_ns\":%ld"
            "}\n",
            (unsigned long long)evt.ts_ns,
            ca_event_kind_name(evt.kind),
            evt.label,
            evt.agent_id,
            (unsigned long long)evt.elapsed_ns,
            (long)sec,
            ns
        );
    }
    fflush(fp);
}
