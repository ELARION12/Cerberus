"""smolagents: ToolCallingAgent through Ollama's OpenAI-compatible endpoint."""

import os
import sys
from importlib import metadata

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
import harness  # noqa: E402
import tasks  # noqa: E402

from smolagents import OpenAIServerModel, Tool, ToolCallingAgent  # noqa: E402


def build_tool(name, desc, style):
    fn = tasks.TOOL_FUNCTIONS[name]
    if style == "text":
        inputs = {"text": {"type": "string", "description": "the text"}}

        def forward(self, text):
            return fn(text)
    else:
        inputs = {"a": {"type": "number", "description": "first number"},
                  "b": {"type": "number", "description": "second number"}}

        def forward(self, a, b):
            return fn(a, b)
    cls = type(name, (Tool,), {"name": name, "description": desc, "inputs": inputs,
                               "output_type": "string", "forward": forward})
    return cls()


model = OpenAIServerModel(model_id=harness.MODEL, api_base=harness.PROXY + "/v1", api_key="ollama")
agent = ToolCallingAgent(tools=[build_tool(n, d, s) for n, d, s, _ in tasks.TOOLS], model=model,
                         max_steps=harness.MAX_STEPS, verbosity_level=0)


def ask(prompt):
    return agent.run(prompt, reset=True)


harness.run("smolagents", ask, version="smolagents " + metadata.version("smolagents"))
