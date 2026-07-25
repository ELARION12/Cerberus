# 📜 IronAgent Changelog & Release Notes

All notable changes to the **IronAgent** framework are documented here.

---

## 🚀 [v1.2.0] - 2026-07-25 (Latest Build)

### 🌟 Major Highlights & Architecture Overhaul

Version **v1.2.0** shifts IronAgent from basic context management to **dynamic capability expansion**, enabling microsecond AST output parsing, live MCP tool hot-swapping, and parallel multi-agent shared memory without Python GIL contention.

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
- Pybind11 module bindings (`_coreagent_cpp`).
- Basic Python wrapper API (`coreagent`).
