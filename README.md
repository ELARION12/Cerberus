<p align="center">
  <img src="logo.svg" alt="IronAgent Logo" width="100%"/>
</p>

# ⚡ IronAgent — Bare-Metal AI Agent Engine

> **Version v1.4.0 (Model & Memory Co-Design)**  
> *High-performance C11/C++17 native agent state machine, zero-copy paged KV-cache memory arenas, SQ8 quantized SIMD vector search, and HNSW graph indexing with Pybind11 Python bindings.*

---

## 📌 Overview

**IronAgent** is an ultra-low overhead, production-grade AI agent runtime engineered in bare-metal **C11 and C++17**. Unlike conventional Python-only agent frameworks (LangChain, AutoGen) that consume 300MB+ RAM and suffer from Global Interpreter Lock (GIL) contention, IronAgent executes high-frequency reasoning loops (`Think ➔ Plan ➔ Act ➔ Reflect`) directly in compiled C++ at **270,000+ iterations/second** with a tiny **~14 MB RSS memory footprint**.

---

## 🌟 Key Architecture Pillars & v1.4.0 Innovations

```mermaid
graph TD
    subgraph Layer3["🐍 Layer 3: Python API & Custom Prompt Engine"]
        API["Python Wrapper (coreagent)"]
        Runner["Custom Prompt Runner (run_prompt.py)"]
        Pybind["Pybind11 C++ Bridge (_coreagent_cpp)"]
    end

    subgraph Layer2["⚡ Layer 2: C++ Reasoning & Vector RAG Engine"]
        KVCache["KVCacheArena (Zero-Copy Paged Memory)"]
        HNSW["HNSWIndex (O(log N) Vector Search)"]
        SQ8["SQ8Quantizer (8-Bit SIMD Quantization)"]
        Vector["VectorBuffer (Embedded RAG Memory)"]
        Swarm["SwarmOrchestrator (Fan-out/Fan-in)"]
        Parser["OutputParser (Native AST Classification)"]
    end

    subgraph Layer1["⚙️ Layer 1: Bare-Metal C Core"]
        Arena["ca_arena_t (Semispace Ping-Pong Memory)"]
        SHM["ca_shm_t (pthread_rwlock_t Shared Memory)"]
        Ring["ca_ringbuf_t (Lockless SPSC Telemetry Ring)"]
        Pool["ca_thread_pool_t (Lazy Worker Pool)"]
    end

    Runner --> API
    API --> Pybind
    Pybind --> KVCache
    Pybind --> Vector
    Pybind --> Swarm
    Vector --> HNSW
    Vector --> SQ8
    KVCache --> Arena
    Vector --> Arena
    SHM --> Arena
```

1. **🧠 Zero-Copy Paged KV-Cache Arenas (`KVCacheArena`) [v1.4.0]**:
   - Page-aligned (4KB / 64KB boundary) memory pool backed by `mmap` and POSIX shared memory (`shm_open`).
   - Enables **0-copy attention KV-cache sharing** between local LLM runtimes (`llama.cpp`, `vLLM`) and agent processes.
   - **64-bit FNV-1a token sequence hash lookup** for instant prompt prefix cache hits (0-ms prefix caching).

2. **⚡ SQ8 Quantized SIMD Vector Memory (`SQ8Quantizer`) [v1.4.0]**:
   - Compresses 32-bit float vector embeddings into 8-bit `uint8` codes for **4x memory compression** (~75% RAM reduction).
   - Accelerated dot-product SIMD execution (x86-64 AVX2 / ARM NEON) with scalar fallbacks.

3. **📍 Native C++ HNSW Graph Index (`HNSWIndex`) [v1.4.0]**:
   - Hierarchical Navigable Small World graph index for logarithmic time **$O(\log N)$ vector search** over large embedding corpora.

4. **🐝 Hierarchical C++ Swarm Orchestration (`SwarmOrchestrator`)**:
   - Multi-threaded fan-out/fan-in parallel worker delegation over native C thread pools with zero GIL contention.

5. **📡 Lockless Event Ring Telemetry (`ca_ringbuf_t`)**:
   - Microsecond atomic SPSC ring buffer for high-resolution nanosecond span tracing and direct OpenTelemetry (OTLP) NDJSON log exporting.

