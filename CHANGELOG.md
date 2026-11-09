# 📜 Cerberus Changelog & Release Notes

All notable changes to the **Cerberus** framework are documented here.

## [Unreleased]

- Everything still called CoreAgent or IronAgent is now Cerberus. This breaks imports: the Python package is `cerberus_agent` (installs as `cerberus-agent`), the native module is `_cerberus_core`, and the shared memory prefix is `/cerberus_kv_`.
- Moved `run_prompt.py` and `prompt.txt` into `examples/`.
- `pip install .` now builds and installs the native engine (`pyproject.toml` replaces `setup.py`, which never built it). The package has no required dependencies; `mcp` is an optional extra.
- The build no longer uses `-march=native`, so binaries run on any CPU of their architecture. AVX2 (x86-64) and NEON (64-bit ARM) kernels are picked when the program starts. `cerberus_agent.simd_backend()` reports the choice and `CERBERUS_SIMD=scalar` forces the plain path. `-DCERBERUS_NATIVE_ARCH=ON` brings the old behaviour back for local benchmarking.
- Fixed the NEON quantizer reading up to four bytes past the end of the code buffer.
- The C and C++ tests keep `assert()` on in Release builds (it was compiled out before, so nothing was being checked), and two out-of-date checks were corrected. The C++ suite also runs once more with the scalar kernels.
- The library no longer prints progress messages by default. Set `CERBERUS_VERBOSE=1` or call `cerberus_agent.set_verbose(True)` to get them back. The `cerberus` command still shows progress unless you pass `--quiet`.
- Replaced the benchmark scripts, which no longer ran, with `benchmarks/context_loop.py`, `benchmarks/vector_query.py` and a native `bench_native` target (`-DCERBERUS_BUILD_BENCHMARKS=ON`). They compare against a plain Python baseline and numpy, and report memory and recall as well as speed.
- Rewrote the README and `DOCUMENTATION.md`. Removed claims the code does not back up: the 4x memory saving from SQ8 (it uses about a quarter more), llama.cpp and vLLM cache sharing (there is no integration), and the 270,000 "agent iterations" figure (it measured context buffer appends).
- Added `examples/quickstart.py`, an offline example with a scripted planner, and renamed `test_local_llm.py` to `ollama_agent.py`.
- Removed `bench.gif`, which showed the old benchmark.

---

## 🚀 [v1.4.0] - 2026-07-31 (Latest Build)

### 🌟 Major Highlights: Model & Memory Co-Design

Version **v1.4.0** introduces **Zero-Copy Paged KV-Cache Arenas** and **SQ8 Quantized SIMD Vector Memory with HNSW Graph Indexing**.

#### 1. 🧠 Zero-Copy Paged KV-Cache Arenas (`KVCacheArena`)
- **Page-Aligned Memory Backing:** 4KB / 64KB page-aligned allocations using `mmap` and POSIX shared memory (`shm_open`) for zero-copy attention KV-cache sharing between local LLM runtimes (`llama.cpp`, `vLLM`) and Cerberus processes.
- **Prefix Hash Table:** 64-bit FNV-1a token hash lookup for instant prompt prefix cache hits (0-ms prefix caching).
- **Reference Counting & Pinning:** Thread-safe lock-free page reference counting to safely share KV blocks across concurrent worker agents.

#### 2. ⚡ SQ8 Vector Quantization & HNSW Graph Indexing (`VectorBuffer` Upgrade)
- **SQ8 8-Bit Scalar Quantization (`SQ8Quantizer`):** Compresses float32 vector embeddings to uint8 codes for 4x memory reduction with SIMD (AVX2 / NEON) integer dot-product calculation.
- **Hierarchical Navigable Small World Index (`HNSWIndex`):** Native C++ graph index providing logarithmic time $O(\log N)$ top-k nearest neighbor vector search.

---

## 🚀 [v1.3.0] - 2026-07-29

### 🌟 Major Highlights & System Architecture Expansion

Version **v1.3.0** introduces three core pillars designed to make Cerberus a production-grade, highly autonomous agent framework: **Hierarchical C++ Swarm Orchestration**, **Embedded SIMD Vector Memory**, and **Lockless Event Ring Telemetry**.

