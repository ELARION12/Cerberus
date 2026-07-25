"""
Minimal pure-Python MCP server exposing filesystem tools,
scoped to a single root directory. Runs over stdio.
"""

import os
import sys
from pathlib import Path

from mcp.server.fastmcp import FastMCP

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd()

mcp = FastMCP("CoreAgent Filesystem Server")


def _resolve(path: str) -> Path:
    """Resolve a user-supplied path against ROOT, and refuse to
    escape it (basic path traversal protection)."""
    target = (ROOT / path).resolve()
    if target != ROOT and ROOT not in target.parents:
        raise ValueError(f"Path '{path}' is outside the allowed root: {ROOT}")
    return target


@mcp.tool()
def list_directory(path: str = ".") -> str:
    """List files and folders inside a directory, relative to the server root."""
    target = _resolve(path)
    if not target.is_dir():
        return f"Error: '{path}' is not a directory"
    entries = sorted(os.listdir(target))
    return "\n".join(entries) if entries else "(empty directory)"


@mcp.tool()
def read_file(path: str) -> str:
    """Read the contents of a text file, relative to the server root."""
    target = _resolve(path)
    if not target.is_file():
        return f"Error: '{path}' is not a file"
    try:
        return target.read_text(errors="replace")
    except Exception as e:
        return f"Error reading '{path}': {e}"


@mcp.tool()
def write_file(path: str, content: str) -> str:
    """Write text content to a file, relative to the server root."""
    target = _resolve(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content)
    return f"Wrote {len(content)} bytes to {path}"


if __name__ == "__main__":
    mcp.run()
