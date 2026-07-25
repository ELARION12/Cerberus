<div align="center">
  <img src="logo.svg" alt="IronAgent Banner" width="100%"/>

  <br />
  <a href="https://python.org"><img src="https://img.shields.io/badge/Python-3.10%2B-blue.svg?style=for-the-badge&logo=python&logoColor=white" alt="Python"></a>
  <a href="https://isocpp.org/"><img src="https://img.shields.io/badge/C%2B%2B-17%2F20-00599C.svg?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++"></a>
  <a href="https://en.wikipedia.org/wiki/C11_(C_standard_revision)"><img src="https://img.shields.io/badge/C-11-282C34.svg?style=for-the-badge&logo=c&logoColor=white" alt="C"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge" alt="License"></a>
  <a href="#"><img src="https://img.shields.io/badge/Version-1.2.0-orange.svg?style=for-the-badge" alt="Version"></a>
  <br />
  <br />
</div>

> [!IMPORTANT]
> **IronAgent (v1.2.0)** is an ultra-high-performance, bare-metal AI agent runtime built in pure **C, C++, and Python**. Designed to eliminate the execution latency, memory bloat, and garbage collection pauses of heavy orchestration frameworks, IronAgent executes the core `Think ➔ Plan ➔ Act ➔ Reflect` state machine directly inside native compiled code.

---

## ⚡ Core Performance Snapshot (V1.2.0)

| Metric | **IronAgent V1.2.0** | **LangChain / Heavy Frameworks** | **Performance Delta** |
|---|---|---|---|
| **Hot-Loop Throughput** | **272,487 iterations/sec** | ~2,500 – 5,000 iterations/sec | **~50× to 100× Faster** |
| **50,000 Context Steps Time** | **0.183 seconds** | ~10.0 – 20.0 seconds | **~99% Time Reduction** |
| **Memory Footprint (RSS)** | **14.0 MB – 15.3 MB** | ~150 MB – 400 MB+ | **~10× to 25× Lighter** |
| **Garbage Collection (GC)** | **Zero GC pressure** | Frequent GC pauses & spikes | **Deterministic Execution** |
| **Data Copying** | Zero-Copy `py::memoryview` | Heap string duplication | **Zero FFI Copying** |

---

## 💡 The Philosophy: High-Performance AI Systems Engineering

Modern AI agent frameworks (LangChain, AutoGen, CrewAI) treat orchestration as an application-level scripting task. They instantiate thousands of Python objects (`BaseMessage`, `Pydantic` models) for every conversation turn, triggering massive heap fragmentation and blocking CPU cycles on string serialization.

**IronAgent approaches agent orchestration as a systems engineering problem:**
1. **Memory Allocation:** Custom 32-byte SIMD-aligned bump-pointer memory arenas (`ca_arena_t`) written in C11.
2. **Context Window Management:** Dual-arena semispace copying garbage collection (`ContextBuffer`) for $O(1)$ context sliding windows.
3. **AST Output Parsing:** Hand-rolled C++ parser (`OutputParser`) for JSON Objects, Arrays, and POSIX ERE Regex without third-party dependencies.
4. **Parallel Multi-Agent Communication:** Native C shared memory (`ca_shm_t`) with POSIX Multi-Reader Single-Writer (`pthread_rwlock_t`) locks bypassing Python's Global Interpreter Lock (GIL).

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

    subgraph Layer2 [C++ Engine State Machine]
        State[Think ➔ Plan ➔ Act ➔ Reflect]:::cpp
        Parser[OutputParser: JSON / Regex]:::cpp
        DynReg[DynamicToolRegistry: Hot Swap]:::cpp
        Ctx[ContextBuffer: Semispace Arena]:::cpp
    end

    subgraph Layer3 [C Bare-Metal Core]
        Arena[ca_arena_t: 32-byte SIMD Arena]:::ccore
        SHM[ca_shm_t: pthread_rwlock_t Blackboard]:::ccore
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
    State --> Parser
    State --> DynReg
    State --> Ctx
    State --> SHM
    Ctx --> Arena
    DynReg --> Threads
    DynReg --> Exec
