"""LlamaIndex: FunctionAgent with an Ollama model."""

import asyncio
import os
import sys
from importlib import metadata

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
import harness  # noqa: E402
import tasks  # noqa: E402

from llama_index.core.agent.workflow import FunctionAgent  # noqa: E402
from llama_index.core.tools import FunctionTool  # noqa: E402
from llama_index.llms.ollama import Ollama  # noqa: E402


def build_tool(name, desc, style):
    fn = tasks.TOOL_FUNCTIONS[name]
    if style == "text":
        def run(text: str) -> str:
            return fn(text)
    else:
        def run(a: float, b: float) -> str:
            return fn(a, b)
    return FunctionTool.from_defaults(fn=run, name=name, description=desc)


llm = Ollama(model=harness.MODEL, base_url=harness.PROXY, request_timeout=240.0,
             context_window=4096, temperature=0)
agent = FunctionAgent(tools=[build_tool(n, d, s) for n, d, s, _ in tasks.TOOLS], llm=llm)


def ask(prompt):
    async def go():
        return await agent.run(user_msg=prompt, max_iterations=harness.MAX_STEPS)
    return str(asyncio.run(go()))


harness.run("llamaindex", ask, version="llama-index-core " + metadata.version("llama-index-core"))
