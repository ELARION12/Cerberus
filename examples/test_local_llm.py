#!/usr/bin/env python3
"""
Example: Running Cerberus AI with a Local LLM (Ollama / Qwen 2.5 / Llama 3)
"""

import sys
import os

# Add python directory to module path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "../python"))

from cerberus import Agent
from cerberus.llm import OllamaClient

def main():
    print("🐕 Initializing Cerberus AI connected to Local Ollama...")

    # 1. Initialize Ollama Client (default: http://localhost:11434, model="qwen2.5:0.5b")
    client = OllamaClient(
        host="http://localhost:11434",
        model="qwen2.5:0.5b" # Change to any pulled model e.g. "llama3.2", "mistral"
    )

    # 2. Check if local LLM server is reachable
    if not client.is_alive():
        print("⚠️ Local Ollama server is not running on http://localhost:11434!")
        print("Please start it in your terminal with:  ollama serve")
        print("And pull a model with:                 ollama pull qwen2.5:0.5b")
        return

    print("✅ Local Ollama server detected successfully!")

    # 3. Create Agent instance with LLM client
    agent = Agent(name="CerberusAgent", llm_client=client)

    # 4. Connect Filesystem MCP tools if desired
    agent.connect_filesystem_mcp()

    # 5. Run a task using the local LLM reasoning engine
    task = "List all markdown files in the current workspace directory."
    print(f"\n🧠 Running task with local LLM: '{task}'...")

    result = agent.run_llm(task)

    print("\n✅ Execution Result:")
    print(result)

if __name__ == "__main__":
    main()