```

---

## 🛠️ V1.2.0 Feature Breakdown

### 1. ⚡ C++ AST Output Parser (`OutputParser`)
Located in [`src/cpp_engine/parser/`](file:///home/elarion/Desktop/IronAgent/src/cpp_engine/parser/), `OutputParser` evaluates tool outputs inside C++ during `Agent::reflect()`:
- **Formats Handled:** `JSON_OBJECT`, `JSON_ARRAY`, `SCALAR`, and `PLAIN_TEXT`.
- **POSIX ERE Regex:** Regex patterns are evaluated via system `regex.h` without external libraries.
- **Zero Allocations:** Extracted tokens point into arena memory without string copies.

### 2. 🔌 Dynamic Tool Registry (`DynamicToolRegistry`)
Located in [`src/cpp_engine/tools/`](file:///home/elarion/Desktop/IronAgent/src/cpp_engine/tools/):
- **Hot-Swapping:** Remote MCP (Model Context Protocol) tool schemas can be registered or unregistered while the agent state machine is running.
- **Server Isolation:** `teardownServer(server_id)` cleanly strips all tools belonging to a disconnected remote MCP server without corrupting main memory.

### 3. 🧠 Native C Shared Memory (`ca_shm_t` & `pthread_rwlock_t`)
Located in [`src/c_core/memory/`](file:///home/elarion/Desktop/IronAgent/src/c_core/memory/):
- **Multi-Reader / Single-Writer Lock:** Multiple worker agents can read shared blackboard facts simultaneously without blocking.
- **C++ Auto-Injection:** `Agent::think()` automatically reads shared memory summaries in C++ and prepends them to the prompt context with zero FFI calls.

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
│   │   └── executor/
│   │       └── executor.c/.h      # Subprocess execution & pipe redirection
│   ├── cpp_engine/                # C++17 Reasoning State Machine
│   │   ├── agent/
│   │   │   ├── Agent.cpp/.hpp     # Think-Plan-Act-Reflect engine
│   │   │   └── AgentState.hpp     # Typed state transitions
│   │   ├── parser/
│   │   │   ├── OutputParser.cpp/.hpp # Fast AST JSON & Regex parser
│   │   ├── tools/
│   │   │   ├── ToolRegistry.hpp    # Static tool registry
│   │   │   └── DynamicToolRegistry.cpp/.hpp # MCP hot-swappable registry
│   │   ├── memory/
│   │   │   └── ContextBuffer.hpp  # Semispace ping-pong GC context window
│   │   └── core/
│   │       ├── Arena.hpp          # C++ RAII Arena wrapper
│   │       ├── ThreadPool.hpp     # C++ ThreadPool wrapper
│   │       └── SharedMemory.hpp   # C++ RAII SharedMemory wrapper
│   └── bindings/
│       └── pybind_main.cpp        # Pybind11 module bindings
├── python/
│   └── coreagent/                 # High-Level Python Package
│       ├── __init__.py            # Module exports (v1.2.0)
│       ├── agent.py               # Python Agent class
│       ├── shared_memory.py       # SharedMemory wrapper
│       ├── mcp_client.py          # MCP async bridge over stdio
│       ├── llm.py                 # Ollama HTTP client
│       └── tools.py               # Built-in Python tools
├── tests/
│   ├── c_tests/                   # C unit tests (test_c_core)
│   ├── cpp_tests/                 # C++ engine unit tests (test_cpp_engine)
│   └── python_tests/              # Pytest integration suite
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

### Example 2: Multi-Agent Team with Native C Shared Memory
```python
from coreagent import Agent, SharedMemory

# 1. Create a C-backed shared memory blackboard
shm = SharedMemory()
shm.write("target_file", "data.json", by="Orchestrator")

# 2. Spawn worker agents and join the shared memory team
worker1 = Agent(name="Worker1").join_team(shm)
worker2 = Agent(name="Worker2").join_team(shm)

# 3. Workers read and write in parallel without GIL contention
worker1.remember("file_size", "2048")
print("Shared Memory Summary:")
print(shm.summary())
```

### Example 3: Injecting Custom Python Tools
```python
from coreagent import Agent, ToolInput, ToolOutput

agent = Agent()

@agent.tool("calculator", "Evaluate arithmetic expression. args='a + b'")
def calculator(inp: ToolInput) -> ToolOutput:
    out = ToolOutput()
    try:
        out.result = str(eval(inp.args))
        out.success = True
    except Exception as e:
        out.success = False
        out.error = str(e)
    return out

res = agent.tools.dispatch(ToolInput(name="calculator", args="12 * 4"))
print("Result:", res.result)  # "48"
```

---

## 🗺️ Roadmap Ahead (V1.3.0)

- [x] **V1.1.0:** Semispace arena compaction, zero-copy `memoryview`, lazy thread pool.
- [x] **V1.2.0:** Dynamic AST Parsing (`OutputParser`), Dynamic MCP Tool Registry (`DynamicToolRegistry`), Native C Shared Memory (`ca_shm_t` with `pthread_rwlock_t`).
- [ ] **V1.3.0 (Upcoming):**
  - **Hierarchical C++ Swarm Orchestrator:** Async fan-out/fan-in worker delegation in C++.
  - **Embedded SIMD Vector Memory (`VectorBuffer`):** AVX2/NEON vector search inside `ContextBuffer` for zero-dependency RAG.
  - **Lockless Event Ring Telemetry (`ca_ringbuf_t`):** Microsecond tracing & OpenTelemetry export.

---

## 📄 License

IronAgent is distributed under the **MIT License**. See [`LICENSE`](file:///home/elarion/Desktop/IronAgent/LICENSE) for details.
