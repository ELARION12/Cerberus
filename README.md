<div align="center">
  <img src="logo.svg" alt="IronAgent Banner" width="100%"/>

  <br />
  <a href="https://python.org"><img src="https://img.shields.io/badge/Python-3.10%2B-blue.svg?style=for-the-badge&logo=python&logoColor=white" alt="Python"></a>
  <a href="https://isocpp.org/"><img src="https://img.shields.io/badge/C%2B%2B-17%2F20-00599C.svg?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++"></a>
  <a href="https://en.wikipedia.org/wiki/C11_(C_standard_revision)"><img src="https://img.shields.io/badge/C-11-282C34.svg?style=for-the-badge&logo=c&logoColor=white" alt="C"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge" alt="License"></a>
  <a href="CHANGELOG.md"><img src="https://img.shields.io/badge/Latest_Build-v1.3.0-brightgreen.svg?style=for-the-badge" alt="Latest Build"></a>
  <br />
  <br />
</div>

> [!IMPORTANT]
> **IronAgent (v1.3.0)** is an ultra-high-performance, bare-metal AI agent runtime built in pure **C11, C++17, and Python**. Designed to eliminate the execution latency, memory bloat, and garbage collection pauses of heavy orchestration frameworks, IronAgent executes the core `Think ➔ Plan ➔ Act ➔ Reflect` state machine, swarm orchestration, SIMD RAG vector memory, and lockless telemetry directly inside native compiled code.

---

## ⚡ Core Performance Snapshot (V1.3.0)

<div align="center">
  <img src="bench.gif" alt="IronAgent Benchmark vs LangChain" width="100%"/>
</div>

| Metric | **IronAgent V1.3.0** | **LangChain / Heavy Frameworks** | **Performance Delta** |
|---|---|---|---|
| **Hot-Loop Throughput** | **272,487 iterations/sec** | ~2,500 – 5,000 iterations/sec | **~50× to 100× Faster** |
| **50,000 Context Steps Time** | **0.183 seconds** | ~10.0 – 20.0 seconds | **~99% Time Reduction** |
| **Memory Footprint (RSS)** | **14.0 MB – 16.2 MB** | ~150 MB – 400 MB+ | **~10× to 25× Lighter** |
| **Vector Dot-Product (SIMD)** | **AVX2 / NEON hardware native** | Python NumPy / PyTorch overhead | **Microsecond Top-K Search** |
| **Telemetry Overhead** | **Lock-free (<50ns / event)** | Heavy logging wrappers / GC allocs | **Near-Zero Telemetry Cost** |

---

## 💡 The Philosophy: High-Performance AI Systems Engineering

Modern AI agent frameworks (LangChain, AutoGen, CrewAI) treat orchestration as an application-level scripting task. They instantiate thousands of Python objects (`BaseMessage`, `Pydantic` models) for every conversation turn, triggering massive heap fragmentation and blocking CPU cycles on string serialization.

**IronAgent approaches agent orchestration as a systems engineering problem:**
1. **Memory Allocation:** Custom 32-byte SIMD-aligned bump-pointer memory arenas (`ca_arena_t`) written in C11.
2. **Context Window Management:** Dual-arena semispace copying garbage collection (`ContextBuffer`) for $O(1)$ context sliding windows.
3. **AST Output Parsing:** Hand-rolled C++ parser (`OutputParser`) for JSON Objects, Arrays, and POSIX ERE Regex without third-party dependencies.
4. **Parallel Multi-Agent Communication:** Native C shared memory (`ca_shm_t`) with POSIX Multi-Reader Single-Writer (`pthread_rwlock_t`) locks bypassing Python's Global Interpreter Lock (GIL).
5. **Hierarchical Swarm Delegation:** Native C++ async fan-out/fan-in worker orchestrator (`SwarmOrchestrator`) built on POSIX pthreads.
6. **Embedded SIMD Vector RAG:** AVX2/NEON vector dot-product engine (`VectorBuffer`) for zero-dependency local retrieval.
7. **Lockless Telemetry Ring Buffer:** Single-Producer Single-Consumer lockless event ring (`ca_ringbuf_t`) with OpenTelemetry NDJSON export.

---

## 🏛️ Deep-Dive Architecture Diagram

