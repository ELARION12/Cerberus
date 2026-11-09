"""
End-to-end agent benchmark with a real local model (Ollama).

It runs the same small tasks through two loops that send the model identical
prompts:

  cerberus  Agent.run_llm()
  plain     a minimal Python loop: ask the planner, look the tool up in a dict,
            run it, retry with the error on failure

Both keep one running conversation across the tasks, the way a long-lived agent
does, so later prompts include earlier tasks and their results.

and reports success rate, time per task split into model time and everything
else, and the memory of the agent process. Prompts are made identical by
copying the tool list and the context format out of a real Cerberus agent.

Needs a running Ollama server and a pulled model, for example:
    ollama serve &
    ollama pull qwen2.5:0.5b

    python benchmarks/agent_e2e.py
    python benchmarks/agent_e2e.py --model qwen2.5:1.5b --rounds 2
"""

import argparse
import importlib.util
import json
import os
import platform
import re
import resource
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MAX_STEPS = 5


# ---- tools: one implementation, used by both loops ---------------------------

def _unquote(text):
    return text.strip().strip("'\"").strip()


def t_word_count(args):
    return True, str(len(_unquote(args).split()))


def t_add(args):
    nums = re.findall(r"-?\d+(?:\.\d+)?", args)
    if len(nums) < 2:
        return False, "need two numbers"
    total = sum(float(n) for n in nums[:2])
    return True, str(int(total)) if total == int(total) else str(total)


def t_reverse(args):
    return True, _unquote(args)[::-1]


def t_upper(args):
    return True, _unquote(args).upper()


TOOLS = {
    "word_count": ("Count the words in a text. args = the text", t_word_count),
    "add": ("Add two numbers. args = the two numbers separated by a space", t_add),
    "reverse_text": ("Reverse a text. args = the text", t_reverse),
    "to_upper": ("Convert a text to uppercase. args = the text", t_upper),
}

TASKS = [
    ("How many words are in 'the quick brown fox'?", "4"),
    ("Count the words in: one two three four five six", "6"),
    ("How many words does 'to be or not to be that is the question' have?", "10"),
    ("How many words are in 'a b c'?", "3"),
    ("What is 17 plus 25?", "42"),
    ("Add 120 and 305.", "425"),
    ("Sum of 9 and 14?", "23"),
    ("What is 1000 plus 1?", "1001"),
    ("Reverse the text 'stressed'", "desserts"),
    ("Reverse this word: hello", "olleh"),
    ("Please reverse 'level up'", "pu level"),
    ("Reverse 'abc123'", "321cba"),
    ("Convert 'hello world' to uppercase", "HELLO WORLD"),
    ("Make this uppercase: cerberus", "CERBERUS"),
    ("Shout this: good morning", "GOOD MORNING"),
    ("Uppercase the text 'edge devices'", "EDGE DEVICES"),
]


# ---- helpers shared by parent and children -----------------------------------

def load_cerberus():
    """Use the installed package if there is one, else the source tree."""
    try:
        import cerberus_agent as ca
        if ca._BACKEND_AVAILABLE:
            return ca
    except ImportError:
        pass
    sys.path.insert(0, os.path.join(HERE, "..", "python"))
    for name in [m for m in sys.modules if m.startswith("cerberus_agent")]:
        del sys.modules[name]
    import cerberus_agent as ca
    return ca


def standalone_ollama_client():
    """Import llm.py on its own so the plain loop never loads the native module."""
    spec = importlib.util.find_spec("cerberus_agent")
    if spec is None:
        path = os.path.join(HERE, "..", "python", "cerberus_agent", "llm.py")
    else:
        path = os.path.join(list(spec.submodule_search_locations)[0], "llm.py")
    mod_spec = importlib.util.spec_from_file_location("standalone_llm", path)
    mod = importlib.util.module_from_spec(mod_spec)
    sys.modules["standalone_llm"] = mod
    mod_spec.loader.exec_module(mod)
    return mod.OllamaClient


BUILT_IN_TOOLS = ("shell", "read_file", "write_file", "echo")


def only_benchmark_tools(agent):
    """The engine ships four built-in tools. Remove them so both loops offer the
    model exactly the same tools, and so a small model cannot run shell commands."""
    for name in BUILT_IN_TOOLS:
        agent.tools.remove_tool(name)


def peak_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0


def ollama_rss_mb():
    total = 0
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            with open(f"/proc/{pid}/comm") as f:
                if f.read().strip() != "ollama":
                    continue
            with open(f"/proc/{pid}/status") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        total += int(line.split()[1])
        except OSError:
            pass
    return total / 1024.0


def timed_client_class(base):
    class Timed(base):
        def __init__(self, *a, **k):
            super().__init__(*a, **k)
            self.model_seconds = 0.0
            self.calls = 0
            self.contexts = []

        def plan(self, task, tool_specs, context=""):
            self.contexts.append(str(context))
            t = time.perf_counter()
            try:
                return super().plan(task, tool_specs, context)
            finally:
                self.model_seconds += time.perf_counter() - t
                self.calls += 1
    return Timed


