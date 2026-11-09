"""Control: the smallest agent loop that uses Ollama's native tool calling. Standard library only."""

import json
import os
import sys
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
import harness  # noqa: E402
import tasks  # noqa: E402


def tool_specs():
    specs = []
    for name, desc, style, _ in tasks.TOOLS:
        if style == "text":
            props, req = {"text": {"type": "string", "description": "the text"}}, ["text"]
        else:
            props = {"a": {"type": "number", "description": "first number"},
                     "b": {"type": "number", "description": "second number"}}
            req = ["a", "b"]
        specs.append({"type": "function", "function": {
            "name": name, "description": desc,
            "parameters": {"type": "object", "properties": props, "required": req}}})
    return specs


SPECS = tool_specs()
STYLE = {name: style for name, _, style, _ in tasks.TOOLS}


def chat(messages):
    body = {"model": harness.MODEL, "messages": messages, "tools": SPECS, "stream": False}
    req = urllib.request.Request(harness.PROXY + "/api/chat", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=240) as r:
        return json.loads(r.read())["message"]


def call_tool(name, args):
    fn = tasks.TOOL_FUNCTIONS.get(name)
    if fn is None:
        return f"Unknown tool: {name}"
    try:
        return fn(args["text"]) if STYLE[name] == "text" else fn(args["a"], args["b"])
    except Exception as e:
        return f"Error: {e}"


def ask(prompt):
    messages = [{"role": "user", "content": prompt}]
    for _ in range(harness.MAX_STEPS):
        msg = chat(messages)
        messages.append(msg)
        calls = msg.get("tool_calls") or []
        if not calls:
            return msg.get("content", "")
        for c in calls:
            fn = c["function"]
            messages.append({"role": "tool", "tool_name": fn["name"],
                             "content": call_tool(fn["name"], fn.get("arguments") or {})})
    return messages[-1].get("content", "")


harness.run("plain", ask, version="python " + sys.version.split()[0])
