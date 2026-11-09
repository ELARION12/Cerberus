"""
Run an agent with a local model served by Ollama.

Needs a running Ollama server and a pulled model:
    ollama serve
    ollama pull qwen2.5:0.5b

The model picks one tool for the task, the engine runs it and returns the
result. If the tool fails, the error goes back to the model for another try.
"""

import sys

from geryon import Agent, set_verbose
from geryon.llm import OllamaClient


def main():
    set_verbose(True)   # show the planner's choices as it works

    client = OllamaClient(host="http://localhost:11434", model="qwen2.5:0.5b")
    if not client.is_alive():
        print("No Ollama server on http://localhost:11434. Start one with: ollama serve")
        return 1

    agent = Agent(name="OllamaAgent", llm_client=client)
    task = "List all markdown files in the current directory."
    print(f"Task: {task}")
    print("Result:")
    print(agent.run_llm(task))
    return 0


if __name__ == "__main__":
    sys.exit(main())
