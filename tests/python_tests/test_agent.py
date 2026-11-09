"""
tests/python_tests/test_agent.py
Python-layer tests for Cerberus
"""

import sys
import os
import pytest

# Add python/ to path
sys.path.insert(0, os.path.join(
    os.path.dirname(__file__), "../../python"
))

from cerberus_agent import Agent, ToolInput, ToolOutput


# ── Test 1: Agent Creation ────────────────────────────────
def test_agent_creation():
    agent = Agent(name="TestBot", num_threads=2)
    assert agent.name == "TestBot"
    assert agent.state_name == "IDLE"
    print(f"\n✅ Agent created: {agent}")


# ── Test 2: Default Tools Loaded ──────────────────────────
def test_default_tools():
    agent = Agent()
    names = agent.tool_names
    assert "shell"      in names
    assert "read_file"  in names
    assert "write_file" in names
    assert "echo"       in names
    print(f"\n✅ Default tools: {names}")


# ── Test 3: Custom Tool Registration ─────────────────────
def test_custom_tool():
    agent = Agent(load_defaults=False)

    def multiply(input: ToolInput) -> ToolOutput:
        out = ToolOutput()
        try:
            a, b    = map(int, input.args.split())
            out.result  = str(a * b)
            out.success = True
        except Exception as e:
            out.success = False
            out.error   = str(e)
        return out

    agent.add_tool("multiply",
                   "Multiply two numbers. args='a b'",
                   multiply)

    assert "multiply" in agent.tool_names

    result = agent.tools.dispatch(
        ToolInput()
    )

    # Direct dispatch test
    inp      = ToolInput()
    inp.name = "multiply"
    inp.args = "6 7"
    out = agent.tools.dispatch(inp)
    assert out.success
    assert out.result == "42"
    print(f"\n✅ multiply tool: 6 × 7 = {out.result}")


# ── Test 4: Decorator Registration ───────────────────────
def test_tool_decorator():
    agent = Agent(load_defaults=False)

    @agent.tool("greet", "Greet someone")
    def greet(input: ToolInput) -> ToolOutput:
        out         = ToolOutput()
        out.result  = f"Hello, {input.args}! 👋"
        out.success = True
        return out

    assert "greet" in agent.tool_names

    inp      = ToolInput()
    inp.name = "greet"
    inp.args = "World"
    out      = agent.tools.dispatch(inp)
    assert out.success
    assert "Hello, World" in out.result
    print(f"\n✅ Decorator tool: {out.result}")


# ── Test 5: Run a Task ────────────────────────────────────
def test_agent_run():
    agent  = Agent(name="RunBot", max_steps=3, num_threads=2)
    result = agent.run("list the files here")
    assert isinstance(result, str)
    assert len(result) > 0
    print(f"\n✅ Agent run result length: {len(result)} chars")


# ── Test 6: Context Buffer ────────────────────────────────
def test_context():
    agent = Agent(load_defaults=False)
    ctx   = agent.context

    ctx.add_user("Hello agent!")
    ctx.add_assistant("Hello! How can I help?")

    assert ctx.message_count() >= 2
    prompt = bytes(ctx.build_prompt()).decode("utf-8")
    assert "Hello agent!" in prompt
    print(f"\n✅ Context: {ctx.message_count()} messages")
    print(f"   Prompt preview: {prompt[:80]}...")


# ── Test 7: Agent repr ────────────────────────────────────
def test_agent_repr():
    agent = Agent(name="ReprBot")
    r     = repr(agent)
    assert "ReprBot"  in r
    assert "IDLE"     in r
    print(f"\n✅ repr: {r}")


# ── Main ──────────────────────────────────────────────────
if __name__ == "__main__":
    print("╔══════════════════════════════════════════╗")
    print("║  Cerberus Python Layer Tests             ║")
    print("╚══════════════════════════════════════════╝")

    test_agent_creation()
    test_default_tools()
    test_custom_tool()
    test_tool_decorator()
    test_agent_run()
    test_context()
    test_agent_repr()

    print("\n✅ All Python tests PASSED!\n")