# ---- children: one loop each, run in its own process --------------------------

def child_cerberus(cfg):
    ca = load_cerberus()
    from cerberus_agent import Agent, ToolOutput
    from cerberus_agent.llm import OllamaClient

    client = timed_client_class(OllamaClient)(model=cfg["model"], host=cfg["host"], timeout=120)
    agent = Agent(name="E2E", num_threads=1, load_defaults=False, llm_client=client, max_steps=MAX_STEPS)
    only_benchmark_tools(agent)
    for name, (desc, fn) in TOOLS.items():
        def make(fn):
            def tool(inp):
                ok, text = fn(inp.args)
                out = ToolOutput()
                out.success = ok
                if ok:
                    out.result = text
                else:
                    out.error = text
                return out
            return tool
        agent.add_tool(name, desc, make(fn))
    system_text = cfg["context_prefix"][len("system: "):-1]   # the agent starts every conversation with this line

    def reset():
        agent.context.clear()
        agent.context.add_system(system_text)

    return run_tasks(cfg, client, lambda task: agent.run_llm(task, max_steps=MAX_STEPS), reset)


def child_plain(cfg):
    OllamaClient = standalone_ollama_client()
    client = timed_client_class(OllamaClient)(model=cfg["model"], host=cfg["host"], timeout=120)
    specs = cfg["tool_specs"]
    state = {"context": cfg["context_prefix"]}     # grows across tasks, like the agent's context

    def run(task):
        state["context"] += f"user: {task}\n"
        for _ in range(MAX_STEPS):
            plan_str = client.plan(task, specs, state["context"])
            sep = plan_str.find("|")
            name = plan_str[:sep] if sep != -1 else plan_str
            args = plan_str[sep + 1:] if sep != -1 else ""
            entry = TOOLS.get(name)
            if entry is None:
                ok, text = False, f"Unknown tool: {name}"
            else:
                ok, text = entry[1](args)
            if ok:
                state["context"] += f"tool {name}: {text}\n"
                return text
            state["context"] += f"tool {name}: ERROR: {text}\n"
        return "Max steps reached without completion."

    def reset():
        state["context"] = cfg["context_prefix"]

    return run_tasks(cfg, client, run, reset)


def run_tasks(cfg, client, run, reset=None):
    client.generate("Say OK.")      # load the model before timing anything
    rows = []
    for task, expected in TASKS * cfg["repeats"]:
        if reset is not None and cfg["fresh"]:
            reset()
        client.model_seconds, client.calls = 0.0, 0
        t = time.perf_counter()
        try:
            out = str(run(task)).strip()
        except Exception as e:      # a harness error counts as a failure, not a crash
            out = f"EXCEPTION: {e}"
        total = time.perf_counter() - t
        rows.append({
            "task": task, "expected": expected, "got": out, "ok": out == expected,
            "total_ms": total * 1000, "model_ms": client.model_seconds * 1000, "calls": client.calls,
        })
    return {"rows": rows, "peak_mb": peak_mb()}


# ---- parent -------------------------------------------------------------------

def capture_prompt_shape(cfg):
    """Ask a real Cerberus agent what it sends, so the plain loop can copy it."""
    ca = load_cerberus()
    from cerberus_agent import Agent, ToolOutput

    class Recorder:
        def __init__(self, replies):
            self.replies, self.specs, self.contexts = list(replies), None, []

        def plan(self, task, tool_specs, context=""):
            self.specs = dict(tool_specs)
            self.contexts.append(str(context))
            return self.replies.pop(0)

        def is_alive(self):
            return True

    def build(rec, extra=None):
        agent = Agent(name="E2E", num_threads=1, load_defaults=False, llm_client=rec, max_steps=MAX_STEPS)
        only_benchmark_tools(agent)
        for name, (desc, fn) in TOOLS.items():
            agent.add_tool(name, desc, lambda inp: _ok("x"))
        if extra:
            agent.add_tool(*extra)
        return agent

    def _ok(text):
        out = ToolOutput()
        out.success, out.result = True, text
        return out

    def _fail(inp):
        out = ToolOutput()
        out.success, out.error = False, "ERR_PLACEHOLDER"
        return out

    user_line = "user: TASK_PLACEHOLDER\n"

    first = Recorder(["word_count|x"])
    build(first).run_llm("TASK_PLACEHOLDER")
    ctx0 = first.contexts[0]
    assert ctx0.endswith(user_line), f"unexpected context format: {ctx0!r}"
    prefix = ctx0[:-len(user_line)]

    retry = Recorder(["boom|x", "word_count|x"])
    build(retry, ("boom", "always fails", _fail)).run_llm("TASK_PLACEHOLDER")
    assert retry.contexts[0] == ctx0
    assert retry.contexts[1] == prefix + user_line + "tool boom: ERROR: ERR_PLACEHOLDER\n", retry.contexts[1]

    two = Recorder(["word_count|x", "word_count|x"])
    agent = build(two)
    agent.run_llm("A")
    agent.run_llm("B")
    assert two.contexts[1] == prefix + "user: A\ntool word_count: x\nuser: B\n", two.contexts[1]

    cfg["tool_specs"] = first.specs
    cfg["context_prefix"] = prefix


