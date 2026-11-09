"""
Progress output switch. The library is quiet by default; set GERYON_VERBOSE=1
or call geryon.set_verbose(True) to see what the agent is doing.
"""

import os

_verbose = os.environ.get("GERYON_VERBOSE", "") not in ("", "0")


def set_verbose(on=True):
    global _verbose
    _verbose = bool(on)
    try:
        from . import _geryon_core
        _geryon_core.set_verbose(_verbose)
    except ImportError:
        pass


def verbose():
    return _verbose


def log(*args):
    if _verbose:
        print(*args)
