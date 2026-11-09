# Framework comparison

Runs the same tasks through Cerberus and other agent frameworks, on the same local model, and reports a score for each of three rounds (easy, medium, hard) plus startup time, memory and the framework's own overhead.

## Run it

```bash
pip install uv
bash benchmarks/frameworks/setup_envs.sh       # one virtual environment per framework, in ~/bench-envs
ollama pull qwen2.5:1.5b                       # needs a running Ollama server
python benchmarks/frameworks/run.py --model qwen2.5:1.5b
```

CPU only is fine. Use `--limit 1` for a quick check that everything is wired up, `--frameworks plain cerberus` to run a subset, and `--levels easy` for one round.

## What is compared

| Name | What runs |
|---|---|
| `plain` | A minimal loop over Ollama's native tool calling. A control, not a competitor. |
| `cerberus` | `Agent.run_llm(multi_step=True)` from the installed wheel. |
| `langchain` | `langchain.agents.create_agent` with `ChatOllama`. |
| `llamaindex` | `FunctionAgent` with the Ollama LLM. |
| `smolagents` | `ToolCallingAgent` through Ollama's OpenAI-compatible endpoint. |

Each framework is used the way its own documentation shows, with its stock prompts. Versions are recorded in the results and in `~/bench-envs/<name>/freeze.txt`.

## The tasks

22 tasks over six tools (word count, add, multiply, reverse text, uppercase, text length). Easy is one tool call. Medium needs two calls where the second uses the first result. Hard needs three or four. An answer is correct when the expected value appears in it as whole words. The expected values are computed from the tools in `common/tasks.py`, which checks itself on every run.

## What is measured

- **Score** per round: correct answers over tasks.
- **s/task**: wall time per task, including the model.
- **Overhead ms**: wall time minus the time spent waiting for the model. The model time is measured by a proxy that every framework talks through, so it is counted the same way for all of them. What is left is the framework's own work.
- **Model calls, tool calls**: how many times the model was asked, and how many tools actually ran.
- **RSS MB**: memory of the agent process, at the start and at the end of each round. The model runs in the Ollama server and is the same for everyone.
- **Cold start**: from process start until the agent is built and ready to take its first task.

The proxy also pins temperature 0, a fixed seed, a 4096 token context and a 400 token output cap, so every framework gets the same model settings instead of its own defaults.

## What these results can and cannot tell you

Read them with these limits in mind.

- **Startup time, memory and overhead are the solid part.** They are large, mechanical differences (imports, loading, per-step work) that repeat from run to run. They are reasonable to quote with the machine, model and framework versions next to them.
- **Scores are not a ranking.** There are 22 tasks and only 6 in the hard round, so one task moves a score by about 17 points. One model, one machine, one run. Treat a gap of a task or two as noise.
- **Total time depends on the number of model calls.** Frameworks that can ask for several tools in one reply need fewer calls than one that asks for one tool per turn, and on a CPU each call costs seconds. That is a real difference in design, but it is not the same as one framework being faster code.
- **Cerberus was tuned on these tasks.** Its multi-step loop, its repeat guard and the wording of its planner prompt were developed while looking at several of these tasks. The other frameworks run with their stock settings. This favours Cerberus. A fair accuracy comparison needs tasks that were not used during development.
- **The adapters were all written by one person.** If you maintain one of these frameworks and think the setup is not how it should be used, please open an issue or a pull request.
- **The tasks are toy tools.** They say nothing about real agent work such as files, the web, long conversations or messy tool output.
- **A busy laptop is a noisy machine.** CPU speed moves with temperature and other programs. Timing differences under about 20% should not be read as real.

## Adding a framework

Copy `adapters/run_plain.py`, build that framework's agent, and give `harness.run` a function that takes a prompt and returns the final answer text. Add the framework to `setup_envs.sh` and to `ALL` in `run.py`. Name the file `run_<name>.py`, never the name of the package itself, or Python will import your file instead of the library.
