"""
run_llm() has to hand the model the real conversation so far. It used to pass a
memoryview that was formatted as '<memory at 0x...>', so retries and shared
team memory never reached the model.
"""

import json

import pytest

import geryon as ca
from geryon import Agent, ToolOutput
from geryon.llm import OllamaClient

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


def test_prompt_labels_each_line_with_its_role_and_tool_name():
    from geryon._geryon_core import ContextBuffer
    ctx = ContextBuffer(65536, 65536, 4096)
    ctx.add_system("be brief")
    ctx.add_user("count the words")
    ctx.add_assistant("call word_count(a b)")
    ctx.add_tool_result("word_count", "2")
    assert bytes(ctx.build_prompt()).decode() == (
        "system: be brief\n"
        "user: count the words\n"
        "assistant: call word_count(a b)\n"
        "tool word_count: 2\n"
    )


def counting_agent(planner):
    agent = Agent(name="M", num_threads=1, llm_client=planner)

    @agent.tool("add_one", "Add one to a number. args = the number")
    def add_one(inp):
        out = ToolOutput()
        out.success, out.result = True, str(int(inp.args) + 1)
        return out

    return agent


def test_multi_step_chains_tools_until_final_answer():
    planner = RecordingPlanner(["add_one|1", "add_one|2", "final_answer|the answer is 3"])
    answer = counting_agent(planner).run_llm("add one twice to 1", multi_step=True)
    assert answer == "the answer is 3"
    assert len(planner.contexts) == 3
    # the third plan call sees both earlier calls and their results, labelled
    last = planner.contexts[2]
    assert "assistant: call add_one(1)" in last
    assert "tool add_one: 2" in last
    assert "tool add_one: 3" in last


def test_multi_step_offers_final_answer_but_single_step_does_not():
    offered = []

    class Planner:
        def plan(self, task, tool_specs, context):
            offered.append(sorted(tool_specs))
            return "final_answer|x" if "final_answer" in tool_specs else "add_one|1"

        def is_alive(self):
            return True

    counting_agent(Planner()).run_llm("t", multi_step=True)
    counting_agent(Planner()).run_llm("t")
    assert "final_answer" in offered[0]
    assert "final_answer" not in offered[1]


def test_multi_step_returns_the_last_result_when_steps_run_out():
    planner = RecordingPlanner(["add_one|1", "add_one|2", "add_one|3"])
    answer = counting_agent(planner).run_llm("keep going", max_steps=3, multi_step=True)
    assert answer == "4"


def test_multi_step_recovers_from_a_failing_tool():
    planner = RecordingPlanner(["flaky|a", "flaky|b", "final_answer|recovered"])
    answer = flaky_agent(planner).run_llm("do it", multi_step=True)
    assert answer == "recovered"
    assert "disk is full" in planner.contexts[1]


def test_single_step_behaviour_is_unchanged():
    planner = RecordingPlanner(["add_one|1"])
    assert counting_agent(planner).run_llm("add one to 1") == "2"
    assert len(planner.contexts) == 1


def test_multi_step_stops_when_the_model_repeats_a_finished_call():
    planner = RecordingPlanner(["add_one|1", "add_one|1", "add_one|1", "add_one|1"])
    answer = counting_agent(planner).run_llm("add one to 1", max_steps=6, multi_step=True)
    assert answer == "2"
    assert len(planner.contexts) == 2          # it did not burn all the steps
