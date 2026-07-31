#!/usr/bin/env python3
"""
IronAgent Custom Prompt Runner
==============================
Reads your custom prompt from 'prompt.txt', executes it through IronAgent
and your local LLM, and outputs the result cleanly.
"""

import sys
import os

# Add python directory to module path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "python"))

from coreagent import Agent
from coreagent.llm import OllamaClient

PROMPT_FILE = "prompt.txt"
RESPONSE_FILE = "response.txt"

def main():
    if not os.path.exists(PROMPT_FILE):
        print(f"❌ Error: {PROMPT_FILE} not found!")
        print(f"Please create '{PROMPT_FILE}' and type your prompt inside it.")
        return

    with open(PROMPT_FILE, "r", encoding="utf-8") as f:
        prompt_text = f.read().strip()

    if not prompt_text:
        print(f"⚠️ {PROMPT_FILE} is empty! Please paste your prompt into {PROMPT_FILE}.")
        return

    print("=" * 60)
    print("🤖 IronAgent Custom Prompt Executor")
    print("=" * 60)
    print(f"📄 Loaded Prompt from '{PROMPT_FILE}':")
    print(f"   \"{prompt_text}\"\n")

    # Connect to local Ollama server
    client = OllamaClient(model="qwen2.5:0.5b")
    if not client.is_alive():
        print("❌ Local Ollama server is not running!")
        print("Start it in another terminal with:  ollama serve")
        return

    agent = Agent(name="CustomPromptAgent", llm_client=client)

    # Check if prompt asks for tool actions (create/write file, run shell, read file)
    action_keywords = ["write", "create", "make file", "run command", "shell", "read file", "list files", "mkdir"]
    is_action_prompt = any(kw in prompt_text.lower() for kw in action_keywords)

    if is_action_prompt:
        print("🧠 Routing to Agent Tool Execution Engine...")
        output = agent.run_llm(prompt_text)
    else:
        print("🧠 Routing to Local LLM Direct Reasoning Engine...")
        resp = client.generate(prompt_text, system="You are a helpful software engineering AI assistant.")
        output = resp.text

    print("\n" + "=" * 60)
    print("✅ EXECUTION RESULT / RESPONSE:")
    print("=" * 60)
    print(output)
    print("=" * 60)

    # Save answer to response.txt
    with open(RESPONSE_FILE, "w", encoding="utf-8") as f:
        f.write(output)

    print(f"\n💾 Saved response to '{RESPONSE_FILE}'")

if __name__ == "__main__":
    main()