#### 1. 🐝 Hierarchical C++ Swarm Orchestrator (`SwarmOrchestrator`)
- **Async Fan-Out / Fan-In:** Parallel worker delegation executed directly on the C core thread pool (`ca_thread_pool_t`) using `std::packaged_task` and `std::future`.
- **Zero Copy Delegation:** Subtasks are executed asynchronously across threads without Python GIL blocking or process serialization penalties.
- **Resilient Execution:** Per-worker timeout enforcement, exception isolation, and complete `SwarmSummary` aggregation.

#### 2. 🧠 Embedded SIMD Vector Memory (`VectorBuffer`)
- **AVX2 & NEON Acceleration:** Vector dot-product operations accelerated with hardware AVX2 (x86-64) and NEON (ARM64) SIMD intrinsics with automatic scalar fallbacks.
- **In-Memory Zero-Copy RAG:** Directly embeds document chunks and performs L2-normalised cosine similarity top-k retrieval directly inside `ContextBuffer` without external vector database dependencies.

#### 3. 📡 Lockless Event Ring Telemetry (`ca_ringbuf_t` & `Telemetry`)
- **Lock-Free Telemetry Ring:** Single-Producer Single-Consumer (SPSC) lockless ring buffer using C11 atomics and cache-line aligned (64-byte) atomic head/tail cursors to eliminate false sharing.
- **Microsecond Span Tracing & OpenTelemetry:** High-resolution monotonic nanosecond timing (`ca_now_ns`) with zero-alloc event emitting and direct OpenTelemetry (OTLP) NDJSON log exporting.

---

## ⚡ [v1.2.0] - 2026-07-25

### 🌟 Major Highlights & Architecture Overhaul

Version **v1.2.0** shifts Cerberus from basic context management to **dynamic capability expansion**, enabling microsecond AST output parsing, live MCP tool hot-swapping, and parallel multi-agent shared memory without Python GIL contention.

#### 1. ⚡ C++ AST Output Parser (`OutputParser`)
- **Native Parsing in `Agent::reflect()`:** Output classification and token extraction for JSON objects, JSON arrays, scalars, and POSIX ERE regex patterns are now executed directly inside C++.
- **Zero Allocations:** Extracted string tokens point directly into the arena memory (`ca_arena_t`), completely bypassing Python string heap allocations.
- **Auto Format Detection:** Evaluates output formats (`JSON_OBJECT`, `JSON_ARRAY`, `SCALAR`, `PLAIN_TEXT`) to pass simplified values into the next planning step.

#### 2. 🔌 Dynamic Tool Registry (`DynamicToolRegistry`)
- **MCP Tool Hot-Swapping:** Remote Model Context Protocol (MCP) server tools can be registered, updated, or removed while the agent state machine is running.
- **Server Teardown & Isolation:** Added `teardownServer(server_id)` to safely strip all tools belonging to a disconnected remote MCP server without corrupting main memory or stopping the agent loop.
- **Thread-Safe Registration:** Guarded by `std::mutex` for safe concurrent updates from background Python threads.

#### 3. 🧠 Native C-Level Shared Memory (`ca_shm_t`)
- **POSIX Multi-Reader Single-Writer Lock (`pthread_rwlock_t`):** Replaced Python's `threading.Lock` dictionary with a native C core blackboard.
- **GIL-Free Multi-Agent Scaling:** Multiple worker agents can read shared blackboard facts simultaneously without blocking each other or fighting over Python's Global Interpreter Lock (GIL).
- **C++ Auto-Injection:** `Agent::think()` automatically serializes shared team facts in C++ and prepends them to the prompt context.

#### 4. 📄 Documentation & Benchmarking
- Added comprehensive technical manual [`README.md`](README.md) with updated 3-layer architecture diagrams and performance matrices.
- Verified sustained hot-loop throughput of **272,487 iterations/sec** at **14.0 MB RSS memory**.

---

## ⚡ [v1.1.0] - 2026-07-20

### Added & Improved
- **Context Arena Compaction:** Implemented semispace ping-pong garbage collection (`retained_` and `standby_` arenas) in `ContextBuffer`.
- **Zero-Copy Pybind11 Bridge:** Passed raw C memory buffers back to Python using `py::memoryview`.
- **Lazy Worker Thread Pool:** Thread pool lazy-spawns workers on first submit call with a reduced 512KB stack allocation.

---

## 📦 [v1.0.0] - 2026-07-15

### Initial Release
- Core C11 bare-metal memory arena (`ca_arena_t`).
- Basic C++ reasoning engine (`Think ➔ Plan ➔ Act ➔ Reflect`).
- Pybind11 module bindings (`_cerberus_core`).
- Basic Python wrapper API (`coreagent`).
