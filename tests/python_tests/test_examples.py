"""The offline quickstart has to keep working, it is the first thing people run."""

import os
import subprocess
import sys

import pytest

import geryon as ca

pytestmark = pytest.mark.skipif(
    not ca._BACKEND_AVAILABLE, reason="native module is not built"
)

EXAMPLE = os.path.join(os.path.dirname(__file__), "..", "..", "examples", "quickstart.py")


def test_quickstart_runs_and_prints_the_expected_answers():
    env = dict(os.environ)
    env["PYTHONPATH"] = os.path.dirname(os.path.dirname(ca.__file__))
    env.pop("GERYON_VERBOSE", None)
    done = subprocess.run(
        [sys.executable, EXAMPLE], env=env, check=True, capture_output=True, text=True,
    )
    lines = {l.split(":", 1)[0].strip(): l.split(":", 1)[1].strip()
             for l in done.stdout.splitlines() if ":" in l}
    assert lines["answer"] == "4"
    assert lines["closest"] == "cats purr"
    assert "word_count" in lines["tools"]
    assert lines["kernel"] in ("avx2", "neon", "scalar")


README = os.path.join(os.path.dirname(__file__), "..", "..", "README.md")


def test_readme_quickstart_runs():
    text = open(README, encoding="utf-8").read()
    start = text.index("```python") + len("```python")
    code = text[start:text.index("```", start)]
    env = dict(os.environ)
    env["PYTHONPATH"] = os.path.dirname(os.path.dirname(ca.__file__))
    env.pop("GERYON_VERBOSE", None)
    done = subprocess.run(
        [sys.executable, "-c", code], env=env, check=True, capture_output=True, text=True,
    )
    assert done.stdout.split() == ["4", "cats", "purr"]