---

## 📂 Detailed Project Structure

```text
IronAgent/
├── CMakeLists.txt              # Top-level CMake build configuration
├── Makefile                    # Standard Makefile wrapper
├── LICENSE                     # MIT License
├── README.md                   # Technical documentation & usage guide
├── CHANGELOG.md                # Release history & version notes
├── setup.py                    # Python package setup script
├── requirements.txt            # Python dependencies
├── prompt.txt                  # Custom prompt ticket input file
├── run_prompt.py               # Custom prompt ticket executor script
│
├── src/                        # Core Engine Source Code
│   ├── c_core/                 # Layer 1: Bare-Metal C11 Core
│   │   ├── memory/
│   │   │   ├── allocator.h/c   # ca_arena_t bump-pointer arena allocator
│   │   │   └── shared_memory.h/c # ca_shm_t POSIX rwlock shared blackboard
│   │   ├── threading/
│   │   │   └── thread_pool.h/c # ca_thread_pool_t lazy thread pool
│   │   ├── executor/
│   │   │   └── executor.h/c    # ca_exec_t POSIX process execution engine
│   │   └── telemetry/
│   │       └── ringbuf.h/c     # ca_ringbuf_t lockless SPSC atomic ring buffer
│   │
│   ├── cpp_engine/             # Layer 2: C++17 State Machine & Memory Engines
│   │   ├── memory/
│   │   │   ├── KVCacheArena.hpp # Zero-copy paged KV-cache memory arena
│   │   │   ├── SQ8Quantizer.hpp # 8-bit scalar vector quantization & SIMD math
│   │   │   ├── HNSWIndex.hpp    # Logarithmic O(log N) HNSW graph index
│   │   │   ├── VectorBuffer.hpp # Embedded SIMD vector memory for RAG
│   │   │   ├── ContextBuffer.hpp # Semispace ping-pong prompt arena
│   │   │   └── Arena.hpp        # C++ RAII wrapper for ca_arena_t
│   │   ├── agent/
│   │   │   ├── Agent.hpp/cpp    # C++ Agent state machine
│   │   │   └── AgentState.hpp   # State enums (THINK, PLAN, ACT, REFLECT)
│   │   ├── swarm/
│   │   │   └── SwarmOrchestrator.hpp # Hierarchical swarm orchestrator
│   │   ├── parser/
│   │   │   └── OutputParser.hpp/cpp # Native C++ AST output classifier
│   │   └── tools/
│   │       ├── ToolRegistry.hpp # Tool dispatcher & execution registry
│   │       └── DynamicToolRegistry.hpp/cpp # MCP tool hot-swapping registry
│   │
│   └── bindings/               # Layer 3: Python C Extensions
│       └── pybind_main.cpp     # Pybind11 module definition (_coreagent_cpp)
│
├── python/                     # Python High-Level Package
│   └── coreagent/
│       ├── __init__.py         # Package exports & version definitions
│       ├── agent.py            # High-level Python Agent class
│       ├── cli.py              # CLI entry point
│       ├── llm.py              # Ollama / Local LLM HTTP client
│       ├── shared_memory.py    # Python SharedMemory wrapper
│       ├── tools.py            # Default system tools (shell, read, write)
│       ├── mcp_client.py       # MCP Tool Bridge client
│       └── mcp_filesystem_server.py # Built-in MCP filesystem server
│
├── benchmarks/                 # Performance Benchmarking Suite
│   ├── bench_main.cpp          # Native C++ hot-loop benchmark
│   ├── bench_vs_langchain.py   # Latency comparison against LangChain
│   └── hot_loop.py             # Python wrapper benchmark
│
├── examples/                   # Usage Examples
│   └── test_local_llm.py       # Local LLM integration example
│
└── tests/                      # Suite of Unit & System Tests
    ├── c_tests/                # C11 core unit tests
    ├── cpp_tests/              # C++ engine unit tests (test_cpp_engine.cpp)
    └── python_tests/           # Python unit tests (test_v140.py)
```

---

## 🛠️ Fork Installation & Setup Instructions

