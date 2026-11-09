"""
Built-in Python-side tools for Geryon.
"""

import subprocess
import os
import urllib.request
import urllib.error
from urllib.parse import urlparse

MAX_HTTP_BYTES = 1_000_000

try:
    from ._geryon_core import ToolInput, ToolOutput
except ImportError:
    class ToolInput:
        name: str
        args: str
    class ToolOutput:
        result: str
        success: bool
        error: str
        elapsed_ms: int


def shell_tool(input: ToolInput) -> ToolOutput:
    """
    Execute a shell command safely.
    args = shell command string
    """
    out = ToolOutput()
    try:
        result = subprocess.run(
            input.args,
            shell=True,
            capture_output=True,
            text=True,
            timeout=10
        )
        out.result     = result.stdout
        out.success    = result.returncode == 0
        out.error      = result.stderr if result.returncode != 0 else ""
        out.elapsed_ms = 0
    except subprocess.TimeoutExpired:
        out.result     = ""
        out.success    = False
        out.error      = "Timeout expired"
        out.elapsed_ms = 10000
    except Exception as e:
        out.result  = ""
        out.success = False
        out.error   = str(e)
    return out


def read_file_tool(input: ToolInput) -> ToolOutput:
    """
    Read a file from disk.
    args = file path
    """
    out = ToolOutput()
    try:
        with open(input.args.strip(), "r") as f:
            out.result  = f.read()
            out.success = True
            out.error   = ""
    except Exception as e:
        out.result  = ""
        out.success = False
        out.error   = str(e)
    return out


def write_file_tool(input: ToolInput) -> ToolOutput:
    """
    Write content to a file.
    args = 'path|content'
    """
    out = ToolOutput()
    try:
        sep = input.args.find("|")
        if sep == -1:
            out.success = False
            out.error   = "Format must be 'path|content'"
            return out

        path    = input.args[:sep]
        content = input.args[sep + 1:]

        os.makedirs(os.path.dirname(os.path.abspath(path)),
                    exist_ok=True)

        with open(path, "w") as f:
            f.write(content)

        out.result  = f"Written {len(content)} bytes → {path}"
        out.success = True
        out.error   = ""
    except Exception as e:
        out.result  = ""
        out.success = False
        out.error   = str(e)
    return out


def http_get_tool(input: ToolInput) -> ToolOutput:
    """
    Perform an HTTP GET request.
    args = URL (http or https only; at most 1 MB of the response is returned)
    """
    out = ToolOutput()
    url = input.args.strip()
    if urlparse(url).scheme not in ("http", "https"):
        out.result  = ""
        out.success = False
        out.error   = "Only http and https URLs are allowed"
        return out
    try:
        with urllib.request.urlopen(url, timeout=10) as resp:
            out.result  = resp.read(MAX_HTTP_BYTES).decode("utf-8", errors="replace")
            out.success = True
            out.error   = ""
    except urllib.error.URLError as e:
        out.result  = ""
        out.success = False
        out.error   = f"HTTP error: {e}"
    except Exception as e:
        out.result  = ""
        out.success = False
        out.error   = str(e)
    return out