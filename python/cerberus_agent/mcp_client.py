"""
Bridge between Cerberus's synchronous tool interface
and MCP servers, which communicate via async JSON-RPC over stdio.
Runs a background asyncio event loop and exposes blocking synchronous methods.
"""

import asyncio
import threading
from contextlib import AsyncExitStack
from typing import Any, Dict, List, Optional

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


class MCPError(Exception):
    """Raised when an MCP server call fails or the bridge isn't connected."""
    pass


class MCPToolBridge:
    """
    Synchronous wrapper around an async MCP session.

    Launches a background thread that owns the asyncio event loop and the
    MCP stdio process. All public methods are blocking and thread-safe;
    they submit coroutines to the background loop via run_coroutine_threadsafe.
    """

    def __init__(
        self,
        command: str,
        args: Optional[List[str]] = None,
        env: Optional[Dict[str, str]] = None,
    ):
        self.command = command
        self.args = args or []
        self.env = env

        self.on_schema_change = None

        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._thread: Optional[threading.Thread] = None
        self._session: Optional[ClientSession] = None
        self._exit_stack: Optional[AsyncExitStack] = None
        self._stop_event: Optional[asyncio.Event] = None
        self._ready = threading.Event()
        self._connect_error: Optional[BaseException] = None

    def connect(self, timeout: float = 20.0) -> None:
        """Start the background event loop and open the MCP session."""
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()
        # Block until the background loop signals it is ready (or until timeout)
        if not self._ready.wait(timeout=timeout):
            raise MCPError(
                f"Timed out connecting to MCP server: {self.command} {' '.join(self.args)}"
            )
        if self._connect_error:
            raise MCPError(f"Failed to connect to MCP server: {self._connect_error}") from self._connect_error

    def _run_loop(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        self._loop.run_until_complete(self._connect_and_serve())

    async def _connect_and_serve(self):
        try:
            server_params = StdioServerParameters(
                command=self.command, args=self.args, env=self.env
            )
            self._exit_stack = AsyncExitStack()
            read, write = await self._exit_stack.enter_async_context(stdio_client(server_params))
            self._session = await self._exit_stack.enter_async_context(ClientSession(read, write))
            await self._session.initialize()
        except BaseException as e:
            self._connect_error = e
            self._ready.set()
            return

        self._ready.set()
        self._stop_event = asyncio.Event()
        try:
            await self._stop_event.wait()  # keep the loop/session alive
        finally:
            await self._exit_stack.aclose()

    def close(self) -> None:
        """Tear down the session and stop the background loop."""
        if self._loop and self._stop_event:
            fut = asyncio.run_coroutine_threadsafe(self._signal_stop(), self._loop)
            try:
                fut.result(timeout=5)
            except Exception:
                pass
        if self._thread:
            self._thread.join(timeout=5)

    async def _signal_stop(self):
        self._stop_event.set()

    def list_tools(self) -> List[Dict[str, Any]]:
        """Returns [{'name':..., 'description':..., 'input_schema':...}, ...]"""
        result = self._run_coro(self._session.list_tools())
        return [
            {
                "name": t.name,
                "description": t.description or "",
                "input_schema": t.inputSchema,
            }
            for t in result.tools
        ]

    def call_tool(self, name: str, arguments: Dict[str, Any]) -> str:
        """Call an MCP tool synchronously, returns its text content."""
        result = self._run_coro(self._session.call_tool(name, arguments))
        texts = [c.text for c in result.content if getattr(c, "type", None) == "text"]
        return "\n".join(texts) if texts else str(result.content)

    def _run_coro(self, coro):
        """Submit a coroutine to the background event loop and block for the result."""
        if not self._loop or not self._session:
            raise MCPError("Not connected - call connect() first")
        fut = asyncio.run_coroutine_threadsafe(coro, self._loop)
        return fut.result(timeout=30)


if __name__ == "__main__":
    import os
    import sys

    project_dir = os.getcwd()
    server_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mcp_filesystem_server.py")

    bridge = MCPToolBridge(
        command=sys.executable,
        args=[server_script, project_dir],
    )
    print(f"Connecting to filesystem MCP server, scoped to: {project_dir}")
    bridge.connect()
    print("Connected. Discovering tools...\n")
    for t in bridge.list_tools():
        print(f" - {t['name']}: {t['description']}")
    bridge.close()
    print("\nDisconnected cleanly.")
