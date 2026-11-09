"""
run_llm() has to hand the model the real conversation so far. It used to pass a
memoryview that was formatted as '<memory at 0x...>', so retries and shared
team memory never reached the model.
"""

import json

import pytest

import cerberus_agent as ca
from cerberus_agent import Agent, ToolOutput
from cerberus_agent.llm import OllamaClient

pytestmark = pytest.mark.skipif(
    not ca._BACKEND_AVAILABLE, reason="native module is not built"
)


class RecordingPlanner:
    def __init__(self, replies):
        self.replies = list(replies)
        self.contexts = []

    def plan(self, task, tool_specs, context):
        self.contexts.append(context)
        return self.replies.pop(0)

    def is_alive(self):
        return True


def flaky_agent(planner):
    agent = Agent(name="T", num_threads=1, llm_client=planner)
    calls = {"n": 0}

    @agent.tool("flaky", "fails on the first call")
    def flaky(inp):
        calls["n"] += 1
        out = ToolOutput()
        if calls["n"] == 1:
            out.success, out.error = False, "disk is full"
        else:
            out.success, out.result = True, "done"
        return out

    return agent


def test_planner_receives_text_not_a_memoryview():
    planner = RecordingPlanner(["flaky|a", "flaky|b"])
    flaky_agent(planner).run_llm("do the thing")
    assert all(isinstance(c, str) for c in planner.contexts)
    assert "memory at 0x" not in planner.contexts[-1]


def test_error_from_a_failed_tool_reaches_the_planner_on_retry():
    planner = RecordingPlanner(["flaky|a", "flaky|b"])
    result = flaky_agent(planner).run_llm("do the thing")
    assert result == "done"
    assert "disk is full" in planner.contexts[1]


def test_ollama_prompt_contains_the_error_text(monkeypatch):
    sent = []

    def fake_generate(self, prompt, system=None, temperature=0.2, format=None):
        sent.append(prompt)
        return type("R", (), {"text": json.dumps({"tool": "flaky", "args": "x"})})()

    monkeypatch.setattr(OllamaClient, "generate", fake_generate)
    flaky_agent(OllamaClient()).run_llm("do the thing")
    assert len(sent) == 2
    assert "disk is full" in sent[1]
    assert "memory at 0x" not in sent[1]


def test_planner_is_told_what_a_custom_tool_does():
    seen = {}

    class Planner:
        def plan(self, task, tool_specs, context):
            seen.update(tool_specs)
            return "word_count|a b"

        def is_alive(self):
            return True

    agent = Agent(name="T", num_threads=1, llm_client=Planner())

    @agent.tool("word_count", "Count the words in the text")
    def word_count(inp):
        out = ToolOutput()
        out.success, out.result = True, "2"
        return out

    agent.run_llm("how many words?")
    assert seen["word_count"] == "Count the words in the text"
