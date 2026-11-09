<p align="center"><img src="https://raw.githubusercontent.com/ELARION12/Geryon/main/assets/geryon-banner.png" alt="Geryon, a lightweight agent framework for local LLMs on small devices" width="100%"></p>

A lightweight agent framework for running tool-calling agents on local language models. A C11 core and a C++17 engine handle the conversation memory, tool dispatch and vector search, and a Python API sits on top. It is aimed at small Linux machines, such as a Raspberry Pi or a mini PC, where start-up time and memory matter.

Formerly called Cerberus (up to version 1.5.0). It was renamed because that name is used by several well-known projects.

Latest release: v1.6.0. Build status: https://github.com/ELARION12/Geryon/actions

## What it is, and what it is not

- **It is a framework.** It is a library you build agents with. You give it a model client and some tools, and it runs the loop in which the model picks a tool, the tool runs, and the result goes back into the conversation.
- **It orchestrates one model and its tools.** It also has a thread-pool "swarm" for fanning sub-tasks out to several agents, and a shared board for them to read and write, but those parts are lightly tested.
- **It is not a model runner.** The model runs somewhere else. The client that comes with Geryon talks to Ollama, and any object with a `plan()` method can stand in for it. A llama.cpp client does not exist yet.
- **It is not a replacement for the big frameworks.** There is no ecosystem of integrations, and only a few tools are built in. What it offers is a much smaller footprint, measured below.

## Install

Linux only for now, on x86-64 or 64-bit ARM. You need Python 3.9 or newer and a C++17 compiler. `pip` fetches CMake itself if your system does not have a recent one.

```bash
git clone https://github.com/ELARION12/Geryon.git
cd Geryon
pip install .
```

This builds the native engine and installs it as `geryon` (import name `geryon`). It is not on PyPI yet. For MCP tool servers use `pip install ".[mcp]"`. There are no other required dependencies.

## Quickstart

This runs offline. A scripted planner stands in for the model so you can see the loop work.

