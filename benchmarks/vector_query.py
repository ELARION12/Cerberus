"""
Vector search benchmark: brute-force top-5 over a few thousand embeddings.

Reports time per query, index size in memory, and how often the top 5 matches
the exact float32 answer ("recall"). The data is clustered random vectors, not
real embeddings, so treat the recall of the 8-bit index as a rough guide.

numpy is only used as a reference if it is installed. Geryon does not need it.

    python benchmarks/vector_query.py
    python benchmarks/vector_query.py --vectors 20000 --dim 768
"""

import argparse
import json
import os
import platform
import random
import subprocess
import sys
import time

TOP_K = 5


def make_data(n, dim, queries, seed=1):
    rng = random.Random(seed)
    centers = [[rng.uniform(-1, 1) for _ in range(dim)] for _ in range(max(8, n // 100))]
    rows = []
    for _ in range(n):
        c = rng.choice(centers)
        rows.append([x + rng.gauss(0, 0.35) for x in c])
    qs = []
    for _ in range(queries):
        c = rng.choice(centers)
        qs.append([x + rng.gauss(0, 0.35) for x in c])
    return rows, qs


def load_geryon():
    """Use the installed package if there is one, else the source tree."""
    try:
        import geryon as ca
        if ca._BACKEND_AVAILABLE:
            return ca
    except ImportError:
        pass
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
    for name in [m for m in sys.modules if m.startswith("geryon")]:
        del sys.modules[name]
    import geryon as ca
    return ca


def timed(fn, qs):
    fn(qs[0])  # warm up
    best = float("inf")
    for _ in range(3):
        t = time.perf_counter()
        for q in qs:
            fn(q)
        best = min(best, time.perf_counter() - t)
    return best / len(qs) * 1000.0


def child(impl, n, dim, nq):
    rows, qs = make_data(n, dim, nq)
    if impl.startswith("geryon"):
        ca = load_geryon()

        vb = ca.VectorBuffer(dim=dim, max_vectors=n)
        if impl == "geryon-sq8":
            vb.enable_sq8(True)
        for r in rows:
            vb.push(r, "")
        if impl == "geryon-hnsw":
            t0 = time.perf_counter()
            vb.build_hnsw_index()
            build_s = time.perf_counter() - t0
        ms = timed(lambda q: vb.query(q, top_k=TOP_K), qs)
        tops = [[m.index for m in vb.query(q, top_k=TOP_K)] for q in qs]
        extra = {"backend": ca.simd_backend(), "index_mb": vb.bytes_used() / 1e6}
        if impl == "geryon-hnsw":
            extra["build_s"] = build_s
    else:
        import numpy as np

        a = np.asarray(rows, dtype=np.float32)
        a /= np.linalg.norm(a, axis=1, keepdims=True)

        def query(q):
            v = np.asarray(q, dtype=np.float32)
            v /= np.linalg.norm(v)
            s = a @ v
            idx = np.argpartition(-s, TOP_K)[:TOP_K]
            return idx[np.argsort(-s[idx])]

        ms = timed(query, qs)
        tops = [[int(i) for i in query(q)] for q in qs]
        extra = {"backend": "numpy " + np.__version__, "index_mb": a.nbytes / 1e6}
    print(json.dumps({"impl": impl, "ms": ms, "tops": tops, **extra}))


def run_child(impl, args, env_extra=None):
    env = dict(os.environ)
    env.pop("GERYON_SIMD", None)
    env.update(env_extra or {})
    out = subprocess.run(
        [sys.executable, os.path.abspath(__file__), "--child", impl,
         "--vectors", str(args.vectors), "--dim", str(args.dim), "--queries", str(args.queries)],
        env=env, capture_output=True, text=True, check=True,
    ).stdout.strip().splitlines()[-1]
    return json.loads(out)


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
    ap.add_argument("--vectors", type=int, default=5000)
    ap.add_argument("--dim", type=int, default=384)
    ap.add_argument("--queries", type=int, default=50)
    ap.add_argument("--child", help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.child:
        child(args.child, args.vectors, args.dim, args.queries)
        return

    print(f"machine : {cpu_name()}, {os.cpu_count()} threads, Python {platform.python_version()}")
    print(f"workload: {args.vectors} vectors x {args.dim} dims, {args.queries} queries, top {TOP_K}\n")

    plan = [
        ("geryon f32, scalar", "geryon-f32", {"GERYON_SIMD": "scalar"}),
        ("geryon f32", "geryon-f32", {}),
        ("geryon sq8", "geryon-sq8", {}),
        ("geryon hnsw", "geryon-hnsw", {}),
    ]
    try:
        import numpy  # noqa: F401
        plan.append(("numpy f32 (reference)", "numpy", {}))
    except ImportError:
        print("numpy not installed, skipping the reference row\n")

    results = [(label, run_child(impl, args, env)) for label, impl, env in plan]
    truth = results[0][1]["tops"]   # scalar float32 answer is the exact one

    print(f"{'':<24}{'backend':<16}{'ms/query':>10}{'index MB':>10}{'recall@5':>10}")
    for label, r in results:
        hits = sum(len(set(a) & set(b)) for a, b in zip(r["tops"], truth))
        recall = hits / (TOP_K * len(truth))
        note = f"   (index build {r['build_s']:.1f} s)" if "build_s" in r else ""
        print(f"{label:<24}{r['backend']:<16}{r['ms']:>10.2f}{r['index_mb']:>10.1f}{recall:>10.2f}{note}")


if __name__ == "__main__":
    main()
