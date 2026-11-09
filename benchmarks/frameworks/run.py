"""
Compare Cerberus with other agent frameworks on the same local model.

Each framework runs in its own virtual environment (see setup_envs.sh) and in its
own process. All of them talk to the model through a timing proxy, so model time
is measured the same way for everyone. The tasks come in three rounds, easy,
medium and hard, and every round gets its own score.

    bash benchmarks/frameworks/setup_envs.sh
    python benchmarks/frameworks/run.py --model qwen2.5:1.5b

Needs a running Ollama server with the model pulled. CPU only is fine.
"""

import argparse
import datetime
import http.client
import json
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "common"))
import proxy  # noqa: E402
import tasks  # noqa: E402

ALL = ["plain", "cerberus", "langchain", "llamaindex", "smolagents"]
harness_mark = "@@BENCH@@ "


def cpu_name():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith(("model name", "Model", "Hardware")):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def mem_gb():
    try:
        with open("/proc/meminfo") as f:
            return int(f.readline().split()[1]) / 1048576
    except OSError:
        return 0.0


def warm_up(host, port, model):
    conn = http.client.HTTPConnection(host, port, timeout=900)
    body = json.dumps({"model": model, "prompt": "Say OK.", "stream": False, "keep_alive": "60m",
                       "options": {"num_ctx": 4096}})
    conn.request("POST", "/api/generate", body, {"Content-Type": "application/json"})
    resp = conn.getresponse()
    resp.read()
    conn.close()
    if resp.status != 200:
        sys.exit(f"Ollama could not load {model} (HTTP {resp.status}). Did you pull it?")


def run_framework(name, args, base_url, results_file):
    py = os.path.join(args.envs, name, "bin", "python")
    if not os.path.exists(py):
        print(f"  skipping {name}: no environment at {os.path.dirname(os.path.dirname(py))} "
              f"(run setup_envs.sh {name})")
        return None
    env = dict(os.environ)
    env.update({
        "BENCH_PROXY": base_url, "BENCH_MODEL": args.model, "BENCH_LEVELS": ",".join(args.levels),
        "BENCH_LIMIT": str(args.limit), "BENCH_TASK_TIMEOUT": str(args.task_timeout),
        "BENCH_MAX_STEPS": str(args.max_steps), "BENCH_T0": repr(time.time()),
        "PYTHONDONTWRITEBYTECODE": "1", "PYTHONUNBUFFERED": "1", "TOKENIZERS_PARALLELISM": "false",
    })
    env.pop("PYTHONPATH", None)
    err = tempfile.TemporaryFile("w+")
    started = time.time()
    child = subprocess.Popen([py, os.path.join(HERE, "adapters", "run_" + name + ".py")], env=env,
                             stdout=subprocess.PIPE, stderr=err, text=True)
    events = []
    try:
        for line in child.stdout:
            line = line.strip()
            if not line.startswith(harness_mark):
                continue                      # something the framework printed itself
            line = line[len(harness_mark):]
            ev = json.loads(line)
            events.append(ev)
            results_file.write(line + "\n")
            results_file.flush()
            if ev["event"] == "ready":
                print(f"  ready in {ev['cold_start_s']:.1f} s ({ev['version']})", flush=True)
            elif ev["event"] == "task":
                mark = "ok  " if ev["ok"] else ("ERR " if ev["error"] else "miss")
                print(f"    {ev['level']:<6} {mark} {ev['total_s']:5.1f}s {ev['calls']} calls  {ev['prompt'][:52]}", flush=True)
            if time.time() - started > args.framework_timeout:
                print("  framework time limit reached, stopping it")
                child.kill()
                break
    finally:
        child.wait()
    if not any(e["event"] == "ready" for e in events):
        err.seek(0)
        print("  FAILED to start:\n" + "".join("    " + l for l in err.readlines()[-12:]))
    return events


def median(xs):
    return statistics.median(xs) if xs else float("nan")


