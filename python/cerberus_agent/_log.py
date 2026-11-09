"""
Progress output switch. The library is quiet by default; set CERBERUS_VERBOSE=1
or call cerberus_agent.set_verbose(True) to see what the agent is doing.
"""

import os

_verbose = os.environ.get("CERBERUS_VERBOSE", "") not in ("", "0")


def set_verbose(on=True):
    global _verbose
    _verbose = bool(on)
    try:
        from . import _cerberus_core
        _cerberus_core.set_verbose(_verbose)
    except ImportError:
        pass


def verbose():
    return _verbose


def log(*args):
    if _verbose:
        print(*args)