def spawn(kind, cfg_path):
    out = subprocess.run(
        [sys.executable, os.path.abspath(__file__), "--child", kind, "--config", cfg_path],
        capture_output=True, text=True,
    )
    if out.returncode != 0:
        sys.exit(f"{kind} run failed:\n{out.stderr[-2000:]}")
    return json.loads(out.stdout.strip().splitlines()[-1])


def median(xs):
    return statistics.median(xs) if xs else 0.0


def cpu_name():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith(("model name", "Model", "Hardware")):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default="qwen2.5:0.5b")
    ap.add_argument("--host", default="http://localhost:11434")
    ap.add_argument("--rounds", type=int, default=1, help="how many times to repeat the A B B A order")
    ap.add_argument("--repeats", type=int, default=1, help="how many times each child runs the task list")
    ap.add_argument("--fresh", action="store_true", help="start every task with an empty conversation")
    ap.add_argument("--child", choices=["cerberus", "plain"], help=argparse.SUPPRESS)
    ap.add_argument("--config", help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args.child:
        cfg = json.load(open(args.config))
        result = child_cerberus(cfg) if args.child == "cerberus" else child_plain(cfg)
        print(json.dumps(result))
        return

    OllamaClient = standalone_ollama_client()
    probe = OllamaClient(model=args.model, host=args.host)
    if not probe.is_alive():
        sys.exit(f"No Ollama server at {args.host}. Start one with: ollama serve")

    cfg = {"model": args.model, "host": args.host, "repeats": args.repeats, "fresh": args.fresh}
    capture_prompt_shape(cfg)
    probe.generate("Say OK.")       # make sure the model is loaded before the first child starts
    server_mb = ollama_rss_mb()

    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(cfg, f)
        cfg_path = f.name

    print(f"machine : {cpu_name()}, {os.cpu_count()} threads, Python {platform.python_version()}")
    print(f"model   : {args.model} via Ollama at {args.host} (server uses about {server_mb:.0f} MB)")
    print(f"workload: {len(TASKS)} tasks x {args.repeats}, up to {MAX_STEPS} planner calls each, "
          f"order cerberus/plain/plain/cerberus x {args.rounds}")
    print("context : " + ("fresh for every task" if args.fresh else "one running conversation per process") + "\n")

    results = {"cerberus": [], "plain": []}
    for _ in range(args.rounds):
        for kind in ("cerberus", "plain", "plain", "cerberus"):
            print(f"  running {kind} ...", flush=True)
            results[kind].append(spawn(kind, cfg_path))
    os.unlink(cfg_path)

    print()
    print("per run (success / tasks, planner calls per task):")
    for kind in ("cerberus", "plain"):
        parts = []
        for res in results[kind]:
            n_ok = sum(r["ok"] for r in res["rows"])
            parts.append(f"{n_ok}/{len(res['rows'])} ({statistics.mean(r['calls'] for r in res['rows']):.2f})")
        print(f"  {kind:<9}" + "   ".join(parts))
    print()
    print(f"{'':<10}{'success':>9}{'ms/task':>10}{'model ms':>10}{'other ms':>10}{'calls/task':>12}{'agent MB':>10}")
    for kind in ("cerberus", "plain"):
        rows = [r for res in results[kind] for r in res["rows"]]
        ok = sum(r["ok"] for r in rows)
        total = median([r["total_ms"] for r in rows])
        model = median([r["model_ms"] for r in rows])
        other = median([r["total_ms"] - r["model_ms"] for r in rows])
        calls = statistics.mean(r["calls"] for r in rows)
        mem = median([res["peak_mb"] for res in results[kind]])
        print(f"{kind:<10}{ok:>5}/{len(rows):<3}{total:>10.0f}{model:>10.0f}{other:>10.1f}{calls:>12.2f}{mem:>10.1f}")

    print("\nwrong or failed answers:")
    shown = False
    for kind in ("cerberus", "plain"):
        bad = {}
        for res in results[kind]:
            for r in res["rows"]:
                if not r["ok"]:
                    bad.setdefault(r["task"], []).append(r["got"][:40])
        for task, got in bad.items():
            shown = True
            print(f"  {kind:<9} {task[:50]!r:<54} got {got[0]!r}" + (f" (+{len(got) - 1} more)" if len(got) > 1 else ""))
    if not shown:
        print("  none")
    print("\nother ms = total time minus time spent waiting for the model (parsing, tools, loop, prompt building).")


if __name__ == "__main__":
    main()
