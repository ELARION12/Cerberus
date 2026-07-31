# ⚡ IronAgent Technical Manual & Architectural Blueprint

> **High-Performance Bare-Metal AI Agent Engine (C11 / C++17 / Pybind11 / Python)**  
> **Document Version:** 1.4.0 | **Status:** Active Release

---

## 📌 Executive Summary & Core Philosophy

**IronAgent** is an ultra-low latency, production-grade AI agent runtime engineered from first principles in compiled **C11 and C++17**.

Traditional Python-only agent frameworks (LangChain, AutoGen, CrewAI) suffer from heavy RAM bloat (300 MB – 800 MB+ RSS per agent), high garbage collection pauses, and Global Interpreter Lock (GIL) contention during multi-agent scaling. 

IronAgent solves this bottleneck by shifting the core agent state machine (`Think ➔ Plan ➔ Act ➔ Reflect`), prompt serialization, memory arena management, and vector similarity search into a **bare-metal C/C++ engine**. 

### Key System Metrics
* **State Iteration Speed:** **270,000+ state iterations/second**
* **Memory Footprint:** **~14 MB RSS RAM**
* **Vector RAG Latency:** **$< 0.5\text{ ms}$** via HNSW graph & SIMD AVX2/NEON quantization
* **Execution Safety:** Zero heap allocations after startup via bump-pointer memory arenas

---

## 🏗️ 3-Layer System Architecture

```mermaid
graph TD
    subgraph Layer3["🐍 Layer 3: Python API & Custom Prompt Engine"]
        API["Python Wrapper (coreagent.Agent)"]
        Runner["Custom Prompt Runner (run_prompt.py)"]
        Pybind["Pybind11 C++ Bridge (_coreagent_cpp)"]
    end

    subgraph Layer2["⚡ Layer 2: C++17 Reasoning & Memory Engine"]
        KVCache["KVCacheArena (Zero-Copy Paged Memory)"]
        HNSW["HNSWIndex (O(log N) Vector Search)"]
        SQ8["SQ8Quantizer (8-Bit SIMD Quantization)"]
        Vector["VectorBuffer (Embedded RAG Memory)"]
        Swarm["SwarmOrchestrator (Fan-out/Fan-in)"]
        Parser["OutputParser (Native AST Classification)"]
    end

    subgraph Layer1["⚙️ Layer 1: Bare-Metal C11 Core"]
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

---

## 🗺️ Project Evolution & Release Phases

### 📍 Phase 1: Bare-Metal Core Foundation (v1.0.0 – v1.2.0)
* **v1.0.0 (Core Engine):** Implemented bare-metal C11 bump-pointer memory arena (`ca_arena_t`), C++ execution loop, and Pybind11 Python bindings.
* **v1.1.0 (Memory Compaction):** Semispace ping-pong garbage collection (`retained_` & `standby_` arenas) inside `ContextBuffer` to prevent out-of-memory errors. Zero-copy `memoryview` transfers to Python.
* **v1.2.0 (Dynamic Capability Expansion):** Microsecond C++ AST output classifier (`OutputParser`), dynamic MCP tool hot-swapping (`DynamicToolRegistry`), and POSIX multi-reader single-writer shared memory (`ca_shm_t` with `pthread_rwlock_t`).

### 📍 Phase 2: Swarm Systems & SIMD RAG (v1.3.0)
* **Hierarchical C++ Swarm Orchestration (`SwarmOrchestrator`):** Multi-threaded parallel worker delegation executed on native C thread pools with zero Python GIL contention.
* **Embedded SIMD Vector RAG (`VectorBuffer`):** Hardware AVX2/NEON vector dot-product similarity search for zero-dependency local document retrieval.
* **Lockless Ring Buffer Telemetry (`ca_ringbuf_t`):** Single-producer single-consumer (SPSC) atomic ring buffer for microsecond span tracing and OpenTelemetry (OTLP) exporting.

### 📍 Phase 3: Model & Memory Co-Design (v1.4.0 — Current)
* **Zero-Copy Paged KV-Cache Arenas (`KVCacheArena`):** Page-aligned memory backing (`mmap` / POSIX `shm_open`) with 64-bit FNV-1a token sequence hash tables for **0-ms system prompt prefix caching**.
* **SQ8 Vector Quantization (`SQ8Quantizer`):** Compresses 32-bit float vector embeddings into 8-bit `uint8` codes for **4x RAM reduction** with integer SIMD math.
* **Native C++ HNSW Graph Indexing (`HNSWIndex`):** Logarithmic time $O(\log N)$ approximate nearest neighbor search over large embedding corpora.

---

## 🔮 Future Roadmap (v1.5.0 – v2.0.0)

```mermaid
graph LR
    v1_4["v1.4.0 (Current)<br/>Paged KV-Cache Arenas & SQ8 HNSW Index"] --> v1_5["v1.5.0: Distributed Swarms<br/>io_uring Network IPC & Raft Consensus"]
    v1_5 --> v2_0["v2.0.0: Next-Gen Engine<br/>Wasm Micro-Sandboxes & Speculative ToT Engine"]
