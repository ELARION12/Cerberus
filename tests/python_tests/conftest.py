"""
Use the installed cerberus_agent when there is one (a wheel, pip install -e .).
In a plain source checkout fall back to ./python, where the CMake build puts
the native module.
"""

import os
import sys

try:
    import cerberus_agent  # noqa: F401
except ImportError:
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "python"))