```mermaid
graph TD
    %% Styling
    classDef python fill:#4B8BBE,stroke:#306998,stroke-width:2px,color:white;
    classDef cpp fill:#00599C,stroke:#004482,stroke-width:2px,color:white;
    classDef ccore fill:#282C34,stroke:#61DAFB,stroke-width:2px,color:white;
    classDef boundary fill:#2EA043,stroke:#238636,stroke-width:2px,color:white,stroke-dasharray: 5 5;

    %% Nodes
    User((User / Application))
    LLM{{Local Ollama / Remote LLM}}

    subgraph Layer1 [Python 3.10+ Interface]
        API[Agent Wrapper API]:::python
        MCPBridge[MCP Async Bridge]:::python
        PySHM[SharedMemory Handle]:::python
    end

    subgraph FFI [Pybind11 Zero-Copy Boundary]
        Bridge{py::memoryview & Trampolines}:::boundary
    end

    subgraph Layer2 [C++ Engine State Machine & Systems]
        State[Think ➔ Plan ➔ Act ➔ Reflect]:::cpp
        Swarm[SwarmOrchestrator: Fan-Out/In]:::cpp
        Vector[VectorBuffer: AVX2/NEON SIMD RAG]:::cpp
        Parser[OutputParser: JSON / Regex]:::cpp
        DynReg[DynamicToolRegistry: Hot Swap]:::cpp
        Ctx[ContextBuffer: Semispace Arena]:::cpp
        TelemCPP[Telemetry: RAII Ring Buffer Wrapper]:::cpp
    end

    subgraph Layer3 [C Bare-Metal Core]
        Arena[ca_arena_t: 32-byte SIMD Arena]:::ccore
        SHM[ca_shm_t: pthread_rwlock_t Blackboard]:::ccore
        Ring[ca_ringbuf_t: Lockless SPSC Ring Buffer]:::ccore
        Threads[ca_thread_pool_t: Lazy Pthread Pool]:::ccore
        Exec[ca_exec_t: Process Executor]:::ccore
    end

    %% Connections
    User --> API
    API <--> LLM
    API <--> PySHM
    MCPBridge <--> Bridge
    API <--> Bridge
    Bridge <--> State
    Bridge <--> Swarm
    Bridge <--> Vector
    State --> Parser
    State --> DynReg
    State --> Ctx
    State --> SHM
    Swarm --> Threads
    Vector --> Arena
    TelemCPP --> Ring
    Ctx --> Arena
    DynReg --> Threads
    DynReg --> Exec
```

---

## 🛠️ V1.3.0 Feature Breakdown