```python
from geryon import Agent, ToolInput, ToolOutput, VectorBuffer


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

The same code is in [`examples/quickstart.py`](examples/quickstart.py). To use a real local model, swap the planner for `OllamaClient` as in [`examples/ollama_agent.py`](examples/ollama_agent.py). For a task that needs several tools in a row, call `agent.run_llm(task, multi_step=True)`.

The library prints nothing by default. Set `GERYON_VERBOSE=1` or call `geryon.set_verbose(True)` to see what the agent is doing.

## What is in it

|               Part              | What it does | State |
|:-------------------------------:|:---:|:---:|
|          Context buffer         | Keeps a rolling conversation inside a token budget, in arena memory that is compacted instead of grown. Each line is labelled with its role. | Works, tested, benchmarked below. |
|              Tools              | Register Python functions as tools, or use the built-in shell and file tools. The registry can add and remove tools while the agent runs, which is how MCP servers are plugged in (needs the `mcp` extra). | The registry is tested. The MCP bridge is not covered by tests yet. |
|  Agent loop with a local model  | `run_llm()` asks the model for a tool call, runs it, and returns the result. If the tool fails, the error goes back to the model for another try. With `multi_step=True` the model can chain several tools and finish with `final_answer`. | Works. The default is one tool call per task. |
|          Vector memory          | Brute-force cosine search over stored embeddings with AVX2 or NEON kernels, chosen at run time. | Works. |
|       8-bit vectors (SQ8)       | Stores 8-bit codes next to the float vectors. | Does not save memory yet, see Status. |
|            HNSW index           | Approximate nearest-neighbour graph. | Experimental: slow to build and low recall at the defaults. |
|          KV-cache pages         | A shared-memory page pool with lookup by prompt-prefix hash. | The allocator works. There is no llama.cpp or vLLM integration. |
| Swarm, telemetry, shared memory | Thread-pool fan-out of sub-tasks, a lock-free event ring that can write JSON log lines in a simplified OpenTelemetry format, a shared blackboard for agents. | Present, lightly tested. |

`Agent.run()` without a model is a placeholder: it always plans `ls -la`. Use `run_llm()`.

## Releases

Details are in [CHANGELOG.md](CHANGELOG.md) and on the [Releases page](https://github.com/ELARION12/Geryon/releases).

| Version |    Date    | What it added |
|:-------:|:----------:|:---:|
|  1.6.0  | 2026-11-09 | Renamed from Cerberus to Geryon. The Python package, the import name, the command line tool and the environment variables are now `geryon` and `GERYON_*`. No other changes. |
|  1.5.0  | 2026-11-09 | Released as Cerberus. Installable with `pip install .`. CI on x86-64 and ARM64. AVX2 or NEON chosen at run time, so a build runs on any CPU of its architecture. Multi-step `run_llm()`. Role-labelled prompts. Quiet by default. Benchmarks that run, including a comparison with other frameworks. Corrected documentation. The Python package became `cerberus_agent`. |
|  1.4.0  | 2026-07-31 | KV-cache page pool, SQ8 codes, HNSW index. |
|  1.3.0  | 2026-07-29 | Swarm orchestrator, SIMD vector memory, telemetry ring. |
|  1.2.0  | 2026-07-25 | Output parser, dynamic tool registry, shared memory with a read-write lock. |
|  1.1.0  | 2026-07-21 | Semispace compaction in the context buffer, zero-copy hand-off to Python, lazily started thread pool. |
|  1.0.0  | 2026-07-18 | C11 arena allocator, C++ agent loop, pybind11 bindings, Python wrapper. |

The notes for 1.2.0 to 1.4.0 contained claims that did not hold up, such as a 4x memory saving from SQ8 and llama.cpp cache sharing. They now start with a correction.

## Numbers

Measured on an Intel Core i5-10310U laptop (8 threads, 11 GB RAM, no GPU), Python 3.12, Linux. Run them yourself with the scripts in [`benchmarks/`](benchmarks/). Results move by about 20 to 30% between runs on this laptop, and every table below is a single pass. They were measured with version 1.5.0, when the project was still called Cerberus, and the saved result files use that name. The code that was measured has not changed apart from the rename.

### Against other agent frameworks

[`benchmarks/frameworks/`](benchmarks/frameworks/) runs the same tasks through Geryon and four others: a plain Python loop (a control), LangChain, LlamaIndex and smolagents. It uses one local model (`qwen2.5:1.5b` through Ollama, CPU only, temperature 0) and 22 tasks in three rounds. Each framework runs in its own environment and is used the way its documentation shows. A proxy times the model for all of them, so "overhead" is the time a task takes outside the model. Raw output is in [`benchmarks/results/`](benchmarks/results/).

What a framework costs you, over all 22 tasks:

|                             |  start-up  | memory (peak) | overhead per task | model calls per task | total time, 22 tasks |
|:---------------------------:|:----------:|:-------------:|:-----------------:|:--------------------:|:--------------------:|
| plain Python loop (control) |   0.05 s   |     22 MB     |       1.7 ms      |         2.0          |         95 s         |
|          **Geryon**         | **0.09 s** |   **28 MB**   |     **2.6 ms**    |         3.0          |        104 s         |
|       LangChain 1.4.9       |   0.91 s   |     82 MB     |       12 ms       |         2.0          |         96 s         |
|      LlamaIndex 0.14.25     |   1.34 s   |     163 MB    |       33 ms       |         2.0          |         97 s         |
|      smolagents 1.26.0      |   1.01 s   |     79 MB     |       16 ms       |         3.4          |        269 s         |

Against LangChain, LlamaIndex and smolagents, Geryon starts 10 to 15 times faster, peaks at about a third to a sixth of their memory, and does 5 to 13 times less work per task of its own. Total time is almost the same for all but smolagents, because the model takes nearly all of it. Geryon does not make an agent finish faster.

Score per round, correct answers out of the tasks:

|                   | easy (8) | medium (8) | hard (6) | total (22) |
|:-----------------:|:--------:|:----------:|:--------:|:----------:|
| plain Python loop |    8     |     4      |    2     |     14     |
|     **Geryon**    |    7     |     4      |    2     |     13     |
|     LangChain     |    8     |     6      |    2     |     16     |
|     LlamaIndex    |    8     |     6      |    2     |     16     |
|     smolagents    |    8     |     5      |    1     |     14     |

These scores are not a ranking. There are 22 tasks and the hard round has 6, so one task moves a score by about 17 points, and a gap of one or two tasks is noise. Geryon was tuned on these tasks while it was being built and the others ran with their stock settings, which favours Geryon. It is a little behind on the medium round. It asks the model for one tool per turn, so it needs more model calls (2.9 per medium task against 2.0), and a small model chains less reliably that way. The limits of the comparison are written down in [`benchmarks/frameworks/README.md`](benchmarks/frameworks/README.md).

### Context handling

[`benchmarks/context_loop.py`](benchmarks/context_loop.py): 50,000 iterations, each adding a user and an assistant message, trimming to a 16,384 character budget, and rendering the prompt every 10th time. Median of 3 runs, each in its own process.

|                                  | iterations/sec | memory used by `import` |
|:--------------------------------:|:--------------:|:-----------------------:|
|      Geryon `ContextBuffer`      | about 730,000  |       about 11 MB       |
|       Plain Python `deque`       | about 165,000  |           none          |
| `langchain_core` message objects |  about 70,000  |       about 28 MB       |

This is the cost of keeping the conversation, not of running an agent. Model calls take milliseconds to seconds each, so this part is small either way.

### Vector search

[`benchmarks/vector_query.py`](benchmarks/vector_query.py): 5,000 clustered random vectors, 384 dimensions, top 5. The data is not real embeddings.

|                                  | ms per query | index size | top-5 matches exact |
|:--------------------------------:|:------------:|:----------:|:-------------------:|
|       Geryon, scalar code        |     1.9      |   7.8 MB   |         100%        |
|           Geryon, AVX2           |     0.46     |   7.8 MB   |         100%        |
|           Geryon, SQ8            |     0.50     |   9.8 MB   |         98%         |
|           Geryon, HNSW           |     0.12     |   8.3 MB   |         84%         |
| numpy matrix product (reference) | 0.25 to 0.5  |   7.7 MB   |         100%        |

AVX2 is about 4 times faster than the scalar path. numpy's matrix product is as fast as Geryon or faster, so search speed is not an advantage over numpy. What Geryon offers is search inside the agent process without installing numpy. HNSW answers in 0.1 ms but took 17 seconds to build and, at 20,000 vectors, found only about half of the true top 5. Do not rely on it yet.

### Native code

`bench_native`, built with `-DGERYON_BUILD_BENCHMARKS=ON`: one arena allocation takes 2 to 3 ns, and adding a 100 character message to the context buffer about 70 to 80 ns.

### Not measured yet

Anything on a Raspberry Pi, and long-run memory growth. Pi results will be added here when they exist.

## How it is built

```
Python API      python/geryon     agent loop, tools, Ollama client, MCP bridge
C++17 engine    src/cpp_engine    context buffer, vector memory, tool registry, swarm
C11 core        src/c_core        arena allocator, thread pool, process executor, event ring
```

The C core has no dependencies beyond the C library and pthreads. The C++ engine is mostly header-only. pybind11 joins the engine to Python as `_geryon_core`.

The build does not use `-march=native`, so a binary runs on any CPU of its architecture. The AVX2 kernels (x86-64) are compiled in and used only if the CPU reports AVX2 and FMA. On 64-bit ARM the NEON kernels are always used. `geryon.simd_backend()` says which one is active, and `GERYON_SIMD=scalar` forces the plain path.

CI builds and tests it on every push to `main` and every pull request, on x86-64 and on 64-bit ARM. The ARM job fails unless the NEON kernels are the ones running, and they have run there on GitHub's ARM64 machines. They have not run on a Raspberry Pi yet.

## Development

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

ctest --test-dir build                       # C and C++ tests
PYTHONPATH=python pytest tests/python_tests  # Python tests
python benchmarks/context_loop.py
python benchmarks/vector_query.py
```

