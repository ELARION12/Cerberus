"""
Ollama LLM client for Cerberus.

Lightweight HTTP client for a local Ollama server.
Stdlib only - no external dependencies required.
"""

import json
import urllib.request
import urllib.error
from dataclasses import dataclass
from typing import Optional, List, Dict, Any


class OllamaError(Exception):
    """Raised when the Ollama API returns an error or is unreachable."""
    pass


@dataclass
class OllamaResponse:
    text: str
    model: str
    done: bool
    total_duration_ms: float = 0.0
    eval_count: int = 0


class OllamaClient:
    """Minimal client for the local Ollama HTTP API."""

    def __init__(
        self,
        model: str = "qwen2.5:3b",
        host: str = "http://localhost:11434",
        timeout: float = 60.0,
    ):
        self.model = model
        self.host = host.rstrip("/")
        self.timeout = timeout

    def is_alive(self) -> bool:
        """Quick health check - does /api/tags respond?"""
        try:
            req = urllib.request.Request(f"{self.host}/api/tags")
            with urllib.request.urlopen(req, timeout=3) as resp:
                return resp.status == 200
        except Exception:
            return False

    def generate(
        self,
        prompt: str,
        system: Optional[str] = None,
        temperature: float = 0.2,
        format: Optional[Any] = None,
    ) -> OllamaResponse:
        """Single-shot generation against /api/generate.

        format: pass a JSON schema dict to force schema-constrained
        output (Ollama >= 0.5), or "json" for loose JSON mode.
        """
        payload = {
            "model": self.model,
            "prompt": prompt,
            "stream": False,
            "options": {"temperature": temperature},
        }
        if system:
            payload["system"] = system
        if format is not None:
            payload["format"] = format

        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            f"{self.host}/api/generate",
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body = json.loads(resp.read().decode("utf-8"))
        except urllib.error.URLError as e:
            raise OllamaError(f"Could not reach Ollama at {self.host}: {e}") from e

        return OllamaResponse(
            text=body.get("response", "").strip(),
            model=body.get("model", self.model),
            done=body.get("done", True),
            total_duration_ms=body.get("total_duration", 0) / 1e6,
            eval_count=body.get("eval_count", 0),
        )

    # JSON schema used to grammar-constrain the model's output at the token level
    # (enforced by Ollama, not just requested in the prompt). Returns {tool, args}.
    PLAN_SCHEMA = {
        "type": "object",
        "properties": {
            "tool": {"type": "string"},
            "args": {"type": "string"},
        },
        "required": ["tool", "args"],
    }

    def plan(self, task: str, tool_specs: Dict[str, str], context: str = "") -> str:
        """
        Ask the LLM to choose a tool + args, constrained to a JSON schema
        so the model can't drift into malformed syntax like 'args="./"'.
        Returns a "tool_name|args" string for compatibility with
        Agent::act()'s existing parser.
        """
        tools_desc = "\n".join(f"- {name}: {desc}" for name, desc in tool_specs.items())
        chain_hint = ""
        if "final_answer" in tool_specs:
            chain_hint = (
                "You can call several tools one after another. Each result "
                "is added to the conversation below as a 'tool <name>:' line. "
                "Use a result as the input of the next call when the task needs it. "
                "Never repeat a call that already has a result. As soon as the "
                "task is done, call final_answer with the answer.\n\n"
            )
        system = (
            "You are the planning module of a local agent runtime.\n"
            "Respond with a JSON object containing exactly two fields: "
            "'tool' (the tool name to use) and 'args' (the argument "
            "string for that tool, exactly as the tool expects it - "
            "no extra quoting, no key=value syntax).\n\n"
            f"{chain_hint}"
            f"Available tools:\n{tools_desc}\n\n"
            "Always pick the tool that actually accomplishes the task. "
            "Only use 'echo' if no other tool applies - never use it "
            "just to end the task early.\n\n"
            "CRITICAL: if the task or the context below already contains "
            "a specific fact you need (a number, a filename, a path, a "
            "value someone else found) - copy that value EXACTLY as "
            "written. Do not recompute it, round it, or guess a "
            "plausible-looking substitute. Using the wrong exact value "
            "when the correct one was already given is a serious error."
        )
        prompt = f"{context}\n\nTask: {task}" if context else f"Task: {task}"
        result = self.generate(
            prompt, system=system, temperature=0.1, format="json"
        )

        try:
            parsed = json.loads(result.text)
            tool = str(parsed.get("tool", "echo")).strip()
            args = str(parsed.get("args", "")).strip()
        except (json.JSONDecodeError, AttributeError, TypeError):
            # Graceful fallback: echo the raw response so the caller can debug
            tool, args = "echo", f"(unparseable model response: {result.text[:100]})"

        # Return in the "tool_name|args" format that Agent::act() expects
        return f"{tool}|{args}"

    # Schema for the orchestrator delegation call. Returns a list of {worker, subtask} pairs.
    # Sub-tasks must be independent because workers run concurrently and cannot share results.
    DELEGATE_SCHEMA = {
        "type": "object",
        "properties": {
            "assignments": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "worker": {"type": "string"},
                        "subtask": {"type": "string"},
                    },
                    "required": ["worker", "subtask"],
                },
            },
        },
        "required": ["assignments"],
    }

    def delegate(self, task: str, workers: Dict[str, str]) -> List[Dict[str, str]]:
        """
        Ask the LLM to break `task` into independent sub-tasks, each
        assigned to exactly one worker. `workers` maps worker name ->
        role/description. Returns [{"worker": ..., "subtask": ...}, ...].

        Note: sub-tasks are independent - this does not chain one
        worker's result into another's instructions.
        """
        workers_desc = "\n".join(f"- {name}: {role}" for name, role in workers.items())
        system = (
            "You are an orchestrator that breaks a task into independent "
            "sub-tasks and assigns each to exactly one worker best suited "
            "for it.\n"
            "Respond with a JSON object: "
            '{"assignments": [{"worker": "...", "subtask": "..."}, ...]}\n\n'
            f"Available workers:\n{workers_desc}\n\n"
            "Use as few workers as the task actually requires. Each "
            "subtask must be self-contained - workers cannot see each "
            "other's results, so do not assign a subtask that depends "
            "on another worker's output."
        )
        result = self.generate(task, system=system, temperature=0.2, format=self.DELEGATE_SCHEMA)
        try:
            parsed = json.loads(result.text)
            assignments = parsed.get("assignments", [])
            return [
                {"worker": str(a.get("worker", "")).strip(), "subtask": str(a.get("subtask", "")).strip()}
                for a in assignments
                if a.get("worker") and a.get("subtask")
            ]
        except (json.JSONDecodeError, AttributeError, TypeError):
            return []


if __name__ == "__main__":
    client = OllamaClient()
    print("Ollama alive:", client.is_alive())

    resp = client.generate("Say hello in exactly 3 words.")
    print("Raw generate():", resp.text)

    plan = client.plan(
        task="List the files in the current directory",
        tool_specs={
            "shell": "Run a shell command. args = the raw command, e.g. 'ls -la'",
            "read_file": "Read a file. args = a file path",
            "write_file": "Write a file. args = 'path|content'",
            "echo": "Return args back as-is. Only for direct text answers.",
        },
    )
    print("Plan output:", plan)