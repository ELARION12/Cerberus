"""Geryon: Agent.run_llm(multi_step=True) with a local model."""

import os
import sys
from importlib import metadata

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
import harness  # noqa: E402
import tasks  # noqa: E402

from geryon import Agent, ToolOutput  # noqa: E402
from geryon.llm import OllamaClient  # noqa: E402

SYSTEM = "You are a fast, efficient AI agent."
client = OllamaClient(model=harness.MODEL, host=harness.PROXY, timeout=240)
agent = Agent(name="Bench", num_threads=1, load_defaults=False, llm_client=client, max_steps=harness.MAX_STEPS)

# the engine ships four built-in tools; offer only the benchmark's six
for builtin in ("shell", "read_file", "write_file", "echo"):
    agent.tools.remove_tool(builtin)


def make_tool(name):
    def tool(inp):
        out = ToolOutput()
        try:
            out.result = tasks.run_tool_from_args(name, inp.args)
            out.success = True
        except Exception as e:
            out.success, out.error = False, str(e)
        return out
    return tool


for name, desc, style, _ in tasks.TOOLS:
    hint = "args = the text" if style == "text" else "args = the two numbers separated by a space"
    agent.add_tool(name, f"{desc} {hint}", make_tool(name))


def ask(prompt):
    agent.context.clear()                      # every task starts a fresh conversation
    agent.context.add_system(SYSTEM)
    return agent.run_llm(prompt, max_steps=harness.MAX_STEPS, multi_step=True)


harness.run("geryon", ask, version="geryon " + metadata.version("geryon"))
