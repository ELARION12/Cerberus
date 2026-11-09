"""
The vector kernels (AVX2 / NEON) must give the same answers as the scalar code.

The kernel is chosen once per process, so each run happens in its own
subprocess, once with the default choice and once with CERBERUS_SIMD=scalar.
"""

import json
import os
import subprocess
import sys

import pytest

import cerberus_agent as ca

pytestmark = pytest.mark.skipif(
    not ca._BACKEND_AVAILABLE, reason="native module is not built"
)

SCRIPT = r"""
import json, random, sys
import cerberus_agent as ca

random.seed(7)
out = {"backend": ca.simd_backend(), "runs": []}
# sizes chosen to hit the 8-wide and 4-wide blocks and the scalar tail
for dim in (3, 8, 13, 64, 100, 384):
    for sq8 in (False, True):
        vb = ca.VectorBuffer(dim=dim, max_vectors=64)
        if sq8:
            vb.enable_sq8(True)
        for i in range(40):
            vb.push([random.uniform(-1, 1) for _ in range(dim)], "t%d" % i)
        q = [random.uniform(-1, 1) for _ in range(dim)]
        out["runs"].append([[m.index, m.score] for m in vb.query(q, top_k=5)])
with open(sys.argv[1], "w") as f:
    json.dump(out, f)
"""


def run_in_subprocess(tmp_path, simd=None):
    env = dict(os.environ)
    env["PYTHONPATH"] = os.path.dirname(os.path.dirname(ca.__file__))
    env.pop("CERBERUS_SIMD", None)
    if simd:
        env["CERBERUS_SIMD"] = simd
    out_file = tmp_path / f"out_{simd or 'default'}.json"
    subprocess.run(
        [sys.executable, "-c", SCRIPT, str(out_file)],
        env=env, check=True, capture_output=True,
    )
    return json.loads(out_file.read_text())


def test_backend_is_reported():
    assert ca.simd_backend() in ("avx2", "neon", "scalar")


def test_scalar_can_be_forced(tmp_path):
    assert run_in_subprocess(tmp_path, "scalar")["backend"] == "scalar"


def test_kernels_match_scalar(tmp_path):
    fast = run_in_subprocess(tmp_path)
    slow = run_in_subprocess(tmp_path, "scalar")
    assert len(fast["runs"]) == len(slow["runs"])
    for a, b in zip(fast["runs"], slow["runs"]):
        assert len(a) == len(b) == 5
        for (_, score_a), (_, score_b) in zip(a, b):
            assert abs(score_a - score_b) < 1e-4
        # the best hit must be the same one unless the top two are nearly tied
        if a[0][1] - a[1][1] > 1e-3:
            assert a[0][0] == b[0][0]


def test_python_and_native_versions_agree():
    assert ca.__version__ == ca.version()
