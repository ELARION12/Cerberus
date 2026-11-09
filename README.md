![Cerberus](https://raw.githubusercontent.com/ELARION12/Cerberus/main/logo.png)

[![CI](https://github.com/ELARION12/Cerberus/actions/workflows/ci.yml/badge.svg)](https://github.com/ELARION12/Cerberus/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

# Cerberus

Cerberus is a small agent runtime written in C and C++ with a Python API. It keeps an agent's conversation context, tool registry and vector memory in native code, so the bookkeeping around a language model costs little time and memory. It is meant to run next to a local model (Ollama, llama.cpp) on small Linux machines.

It does not run models and it does not make a model faster. It is also early: the core pieces work and are tested, some parts are rough, and the [status section](#status) lists what is not done.

## Install

Linux only for now, on x86-64 or 64-bit ARM. You need Python 3.9 or newer and a C++17 compiler. `pip` fetches CMake itself if your system does not have a recent one.

```bash
git clone https://github.com/ELARION12/Cerberus.git
cd Cerberus
pip install .
```

This builds the native engine and installs it as `cerberus-agent` (import name `cerberus_agent`). It is not on PyPI yet. For MCP tool servers use `pip install ".[mcp]"`. There are no other required dependencies.

## Quickstart

This runs offline. A scripted planner stands in for the model so you can see the loop work.

```python
from cerberus_agent import Agent, ToolInput, ToolOutput, VectorBuffer


class ScriptedPlanner:
    """Answers the way a model would: 'tool_name|arguments'."""
    def plan(self, task, tool_specs, context):
        return "word_count|the quick brown fox"

    def is_alive(self):
        return True


agent = Agent(name="Demo", llm_client=ScriptedPlanner())


@agent.tool("word_count", "Count the words in the text")
def word_count(inp: ToolInput) -> ToolOutput:
    out = ToolOutput()
    out.result = str(len(inp.args.split()))
    out.success = True
    return out


print(agent.run_llm("How many words are in 'the quick brown fox'?"))   # 4

memory = VectorBuffer(dim=3)
memory.push([1.0, 0.0, 0.0], "cats purr")
memory.push([0.0, 1.0, 0.0], "dogs bark")
print(memory.query([0.9, 0.1, 0.0], top_k=1)[0].text)                   # cats purr
```

The same code is in [`examples/quickstart.py`](examples/quickstart.py). To use a real local model, swap the planner for `OllamaClient` as in [`examples/ollama_agent.py`](examples/ollama_agent.py).

The library prints nothing by default. Set `CERBERUS_VERBOSE=1` or call `cerberus_agent.set_verbose(True)` to see what the agent is doing.

## What is in it

| Part | What it does | State |
|---|---|---|
| Context buffer | Keeps a rolling conversation inside a token budget, in arena memory that is compacted instead of grown. | Works, tested, benchmarked below. |
| Tools | Register Python functions as tools, or use the built-in shell and file tools. The registry can add and remove tools while the agent runs, which is how MCP servers are plugged in (needs the `mcp` extra). | The registry is tested. The MCP bridge is not covered by tests yet. |
| Agent loop with a local model | `run_llm()` asks the model for a tool call, runs it, and returns the result. If the tool fails, the error goes back to the model for another try. | Works, but it is one tool call per task. It does not chain steps or write a final answer. |
| Vector memory | Brute-force cosine search over stored embeddings with AVX2 or NEON kernels, chosen at run time. | Works. |
| 8-bit vectors (SQ8) | Stores 8-bit codes next to the float vectors. | Does not save memory yet, see below. |
| HNSW index | Approximate nearest-neighbour graph. | Experimental: slow to build and low recall at the defaults. |
| KV-cache pages | A shared-memory page pool with lookup by prompt-prefix hash. | The allocator works. There is no llama.cpp or vLLM integration yet. |
| Swarm, telemetry, shared memory | Thread-pool fan-out of sub-tasks, a lock-free event ring that can write JSON log lines in a simplified OpenTelemetry format, a shared blackboard for agents. | Present, lightly tested. |

`Agent.run()` without a model is a placeholder: it always plans `ls -la`. Use `run_llm()`.

## Numbers

Measured on an Intel Core i5-10310U laptop (8 threads), Python 3.12, Linux. Run them yourself with the scripts in [`benchmarks/`](benchmarks/). Results move by about 30% between runs on this laptop.

**Context handling** (`benchmarks/context_loop.py`). 50,000 iterations, each adding a user and an assistant message, trimming to a 16,384 character budget, and rendering the prompt every 10th time. Median of 3 runs, each in its own process.

| | iterations/sec | memory used by `import` |
|---|---|---|
| Cerberus `ContextBuffer` | about 870,000 | about 9 MB |
| Plain Python `deque` | about 185,000 | none |
| `langchain_core` message objects | about 75,000 | about 27 MB |

That is the cost of keeping the conversation, not of running an agent. Model calls take milliseconds to seconds each, so this part is small either way. It matters when many agents share one small machine, or when the machine has little memory.

**Vector search** (`benchmarks/vector_query.py`). 5,000 clustered random vectors, 384 dimensions, top 5. The data is not real embeddings.

| | ms per query | index size | top-5 matches exact |
|---|---|---|---|
| Cerberus, scalar code | 1.95 | 7.8 MB | 100% |
| Cerberus, AVX2 | 0.51 | 7.8 MB | 100% |
| Cerberus, SQ8 | 0.51 | 9.8 MB | 98% |
| Cerberus, HNSW | 0.11 | 8.3 MB | 84% |
| numpy matrix product (reference) | 0.3 to 0.5 | 7.7 MB | 100% |

AVX2 is about 4 times faster than the scalar path. A plain numpy matrix product is about as fast as Cerberus, so the point here is not speed over numpy. It is doing the search inside the agent process with no numpy installed. HNSW answers in 0.1 ms but took 18 seconds to build and, at 20,000 vectors, found only about half of the true top 5. Do not rely on it yet.

**Native code** (`bench_native`, build with `-DCERBERUS_BUILD_BENCHMARKS=ON`). One arena allocation takes about 2 ns, and adding a 100 character message to the context buffer about 75 ns.

**Not measured yet:** end to end with a real model. `benchmarks/agent_e2e.py` runs the same tasks through `run_llm()` and through a minimal Python loop against an Ollama model, with identical prompts, and reports success rate, time and memory. Results will go here once it has run.

I have not run these on a Raspberry Pi yet. The ARM64 build is compiled and tested in CI, and Pi numbers will be added here when I have them.

## How it is built

```
Python API            python/cerberus_agent      agent loop, tools, Ollama client, MCP bridge
C++17 engine          src/cpp_engine             context buffer, vector memory, tool registry, swarm
C11 core              src/c_core                 arena allocator, thread pool, process executor, event ring
```

The C core has no dependencies beyond the C library and pthreads. The C++ engine is mostly header-only. pybind11 joins the engine to Python as `_cerberus_core`.

The build does not use `-march=native`, so a binary runs on any CPU of its architecture. The AVX2 kernels (x86-64) are compiled in and used only if the CPU reports AVX2 and FMA. On 64-bit ARM the NEON kernels are always used. `cerberus_agent.simd_backend()` says which one is active, and `CERBERUS_SIMD=scalar` forces the plain path.

## Development

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

ctest --test-dir build                       # C and C++ tests
PYTHONPATH=python pytest tests/python_tests  # Python tests
python benchmarks/context_loop.py
python benchmarks/vector_query.py
```

The build copies the native module into `python/cerberus_agent/`, so the Python tests run without installing. Debug builds turn on AddressSanitizer. Add `-DCERBERUS_NATIVE_ARCH=ON` to tune for your own CPU, and never ship a build made that way. See [CONTRIBUTING.md](CONTRIBUTING.md) for more.

## Status

Known problems:

- SQ8 keeps the float vectors as well as the 8-bit codes, so it uses 25% more memory than plain float32 instead of a quarter of it.
- HNSW is slow to build and has low recall at its defaults.
- The prompt the context buffer renders has no role labels and does not include tool names.
- `run_llm()` makes a single tool call per task.
- The NEON code has been compiled and is tested in CI on ARM64, but has not run on real hardware by me yet.
- Not published to PyPI.

Ideas, none started: a llama.cpp adapter for the KV-cache pages, an OpenAI-compatible client, network transport between agents, and sandboxed tool execution.

See [CHANGELOG.md](CHANGELOG.md) for what changed in each version and [DOCUMENTATION.md](DOCUMENTATION.md) for the design notes.

## License

MIT. See [LICENSE](LICENSE).
