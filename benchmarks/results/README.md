# Results

Raw output and the tables built from it. Each file says in its own header which machine and model produced it.

| File | What it is |
|---|---|
| `frameworks-qwen2.5-1.5b-2026-11-09.txt` | The final comparison tables: Geryon (still called Cerberus when this was measured), a plain loop, LangChain, LlamaIndex and smolagents, three rounds. |
| `frameworks-qwen2.5-1.5b-2026-11-09-plain-cerberus-langchain-llamaindex.jsonl` | Raw per-task results for the first four. |
| `frameworks-qwen2.5-1.5b-2026-11-09-smolagents.jsonl` | Raw per-task results for smolagents. |

smolagents is in its own file because my first full run crashed on it: the harness mistook a line that smolagents printed for a result. The harness was fixed and smolagents was run again alone, a little after the other four, so its timings were taken under slightly different conditions. The partial rows from the crashed run were removed from the first file. `python benchmarks/frameworks/run.py --report-from <files>` rebuilds the tables from the raw files.

The project was called Cerberus when these were measured, so "cerberus" in the files means Geryon. The framework comparison was measured on the code that became version 1.5.0, when the package still reported 1.4.0, which is why the results say `cerberus-agent 1.4.0`.

Every run here is a single pass on a laptop with other programs open. See `benchmarks/frameworks/README.md` for what the numbers can and cannot support.
