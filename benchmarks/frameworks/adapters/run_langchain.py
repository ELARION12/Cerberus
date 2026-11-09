"""LangChain: the current create_agent helper with a ChatOllama model."""

import os
import sys
from importlib import metadata

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
import harness  # noqa: E402
import tasks  # noqa: E402

from langchain.agents import create_agent  # noqa: E402
from langchain_core.tools import StructuredTool  # noqa: E402
from langchain_ollama import ChatOllama  # noqa: E402


def build_tool(name, desc, style):
    fn = tasks.TOOL_FUNCTIONS[name]
    if style == "text":
        def run(text: str) -> str:
            return fn(text)
    else:
        def run(a: float, b: float) -> str:
            return fn(a, b)
    return StructuredTool.from_function(func=run, name=name, description=desc)


llm = ChatOllama(model=harness.MODEL, base_url=harness.PROXY, temperature=0, num_ctx=4096)
agent = create_agent(llm, [build_tool(n, d, s) for n, d, s, _ in tasks.TOOLS])


def ask(prompt):
    out = agent.invoke({"messages": [{"role": "user", "content": prompt}]},
                       config={"recursion_limit": 2 * harness.MAX_STEPS + 1})
    return out["messages"][-1].content


harness.run("langchain", ask, version="langchain " + metadata.version("langchain"))
