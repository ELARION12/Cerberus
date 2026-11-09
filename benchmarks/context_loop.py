"""
Context handling benchmark.

What it measures: keeping a rolling conversation inside a token budget. Each
iteration appends a user and an assistant message, drops the oldest ones when
the budget is exceeded, and every 10th iteration renders the whole prompt.

What it does NOT measure: model calls, tool calls or planning. The numbers say
how much work the bookkeeping around an agent costs, nothing about how fast an
agent answers.

Each implementation runs in its own process so the memory numbers do not mix.

    python benchmarks/context_loop.py
    python benchmarks/context_loop.py --iterations 200000 --repeat 5
    python benchmarks/context_loop.py --impls cerberus plain
"""

import argparse
import json
import os
import platform
import resource
import statistics
import subprocess
import sys
import time
from collections import deque

IMPLS = ("cerberus", "plain", "langchain")
BUDGET_CHARS = 16384          # about 4096 tokens
RENDER_EVERY = 10


def rss_mb():
    with open("/proc/self/statm") as f:
        return int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE") / 1e6


def peak_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0


def user_text(i):
    return f"Task {i}: Calculate and return data parameters."


def assistant_text(i):
    return f"Result {i}: Data processed successfully."


def load_cerberus():
    """Use the installed package if there is one, else the source tree."""
    try:
        import cerberus_agent as ca
        if ca._BACKEND_AVAILABLE:
            return ca
    except ImportError:
        pass
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
    for name in [m for m in sys.modules if m.startswith("cerberus_agent")]:
        del sys.modules[name]
    import cerberus_agent as ca
    return ca


def run_cerberus(n):
    ContextBuffer = load_cerberus()._cerberus_core.ContextBuffer

    after_import = rss_mb()
    ctx = ContextBuffer(1024 * 1024, 1024 * 1024, BUDGET_CHARS // 4)
    ctx.add_system("System: Execute sustained processing loop.")
    base_peak = peak_mb()
    start = time.perf_counter()
    for i in range(n):
        ctx.add_user(user_text(i))
        ctx.add_assistant(assistant_text(i))
        if i % RENDER_EVERY == 0:
            ctx.build_prompt()
    elapsed = time.perf_counter() - start
    return after_import, base_peak, elapsed


def run_plain(n):
    after_import = rss_mb()
    system = "System: Execute sustained processing loop."
    msgs = deque()
    chars = len(system)
    base_peak = peak_mb()
    start = time.perf_counter()
    for i in range(n):
        u, a = user_text(i), assistant_text(i)
        msgs.append(("user", u))
        msgs.append(("assistant", a))
        chars += len(u) + len(a)
        while chars > BUDGET_CHARS and msgs:
            chars -= len(msgs.popleft()[1])
        if i % RENDER_EVERY == 0:
            "\n".join([f"[SYSTEM]: {system}"] + [f"[{r.upper()}]: {t}" for r, t in msgs])
    elapsed = time.perf_counter() - start
    return after_import, base_peak, elapsed


def run_langchain(n):
    from langchain_core.messages import AIMessage, HumanMessage, SystemMessage

    after_import = rss_mb()
    system = SystemMessage(content="System: Execute sustained processing loop.")
    msgs = deque()
    chars = len(system.content)
    base_peak = peak_mb()
    start = time.perf_counter()
    for i in range(n):
        u = HumanMessage(content=user_text(i))
        a = AIMessage(content=assistant_text(i))
        msgs.append(u)
        msgs.append(a)
        chars += len(u.content) + len(a.content)
        while chars > BUDGET_CHARS and msgs:
            chars -= len(msgs.popleft().content)
        if i % RENDER_EVERY == 0:
            "\n".join([f"[SYSTEM]: {system.content}"] + [f"[{m.type.upper()}]: {m.content}" for m in msgs])
    elapsed = time.perf_counter() - start
    return after_import, base_peak, elapsed


RUNNERS = {"cerberus": run_cerberus, "plain": run_plain, "langchain": run_langchain}


def child(impl, n):
    before_import = rss_mb()
    after_import, base_peak, elapsed = RUNNERS[impl](n)
    print(json.dumps({
        "impl": impl,
        "ops_per_sec": n / elapsed,
        "import_mb": after_import - before_import,
        "loop_peak_growth_mb": max(0.0, peak_mb() - base_peak),
    }))


def installed(impl):
    if impl == "langchain":
        try:
            import langchain_core  # noqa: F401
        except ImportError:
            return False
    if impl == "cerberus":
        try:
            return load_cerberus()._BACKEND_AVAILABLE
        except ImportError:
            return False
    return True


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
    ap.add_argument("--iterations", type=int, default=50000)
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--impls", nargs="+", choices=IMPLS, default=list(IMPLS))
    ap.add_argument("--child", choices=IMPLS, help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args.child:
        child(args.child, args.iterations)
        return

    print(f"machine : {cpu_name()}, {os.cpu_count()} threads, Python {platform.python_version()}")
    print(f"workload: {args.iterations} iterations x 2 messages, {BUDGET_CHARS} char budget, "
          f"prompt rendered every {RENDER_EVERY}; median of {args.repeat} runs\n")
    rows = []
    for impl in args.impls:
        if not installed(impl):
            print(f"skipping {impl}: not installed")
            continue
        runs = []
        for _ in range(args.repeat):
            out = subprocess.run(
                [sys.executable, os.path.abspath(__file__), "--child", impl,
                 "--iterations", str(args.iterations)],
                capture_output=True, text=True, check=True,
            ).stdout.strip().splitlines()[-1]
            runs.append(json.loads(out))
        rows.append((
            impl,
            statistics.median(r["ops_per_sec"] for r in runs),
            statistics.median(r["import_mb"] for r in runs),
            statistics.median(r["loop_peak_growth_mb"] for r in runs),
        ))

    print(f"{'implementation':<16}{'iterations/sec':>16}{'import cost MB':>16}{'loop growth MB':>16}")
    for impl, ops, imp, growth in rows:
        print(f"{impl:<16}{ops:>16,.0f}{imp:>16.1f}{growth:>16.1f}")
    print("\nplain     = deque of tuples, no dependencies")
    print("langchain = langchain_core message objects")


if __name__ == "__main__":
    main()
