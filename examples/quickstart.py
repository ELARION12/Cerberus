"""
Offline quickstart: no model and no network needed.

A scripted planner stands in for the language model so you can watch the
loop run: the planner picks a tool, the engine runs it, the result comes back.
To use a real local model, replace ScriptedPlanner() with
OllamaClient(model="qwen2.5:0.5b") (see ollama_agent.py).
"""

from cerberus_agent import Agent, ToolInput, ToolOutput, VectorBuffer, simd_backend


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


print("tools   :", ", ".join(agent.tool_names))
print("answer  :", agent.run_llm("How many words are in 'the quick brown fox'?"))

# Vector memory: store embeddings with a text payload, ask for the closest one.
# Real embeddings come from a model; three numbers are enough to see it work.
memory = VectorBuffer(dim=3)
memory.push([1.0, 0.0, 0.0], "cats purr")
memory.push([0.0, 1.0, 0.0], "dogs bark")
print("closest :", memory.query([0.9, 0.1, 0.0], top_k=1)[0].text)

print("kernel  :", simd_backend())