### 1. 🐝 Hierarchical C++ Swarm Orchestrator (`SwarmOrchestrator`)
Located in [`src/cpp_engine/swarm/`](file:///home/elarion/Desktop/IronAgent/src/cpp_engine/swarm/):
- **Async Fan-Out / Fan-In:** Parallel worker delegation executed on the C worker thread pool (`ca_thread_pool_t`) using `std::packaged_task` and `std::future`.
- **Zero GIL Blocking:** Multi-worker task execution bypasses Python GIL bottlenecks completely.
- **Resilient Aggregation:** Isolates worker failures, enforces worker timeout limits, and compiles outputs into a unified `SwarmSummary`.

### 2. 🧠 Embedded SIMD Vector Memory (`VectorBuffer`)
Located in [`src/cpp_engine/memory/`](file:///home/elarion/Desktop/IronAgent/src/cpp_engine/memory/):
- **Hardware Acceleration:** Computes vector dot-products using AVX2 (x86-64) or NEON (ARM64) SIMD intrinsics with scalar fallbacks.
- **In-Memory Zero-Copy RAG:** Stores L2-normalised float embeddings and text payloads directly in arena memory for instant local RAG context injection.

### 3. 📡 Lockless Event Ring Telemetry (`ca_ringbuf_t` & `Telemetry`)
Located in [`src/c_core/telemetry/`](file:///home/elarion/Desktop/IronAgent/src/c_core/telemetry/):
- **Lock-Free Ring Buffer:** Single-Producer Single-Consumer (SPSC) ring buffer with 64-byte cache-line aligned atomic cursors preventing false sharing.
- **OpenTelemetry Export:** Serializes high-precision nanosecond traces into standard OpenTelemetry (OTLP) NDJSON log format.

---

## 📂 Repository Structure

```
IronAgent/
├── CMakeLists.txt                 # Master CMake configuration
├── src/
│   ├── c_core/                    # C11 Bare-Metal Core
│   │   ├── memory/
│   │   │   ├── allocator.c/.h     # 32-byte SIMD bump-pointer arena
│   │   │   └── shared_memory.c/.h # Native C ca_shm_t with pthread_rwlock_t
│   │   ├── threading/
│   │   │   └── thread_pool.c/.h   # Lazy pthread worker pool
│   │   ├── telemetry/
│   │   │   └── ringbuf.c/.h       # Lockless SPSC telemetry ring buffer
│   │   └── executor/
│   │       └── executor.c/.h      # Subprocess execution & pipe redirection
│   ├── cpp_engine/                # C++17 Reasoning & System Modules
│   │   ├── agent/
│   │   │   ├── Agent.cpp/.hpp     # Think-Plan-Act-Reflect engine
│   │   │   └── AgentState.hpp     # Typed state transitions
│   │   ├── swarm/
│   │   │   └── SwarmOrchestrator.hpp # C++ Async Fan-Out/Fan-In Orchestrator
│   │   ├── memory/
│   │   │   ├── ContextBuffer.hpp  # Semispace ping-pong GC context window
│   │   │   └── VectorBuffer.hpp   # AVX2/NEON SIMD Vector RAG Memory
│   │   ├── parser/
│   │   │   ├── OutputParser.cpp/.hpp # Fast AST JSON & Regex parser
│   │   ├── tools/
│   │   │   ├── ToolRegistry.hpp    # Static tool registry
│   │   │   └── DynamicToolRegistry.cpp/.hpp # MCP hot-swappable registry
│   │   └── core/
│   │       ├── Arena.hpp          # C++ RAII Arena wrapper
│   │       ├── ThreadPool.hpp     # C++ ThreadPool wrapper
│   │       ├── SharedMemory.hpp   # C++ RAII SharedMemory wrapper
│   │       └── Telemetry.hpp      # C++ RAII Telemetry wrapper
│   └── bindings/
│       └── pybind_main.cpp        # Pybind11 module bindings (v1.3.0)
├── python/
│   └── coreagent/                 # High-Level Python Package
│       ├── __init__.py            # Module exports (v1.3.0)
│       ├── agent.py               # Python Agent class
│       ├── shared_memory.py       # SharedMemory wrapper
│       ├── mcp_client.py          # MCP async bridge over stdio
│       ├── llm.py                 # Ollama HTTP client
│       └── tools.py               # Built-in Python tools
├── tests/
│   ├── c_tests/                   # C unit tests (test_c_core)
│   ├── cpp_tests/                 # C++ engine unit tests (test_cpp_engine)
│   └── python_tests/              # Pytest integration suite (test_v12, test_v13)
└── benchmarks/                    # Benchmark scripts (bench_vs_langchain, hot_loop)
```

---

## ⚙️ Building & Testing

### 1. Prerequisites
- **C/C++ Compiler:** `gcc` or `clang` with C++17 support
- **Build System:** `cmake` (>= 3.18)
- **Python:** Python 3.10 or higher

### 2. Compilation
```bash
# 1. Configure build system
cmake -B build -S .

# 2. Build C core, C++ engine, test suite, and Python extension module
cmake --build build

# 3. Install Python package in editable mode
pip install -e .
```

### 3. Running Test Suites
```bash
# Execute C & C++ native test executables
ctest --test-dir build --output-on-failure

# Execute Python test suite
python3 -m pytest tests/ -v
```

---

## 💻 Code Walkthrough & Examples

### Example 1: Basic Single-Agent Loop
```python
from coreagent import Agent

agent = Agent(name="DevBot")
agent.context.add_system("You are an autonomous high-speed AI agent.")

# Runs the THINK -> PLAN -> ACT -> REFLECT loop
result = agent.run("list the files in current directory")
print("Result:", result)
```

### Example 2: Embedded SIMD Vector RAG Memory (`VectorBuffer`)
```python
from coreagent import VectorBuffer

# 1. Create a 4-dimensional vector memory
vb = VectorBuffer(dim=4)

# 2. Store embeddings with document text
vb.push([1.0, 0.0, 0.0, 0.0], "CPU Architecture & Cache Alignment")
vb.push([0.0, 1.0, 0.0, 0.0], "Database Index B-Trees")

# 3. Top-K SIMD vector search
matches = vb.query([0.95, 0.05, 0.0, 0.0], top_k=1)
print("Best Match:", matches[0].text, "Score:", matches[0].score)

# 4. Generate RAG prompt snippet
print(vb.rag_context([0.95, 0.05, 0.0, 0.0], top_k=1))
```

### Example 3: Lockless Event Ring Telemetry (`Telemetry`)
```python
from coreagent import Telemetry, TelemetryKind

# Initialize lock-free telemetry ring
telem = Telemetry(capacity=1024)

# Record events with nanosecond precision
telem.emit(TelemetryKind.THINK, "AgentA", "analyzing_task")
telem.emit(TelemetryKind.ACT,   "AgentA", "executing_tool")

# Export OpenTelemetry NDJSON logs to file
telem.export_otel_to_file("telemetry.log")
```

## 🗺️ Roadmap & Release History

### 📜 Release History
- [x] **V1.1.0:** Semispace arena compaction, zero-copy `memoryview`, lazy thread pool.
- [x] **V1.2.0:** Dynamic AST Parsing (`OutputParser`), Dynamic MCP Tool Registry (`DynamicToolRegistry`), Native C Shared Memory (`ca_shm_t` with `pthread_rwlock_t`).
- [x] **V1.3.0 (Current):**
  - **Hierarchical C++ Swarm Orchestrator (`SwarmOrchestrator`):** Async fan-out/fan-in worker delegation.
  - **Embedded SIMD Vector Memory (`VectorBuffer`):** AVX2/NEON vector search inside `ContextBuffer`.
  - **Lockless Event Ring Telemetry (`ca_ringbuf_t`):** Microsecond tracing & OpenTelemetry export.
- [ ] **V1.4.0 (Upcoming Roadmap):**
  - **Zero-Copy Paged KV-Cache Arenas:** Mmap / CUDA IPC integration between `ca_arena_t` and local LLM runtimes (`llama.cpp` / `vLLM`).
  - **HNSW & Quantized SIMD Vector Memory:** Native C++ HNSW graph indexing and SQ8/Int8 quantization for `VectorBuffer`.
  - **Kernel-Bypassing `io_uring` Network IPC:** Sub-microsecond distributed multi-agent inter-node communication.
  - **Wasm Micro-Sandboxed Execution (`ca_wasm_t`):** Embedded WebAssembly runtime for secure zero-fork tool execution.

### 🔮 Future Theoretical Roadmap

```mermaid
graph LR
    v1_3["v1.3.0 (Current)<br/>SIMD Vector RAG & Lockless SPSC Ring"] --> v1_4["v1.4.0: Model & Memory Co-Design<br/>KV-Cache & HNSW Vector Indexing"]
    v1_4 --> v1_5["v1.5.0: Distributed Swarms<br/>io_uring Network IPC & Raft Consensus"]
    v1_5 --> v2_0["v2.0.0: Next-Gen Engine<br/>Wasm Micro-Sandboxes & Speculative Planning"]
```

#### 📍 **Phase 1: Model & Memory Co-Design (v1.4.0)**
- [ ] **Zero-Copy Paged KV-Cache Arenas:** Mmap / CUDA IPC integration between `ca_arena_t` and local LLM runtimes (llama.cpp / vLLM) for zero-copy system prompt KV-cache sharing.
- [ ] **HNSW & Quantized SIMD Vector Memory:** Upgrade `VectorBuffer` with native C++ HNSW graph indexing and SQ8/Int8 quantization for sub-millisecond similarity search across large embedding corpora.

#### 📍 **Phase 2: Distributed Swarms & Network Systems (v1.5.0)**
- [ ] **Kernel-Bypassing Network IPC (`io_uring`):** High-throughput, low-latency inter-agent distributed networking using Linux `io_uring` for sub-microsecond cross-node agent communication.
- [ ] **Deterministic Replay & Raft Swarm Consensus:** Embedded C++ Raft protocol for swarm state consistency and instant differential memory arena snapshot replay.

#### 📍 **Phase 3: Secure Execution & Speculative Engine (v2.0.0)**
- [ ] **Wasm Micro-Sandboxed Tool Execution (`ca_wasm_t`):** Embed a zero-overhead WebAssembly runtime (Wasmtime/Wasmer C API) inside `ca_exec_t` for secure, zero-fork tool execution.
- [ ] **Speculative Tree-of-Thought (ToT) Engine:** Multi-threaded parallel reasoning path evaluation where losing branch memory arenas are instantly reclaimed via $O(1)$ semispace arena resets.

---

## 📄 License

IronAgent is distributed under the **MIT License**. See [`LICENSE`](file:///home/elarion/Desktop/IronAgent/LICENSE) for details.
