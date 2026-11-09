# Cerberus design notes

Version 1.4.0. These notes describe how the pieces fit together and what each one does today. For install steps and measured numbers see the [README](README.md).

## Layers

```
Python API       python/cerberus_agent   Agent, tools, Ollama client, MCP bridge
C++17 engine     src/cpp_engine          context buffer, vector memory, tool registry, swarm
C11 core         src/c_core              arena allocator, thread pool, process executor, event ring
```

pybind11 (`src/bindings/pybind_main.cpp`) exposes the engine to Python as `cerberus_agent._cerberus_core`. The C core depends only on the C library and pthreads.

## Data structures

### Arena allocator (`ca_arena_t`)

A bump-pointer arena. An allocation moves an offset forward, so it costs a few nanoseconds and there is no per-object free. The arena is reset or discarded as a whole. The header is aligned to a cache line.

```c
typedef struct CA_CACHE_ALIGN {
    uint8_t*  base;        /* aligned base pointer */
    size_t    capacity;    /* size at creation */
    size_t    offset;      /* current bump offset */
    size_t    peak;        /* high-water mark across resets */
    uint32_t  alloc_count;
} ca_arena_t;
```

### Context buffer (`ContextBuffer`)

Holds the conversation. Message text lives in two equal arenas, `retained_` and `standby_`:

1. New messages are allocated into `retained_`.
2. When `retained_` is 75% full, `compact()` copies the live messages into `standby_` and fixes their string pointers.
3. The two arenas swap and the old one is reset in constant time.

A third arena, `scratch_`, holds the rendered prompt. Each `buildPrompt()` call overwrites the previous one, so only one prompt is live at a time. The returned view is valid until the next call.

When the estimated token count goes over the budget, the oldest non-system messages are dropped.

Prompt format: one line per message, labelled with its role: `system: ...`, `user: ...`, `assistant: ...`, and `tool <name>: ...` for tool results. The label lets a model tell a request from a tool result, which matters once a conversation holds more than one step.

### Vector memory (`VectorBuffer`)

Stores L2-normalised float32 embeddings with a text payload per vector. `query()` scores every stored vector with a dot product and returns the top k, so cost grows linearly with the number of vectors. The dot product uses AVX2 on x86-64 or NEON on 64-bit ARM (see below).

Two optional extras, both with caveats:

- **SQ8** (`enable_sq8`) also keeps an 8-bit code per vector (a per-vector minimum and scale plus one byte per dimension) and scores queries against the codes. The float vectors are still kept, so memory goes up by about a quarter, not down. A vector whose values are all equal has no range to quantize, and its score comes out as 0.
- **HNSW** (`build_hnsw_index`) builds an approximate nearest-neighbour graph. Queries take roughly constant time, but the build is slow and recall is low at the default settings (M=16, ef_construction=64, ef_search=32). Treat it as experimental.

```python
from cerberus_agent import VectorBuffer

vb = VectorBuffer(dim=4)
vb.push(embedding=[0.9, 0.1, 0.0, 0.0], text="chunk one")
vb.push(embedding=[0.0, 0.2, 0.9, 0.1], text="chunk two")
for m in vb.query(embedding=[1.0, 0.0, 0.0, 0.0], top_k=1):
    print(m.index, round(m.score, 2), m.text)
```

### KV-cache pages (`KVCacheArena`)

A pool of 4 KB pages backed by POSIX shared memory (`shm_open` plus `mmap`, with an anonymous mapping as fallback). A block is found by the 64-bit FNV-1a hash of a prompt prefix, and blocks are reference counted: allocating a prefix that already exists returns the same block and adds a reference, and the pages are freed when the last reference is released.

```python
from cerberus_agent import KVCacheArena

kv = KVCacheArena(capacity_pages=1024, shm_prefix="/cerberus_kv_")
block = kv.allocate_block(token_prefix="<system>Prompt</system>", min_bytes=4096)
print(block.block_id, block.page_count, block.shm_name)
kv.release_block(block.block_id)
```

This is the allocator only. Nothing connects it to llama.cpp, vLLM or any other runtime yet, so no attention cache is shared today.

## Choosing vector kernels at run time

`src/cpp_engine/core/Simd.hpp` picks the kernel once per process:

- x86-64: the AVX2 and FMA functions are always compiled, using a per-function target attribute. They are used only if `__builtin_cpu_supports` reports both features. The rest of the build stays at the plain x86-64 baseline, so the binary starts on older CPUs.
- 64-bit ARM: NEON is part of the architecture and is always used.
- Anything else, including 32-bit ARM: scalar code.

`CERBERUS_SIMD=scalar` forces the scalar path, and `cerberus_agent.simd_backend()` reports what is active. The test suite runs the vector tests both ways and compares the answers.

## Agent loop

`Agent.run_llm(task)` is the real loop. It puts the task in the context buffer, asks the model for a plan in the form `tool_name|arguments`, runs that tool, and puts the result in the context. There are two modes.

- **Default (`multi_step=False`).** The first successful tool call ends the run and its output is returned. If a tool fails, the error is added to the context and the model is asked again, up to `max_steps` times.
- **`multi_step=True`.** The model can call several tools one after another. Each call and result goes back into the conversation, and the model ends the run by calling `final_answer`, whose argument is returned. If the steps run out first, the last tool result is returned.

The model client is any object with `plan(task, tool_specs, context)` and `is_alive()`. `OllamaClient` is the one provided.

`Agent.run(task)` uses a fixed planner in the C++ engine that always chooses `shell|ls -la`, whatever the task. It exists for smoke tests.

Progress messages are off by default. `CERBERUS_VERBOSE=1` or `cerberus_agent.set_verbose(True)` turns them on. Errors always go to stderr.

## Other parts

- **Tool registry and dynamic registry.** Tools are plain callables taking a `ToolInput` and returning a `ToolOutput`. The dynamic registry can add, replace and remove tools while an agent runs, and can drop every tool that belongs to one server, which is how MCP servers are meant to plug in.
- **Output parser.** Classifies tool output as a JSON object, JSON array, scalar or plain text and pulls out the value, using views into arena memory.
- **Shared memory (`ca_shm_t`).** A key-value blackboard guarded by a read-write lock, so several agents can read at once.
- **Swarm orchestrator.** Runs sub-tasks on the C thread pool and waits for all of them, with an overall timeout.
- **Telemetry ring.** A single-producer, single-consumer ring of timing events. It can write JSON log lines in a simplified OpenTelemetry layout.

## Version history

- **1.0.0**: C11 arena, C++ agent loop, pybind11 bindings, Python wrapper.
- **1.1.0**: Semispace compaction in the context buffer, `memoryview` hand-off to Python, lazily started thread pool.
- **1.2.0**: Output parser, dynamic tool registry, shared memory with a read-write lock.
- **1.3.0**: Swarm orchestrator, SIMD vector memory, telemetry ring.
- **1.4.0**: KV-cache page pool, SQ8 codes, HNSW index.

Changes since then are in [CHANGELOG.md](CHANGELOG.md).

## Ideas, not started

Network transport between agents (`io_uring`), replicated agent state, sandboxed tool execution with a WebAssembly runtime, and a llama.cpp adapter for the KV-cache pages. None of these exist yet.

## License

MIT. See [LICENSE](LICENSE).