The build copies the native module into `python/geryon/`, so the Python tests run without installing. Debug builds turn on AddressSanitizer. Add `-DGERYON_NATIVE_ARCH=ON` to tune for your own CPU, and never ship a build made that way. See [CONTRIBUTING.md](CONTRIBUTING.md) for more.

## Status

Known problems:

- SQ8 keeps the float vectors as well as the 8-bit codes, so it uses 25% more memory than plain float32 instead of a quarter of it.
- HNSW is slow to build and has low recall at its defaults.
- Geryon asks the model for one tool per turn. Frameworks that use the model's native tool calling can ask for several at once, so they need fewer model calls on multi-step tasks. Using native tool calling in Geryon is the next thing to try.
- The built-in tools run with your permissions and are not sandboxed. `shell` runs whatever command the model picks, `read_file` and `write_file` reach any path you can, and `py_http_get` fetches any http or https address. Remove the ones you do not want, for example `agent.tools.remove_tool("shell")`.
- The NEON code has run in CI on ARM64 but not on a Raspberry Pi.
- Linux only. Not published to PyPI.

Ideas, none started: a memory budget setting that sizes an agent's arenas from one number, a llama.cpp adapter for the KV-cache pages, native tool calling, a Raspberry Pi benchmark page, and sandboxed tool execution.

## License

MIT. See [LICENSE](LICENSE).