```

### 📍 Phase 4: Distributed Swarms & Kernel-Bypassing IPC (v1.5.0 — Upcoming)
* **`io_uring` Kernel-Bypassing IPC:** High-throughput, sub-microsecond cross-node agent communication using Linux `io_uring`.
* **Raft Swarm Consensus:** Embedded C++ Raft protocol for swarm state synchronization and instant differential memory arena snapshot replay.

### 📍 Phase 5: Secure Execution & Speculative Engine (v2.0.0)
* **Wasm Micro-Sandboxing (`ca_wasm_t`):** Embedded WebAssembly runtime (Wasmtime / Wasmer C API) inside `ca_exec_t` for secure, zero-fork tool execution.
* **Speculative Tree-of-Thought (ToT) Engine:** Multi-threaded parallel reasoning path evaluation where losing branch memory arenas are instantly reclaimed via $O(1)$ semispace resets.

---

## 💡 Operational Modes: With LLM vs. Without LLM

| Dimension | ⚡ IronAgent WITHOUT LLM | 🧠 IronAgent WITH LLM |
| :--- | :--- | :--- |
| **Brain / Planner** | **C++ Pattern & Regex Classifier** (`OutputParser`) | **Local / Cloud LLM** (Ollama, Qwen, Llama 3) |
| **Step Speed** | **$< 0.01\text{ ms}$ (270,000+ iter/sec)** | **~15 – 50 ms / step** |
| **RAM Requirement** | **~14 MB RAM** | **~14 MB + Model Size (~400MB - 2GB)** |
| **Task Type** | Deterministic tool execution, fixed rules, SIMD vector RAG search | Unstructured natural language prompts, code writing, self-correction |

---

## 🎯 Detailed Real-World Applications

1. **Embedded Linux & Edge AI Systems (IoT, Drones, Robotics)**
   - Operates on resource-constrained single-board computers (Raspberry Pi 4/5, NVIDIA Jetson) at **~14 MB RAM** where traditional Python frameworks crash.
   - Executes real-time sensor monitoring, motor control commands, and local offline decision-making.

2. **Autonomous Software Engineering & Repository Maintenance**
   - High-speed code scanning, AST parsing, automated refactoring, unit test execution, and git workflow management.

3. **High-Throughput Multi-Agent Swarm Processing**
   - Parallel worker delegation (`SwarmOrchestrator`) across multi-core processors for concurrent data scraping, document analysis, and research tasks without GIL locks.

4. **Zero-Dependency Private Local Knowledge RAG**
   - Offline document indexing and sub-millisecond similarity search using embedded C++ HNSW and SQ8 SIMD quantization without sending sensitive data to external cloud vector databases.

---

## 📄 License

IronAgent is released under the **MIT License**.
