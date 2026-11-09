"""
The library prints nothing unless asked to. Each case runs in its own process
because the native side reads GERYON_VERBOSE once.
"""

import os
import subprocess
import sys

import pytest

import geryon as ca

pytestmark = pytest.mark.skipif(
    not ca._BACKEND_AVAILABLE, reason="native module is not built"
)

SCRIPT = """
import geryon as ca
{setup}
agent = ca.Agent(name="Probe", num_threads=1)
agent.run("list files in the current directory")
"""


def run(setup="", verbose_env=None):
    env = dict(os.environ)
    env["PYTHONPATH"] = os.path.dirname(os.path.dirname(ca.__file__))
    env.pop("GERYON_VERBOSE", None)
    if verbose_env is not None:
        env["GERYON_VERBOSE"] = verbose_env
    done = subprocess.run(
        [sys.executable, "-c", SCRIPT.format(setup=setup)],
        env=env, check=True, capture_output=True, text=True,
    )
    return done.stdout


def test_silent_by_default():
    assert run() == ""


def test_environment_variable_turns_it_on():
    assert "Agent initialized" in run(verbose_env="1")


def test_zero_in_the_environment_keeps_it_off():
    assert run(verbose_env="0") == ""


def test_set_verbose_turns_it_on():
    assert "Agent initialized" in run(setup="ca.set_verbose(True)")


def test_verbose_reports_the_current_setting():
    before = ca.verbose()
    try:
        ca.set_verbose(True)
        assert ca.verbose() is True
        ca.set_verbose(False)
        assert ca.verbose() is False
    finally:
        ca.set_verbose(before)