def report(all_events, args):
    out = []
    p = out.append
    p(f"machine : {cpu_name()}, {os.cpu_count()} threads, {mem_gb():.0f} GB RAM, no GPU")
    p(f"model   : {args.model} via Ollama, CPU only; temperature 0, 4096 context, same settings for every framework")
    p(f"limits  : {args.max_steps} steps and {args.task_timeout} s per task; {datetime.date.today()}")
    versions = {n: next((e["version"] for e in ev if e["event"] == "ready"), "?") for n, ev in all_events.items()}
    p("versions: " + ", ".join(f"{v}" for v in versions.values()))
    p("")
    for level in args.levels:
        n_tasks = sum(t["level"] == level for t in tasks.TASKS)
        if args.limit:
            n_tasks = min(n_tasks, args.limit)
        p(f"ROUND: {level.upper()}  ({n_tasks} tasks)")
        p(f"  {'framework':<12}{'score':>8}{'%':>6}{'s/task':>9}{'overhead ms':>13}{'model calls':>13}{'tool calls':>12}{'RSS MB':>8}{'errors':>8}")
        for name, ev in all_events.items():
            rows = [e for e in ev if e["event"] == "task" and e["level"] == level]
            if not rows:
                continue
            ok = sum(r["ok"] for r in rows)
            overhead = median([(r["total_s"] - r["model_s"]) * 1000 for r in rows])
            p(f"  {name:<12}{ok:>5}/{len(rows):<2}{100 * ok / len(rows):>5.0f}%"
              f"{median([r['total_s'] for r in rows]):>9.1f}{overhead:>13.1f}"
              f"{statistics.mean(r['calls'] for r in rows):>13.1f}{statistics.mean(r['tool_calls'] for r in rows):>12.1f}"
              f"{max(r['rss_mb'] for r in rows):>8.0f}{sum(bool(r['error']) for r in rows):>8}")
        p("")
    p("OVERALL")
    p(f"  {'framework':<12}{'score':>9}{'cold start s':>14}{'RSS at start MB':>17}{'peak RSS MB':>13}{'total wall s':>14}")
    for name, ev in all_events.items():
        rows = [e for e in ev if e["event"] == "task"]
        if not rows:
            continue
        ready = next(e for e in ev if e["event"] == "ready")
        done = next((e for e in ev if e["event"] == "done"), None)
        peak = done["peak_rss_mb"] if done else max(r["rss_mb"] for r in rows)
        ok = sum(r["ok"] for r in rows)
        p(f"  {name:<12}{ok:>5}/{len(rows):<3}{ready['cold_start_s']:>14.1f}{ready['rss_mb']:>17.1f}"
          f"{peak:>13.1f}{sum(r['total_s'] for r in rows):>14.0f}")
    p("")
    p("overhead ms = median time per task outside the model (the framework's own work). s/task includes the model.")
    p("tool calls = how many tools actually ran per task. RSS MB = agent process memory at the end of the round.")
    p("RSS = memory of the agent process only; the model runs in the Ollama server, the same for everyone.")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default="qwen2.5:1.5b")
    ap.add_argument("--frameworks", nargs="+", default=ALL, choices=ALL)
    ap.add_argument("--levels", nargs="+", default=list(tasks.LEVELS), choices=list(tasks.LEVELS))
    ap.add_argument("--limit", type=int, default=0, help="only the first N tasks of each round (smoke test)")
    ap.add_argument("--ollama", default="127.0.0.1:11434")
    ap.add_argument("--envs", default=os.environ.get("BENCH_ENVS", os.path.expanduser("~/bench-envs")))
    ap.add_argument("--task-timeout", type=int, default=240)
    ap.add_argument("--framework-timeout", type=int, default=3000)
    ap.add_argument("--max-steps", type=int, default=8)
    ap.add_argument("--out", default=os.path.join(HERE, "..", "results"))
    ap.add_argument("--report-from", nargs="+", metavar="JSONL",
                    help="do not run anything; build the tables from saved raw files "
                         "(for a framework in several files, the later file wins)")
    args = ap.parse_args()

    if args.report_from:
        merged = {}
        for path in args.report_from:
            per_file = {}
            for ln in open(path):
                if ln.strip():
                    ev = json.loads(ln)
                    per_file.setdefault(ev["framework"], []).append(ev)
            merged.update(per_file)
        ordered = {n: merged[n] for n in ALL if n in merged}
        text = report(ordered, args)
        print(text)
        os.makedirs(args.out, exist_ok=True)
        out_txt = os.path.join(args.out, f"frameworks-{args.model.replace(':', '-')}-{datetime.date.today().isoformat()}.txt")
        with open(out_txt, "w") as f:
            f.write(text + "\n")
        print(f"\nwritten: {os.path.relpath(out_txt)}")
        return

    tasks.selftest()
    host, port = args.ollama.split(":")
    warm_up(host, int(port), args.model)
    server, stats, base_url = proxy.start((host, int(port)))

    os.makedirs(args.out, exist_ok=True)
    stamp = datetime.date.today().isoformat()
    tag = args.model.replace(":", "-").replace("/", "-")
    subset = "" if set(args.frameworks) == set(ALL) else "-" + "+".join(args.frameworks)
    raw_path = os.path.join(args.out, f"frameworks-{tag}-{stamp}{subset}.jsonl")
    all_events = {}
    with open(raw_path, "w") as raw:
        for name in args.frameworks:
            print(f"== {name}", flush=True)
            events = run_framework(name, args, base_url, raw)
            if events:
                all_events[name] = events
    text = report(all_events, args)
    print("\n" + text)
    with open(raw_path.replace(".jsonl", ".txt"), "w") as f:
        f.write(text + "\n")
    print(f"\nraw results: {os.path.relpath(raw_path)}")


if __name__ == "__main__":
    main()
