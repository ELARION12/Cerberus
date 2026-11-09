"""
What every adapter calls to run the tasks and report results. Standard library only.

An adapter builds its agent, then hands over one function that takes a prompt and
returns the agent's final answer text:

    harness.run("langchain", ask)

Results go to stdout as one JSON object per line so the parent can read them
while the run is still going, and a crash keeps everything written so far.
"""

import contextlib
import json
import os
import resource
import signal
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tasks  # noqa: E402

TASK_TIMEOUT_S = int(os.environ.get("BENCH_TASK_TIMEOUT", "240"))
MAX_STEPS = int(os.environ.get("BENCH_MAX_STEPS", "8"))
PROXY = os.environ.get("BENCH_PROXY", "http://127.0.0.1:11434")
MODEL = os.environ.get("BENCH_MODEL", "qwen2.5:1.5b")


MARK = "@@BENCH@@ "      # frameworks print to stdout too; only marked lines are results


def emit(obj):
    print(MARK + json.dumps(obj), flush=True)


def rss_mb():
    with open("/proc/self/statm") as f:
        return int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE") / 1e6


def peak_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0


def proxy_stats():
    with urllib.request.urlopen(PROXY + "/__stats", timeout=10) as r:
        return json.loads(r.read())


@contextlib.contextmanager
def deadline(seconds):
    def on_alarm(signum, frame):
        raise TimeoutError(f"task took longer than {seconds} s")
    old = signal.signal(signal.SIGALRM, on_alarm)
    signal.setitimer(signal.ITIMER_REAL, seconds)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, old)


def run(framework, ask, levels=None, version=""):
    """Run every task of the chosen levels through ask(prompt) -> answer text."""
    wanted = levels or os.environ.get("BENCH_LEVELS", "easy,medium,hard").split(",")
    t0 = float(os.environ.get("BENCH_T0", time.time()))
    emit({
        "event": "ready", "framework": framework, "version": version,
        "cold_start_s": time.time() - t0, "rss_mb": rss_mb(),
    })
    limit = int(os.environ.get("BENCH_LIMIT", "0"))      # smoke tests: first N tasks per level
    seen = {}
    for task in tasks.TASKS:
        if task["level"] not in wanted:
            continue
        seen[task["level"]] = seen.get(task["level"], 0) + 1
        if limit and seen[task["level"]] > limit:
            continue
        tasks.CALLS.clear()
        before = proxy_stats()
        started = time.perf_counter()
        answer, error = "", ""
        try:
            with deadline(TASK_TIMEOUT_S):
                answer = str(ask(task["prompt"]))
        except TimeoutError as e:
            error = str(e)
        except Exception as e:      # a framework error counts as a failed task, not a crash
            error = f"{type(e).__name__}: {e}"[:300]
        total = time.perf_counter() - started
        after = proxy_stats()
        emit({
            "event": "task", "framework": framework, "level": task["level"], "prompt": task["prompt"],
            "expected": task["expected"], "answer": answer[:300], "ok": tasks.is_correct(answer, task["expected"]),
            "error": error, "total_s": total,
            "model_s": after["model_seconds"] - before["model_seconds"],
            "calls": after["calls"] - before["calls"],
            "tool_calls": len(tasks.CALLS),
            "prompt_tokens": after["prompt_tokens"] - before["prompt_tokens"],
            "output_tokens": after["output_tokens"] - before["output_tokens"],
            "rss_mb": rss_mb(),
        })
    emit({"event": "done", "framework": framework, "peak_rss_mb": peak_mb()})