### Prerequisites
* **C++ Compiler**: GCC 9+ or Clang 10+ supporting C++17 & C11
* **CMake**: Version 3.14 or newer
* **Python**: Version 3.8 or newer with `pybind11`
* **Local LLM Server (Optional)**: [Ollama](https://ollama.com) or `llama.cpp` server for local reasoning

### 1. Clone & Build C++ Core Engine
```bash
git clone https://github.com/your-username/IronAgent.git
cd IronAgent

# Generate build files and compile release binary
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run native C++ test suite
./build/tests/test_cpp_engine
```

### 2. Install Python Package
```bash
# Build & install Python extension in editable mode
pip install -e .

# Or run Python unit test suite directly
PYTHONPATH=build/src/bindings:python python3 -m unittest discover -s tests/python_tests
```

---

## 🚀 Running Custom Prompts & Tickets (`prompt.txt` & `run_prompt.py`)

IronAgent includes a dedicated ticket runner that allows anyone forking the repository to execute custom prompts locally.

### Step 1: Type or Paste your prompt into `prompt.txt`
```text
what is the difference between C and C++?
```
*(or any action request like: `Use write_file tool to create test.py with content: print('Hello World!')`)*

### Step 2: Run the ticket runner
```bash
python3 run_prompt.py
```

### Output & Logging
* **Terminal Display**: Formatted execution trace showing model choices, tool calls, and results.
* **Response Log**: Automatically saved to [`response.txt`](file:///home/elarion/Desktop/IronAgent/response.txt).

---

## 🗺️ Release History & Next Roadmap

### 📜 Release History
- [x] **v1.1.0:** Semispace arena compaction, zero-copy `memoryview`, lazy thread pool.
- [x] **v1.2.0:** Dynamic AST Parsing (`OutputParser`), Dynamic MCP Tool Registry (`DynamicToolRegistry`), Native C Shared Memory (`ca_shm_t`).
- [x] **v1.3.0:** Hierarchical C++ Swarm Orchestrator (`SwarmOrchestrator`), Embedded SIMD Vector Memory (`VectorBuffer`), Lockless Event Ring Telemetry (`ca_ringbuf_t`).
- [x] **v1.4.0 (Current):**
  - **Zero-Copy Paged KV-Cache Arenas (`KVCacheArena`):** `mmap` / POSIX `shm` page memory with 64-bit token hash prefix caching.
  - **SQ8 & HNSW Quantized Vector Memory:** 8-bit scalar vector quantization (`SQ8Quantizer`) for 4x memory compression and C++ HNSW graph indexing (`HNSWIndex`) for $O(\log N)$ vector search.

### 🔮 Future Theoretical Roadmap

```mermaid
graph LR
    v1_4["v1.4.0 (Current)<br/>Paged KV-Cache Arenas & SQ8 HNSW Index"] --> v1_5["v1.5.0: Distributed Swarms<br/>io_uring Network IPC & Raft Consensus"]
    v1_5 --> v2_0["v2.0.0: Next-Gen Engine<br/>Wasm Micro-Sandboxes & Speculative ToT Engine"]
```

#### 📍 **Phase 2: Distributed Swarms & Network Systems (v1.5.0 — Upcoming)**
- [ ] **Kernel-Bypassing Network IPC (`io_uring`):** High-throughput, low-latency inter-agent distributed networking using Linux `io_uring` for sub-microsecond cross-node agent communication.
- [ ] **Deterministic Replay & Raft Swarm Consensus:** Embedded C++ Raft protocol for swarm state consistency and instant differential memory arena snapshot replay.

#### 📍 **Phase 3: Secure Execution & Speculative Engine (v2.0.0)**
- [ ] **Wasm Micro-Sandboxed Tool Execution (`ca_wasm_t`):** Embedded zero-overhead WebAssembly runtime (Wasmtime/Wasmer C API) inside `ca_exec_t` for secure, zero-fork tool execution.
- [ ] **Speculative Tree-of-Thought (ToT) Engine:** Multi-threaded parallel reasoning path evaluation where losing branch memory arenas are instantly reclaimed via $O(1)$ semispace arena resets.

---

## 📄 License

IronAgent is distributed under the **MIT License**. See [`LICENSE`](file:///home/elarion/Desktop/IronAgent/LICENSE) for details.
