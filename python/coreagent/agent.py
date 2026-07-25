from __future__ import annotations
import os
import json
import sys
import time
from dataclasses import dataclass, field
from typing import Optional, Callable, List, Dict, Any, TYPE_CHECKING

if TYPE_CHECKING:
    from .llm import OllamaClient
    from .mcp_client import MCPToolBridge
    from .shared_memory import SharedMemory

from ._coreagent_cpp import (
    Agent        as _CppAgent,
    AgentConfig,
    AgentState,
    ToolInput,
    ToolOutput,
)
from . import _coreagent_cpp as cpp
from .tools import (
    shell_tool,
    read_file_tool,
    write_file_tool,
    http_get_tool,
)


@dataclass
class AgentMessage:
    """A single message passed from one Agent to another."""
    sender:    str
    content:   str
    timestamp: float = field(default_factory=time.time)


class Agent:
    """
    CoreAgent — High-level Python interface to the C++ Agent engine.

    Example:
        agent = Agent(name="MyBot")
        result = agent.run("list files in current directory")
        print(result)
    """

    # Exact argument format per tool, shown to the LLM planner so it
    # doesn't have to guess syntax. Keep this in sync with the tools
    # actually registered in Agent.cpp's setupBuiltinTools() and
    # agent.py's _register_defaults().
    TOOL_SPECS = {
        "shell":         "Run a shell command. args = the raw command, e.g. 'ls -la'",
        "read_file":     "Read a file. args = a file path (must be a file, not a directory)",
        "write_file":    "Write a file. args = 'path|content' (path, pipe, then content)",
        "echo":          "Return args back as-is. Only for direct text answers, not actions.",
        "py_shell":      "Run a shell command via Python. args = the raw command, e.g. 'ls -la'",
        "py_read_file":  "Read a file via Python. args = a file path (must be a file, not a directory)",
        "py_write_file": "Write a file via Python. args = 'path|content'",
        "py_http_get":   "HTTP GET request. args = the URL",
    }

    def __init__(
        self,
        name:          str  = "CoreAgent",
        system_prompt: str  = "You are a fast, efficient AI agent.",
        max_tokens:    int  = 4096,
        num_threads:   int  = 4,
        max_steps:     int  = 10,
        tool_timeout:  int  = 10000,
        load_defaults: bool = True,
        config:        Optional[AgentConfig] = None,
    ):
        if config is None:
            config                = AgentConfig()
            config.name           = name
            config.system_prompt  = system_prompt
            config.max_tokens     = max_tokens
            config.num_threads    = num_threads
            config.max_steps      = max_steps
            config.tool_timeout   = tool_timeout
            config.context_arena_capacity = 1024 * 1024
            config.context_scratch_capacity = 1024 * 1024
        else:
            config.name = name

        self._agent = _CppAgent(config)
        self._llm   = None           # LLM client is loaded lazily on first use
        self._tool_specs = dict(self.TOOL_SPECS)  # grows as MCP tools are added
        self._mcp_bridges: List[Any] = []
        self.inbox: List[AgentMessage] = []       # agent-to-agent message queue
        self.workers: Dict[str, "Agent"] = {}     # name -> subordinate agent
        self.worker_roles: Dict[str, str] = {}    # name -> role description (shown to LLM)
        self.memory: Optional[Any] = None         # shared team blackboard, if joined

        if load_defaults:
            self._register_defaults()

    @property
    def llm(self) -> OllamaClient:
        """Lazily load and return the OllamaClient instance."""
        if self._llm is None:
            from .llm import OllamaClient
            self._llm = OllamaClient(model="qwen2.5:3b")
        return self._llm

    @llm.setter
    def llm(self, value: OllamaClient) -> None:
        self._llm = value

    def _register_defaults(self):
        """Register all built-in Python tools."""
        self.add_tool("py_shell",
            "Run shell command via Python subprocess",
            shell_tool)

        self.add_tool("py_read_file",
            "Read a file from disk via Python",
            read_file_tool)

        self.add_tool("py_write_file",
            "Write a file to disk. args='path|content'",
            write_file_tool)

        self.add_tool("py_http_get",
            "HTTP GET request. args=URL",
            http_get_tool)

    def add_tool(
        self,
        name:        str,
        description: str,
        fn:          Callable[[ToolInput], ToolOutput],
    ) -> "Agent":
        """
        Register a custom tool.

        Args:
            name:        Tool name (used by the planner)
            description: What this tool does
            fn:          Callable(ToolInput) -> ToolOutput

        Returns:
            self (for chaining)
        """
        self._agent.register_tool(name, description, fn)
        return self

    def send_message(self, to_agent: "Agent", content: str) -> None:
        """
        Send a message to another Agent's inbox. Also records the
        send in this agent's own context, so it remembers what it
        told the other agent.
        """
        msg = AgentMessage(sender=self.name, content=content)
        to_agent.inbox.append(msg)
        self.context.add_assistant(f"[sent to {to_agent.name}]: {content}")

    def broadcast(self, to_agents: List["Agent"], content: str) -> None:
        """Send the same message to multiple agents at once."""
        for other in to_agents:
            self.send_message(other, content)

    def receive_messages(self, clear: bool = True) -> List[AgentMessage]:
        """
        Return all pending inbox messages, and feed each into this
        agent's own context (as a user-role entry) so the next
        run()/run_llm() call actually sees them during planning.
        """
        msgs = list(self.inbox)
        for m in msgs:
            self.context.add_user(f"[message from {m.sender}]: {m.content}")
        if clear:
            self.inbox.clear()
        return msgs

    def join_team(self, shared_memory: SharedMemory) -> "Agent":
        """Attach this agent to a shared team blackboard."""
        self.memory = shared_memory
        if hasattr(self._agent, "set_shared_memory") and getattr(shared_memory, "raw_cpp", None) is not None:
            self._agent.set_shared_memory(shared_memory.raw_cpp)
        return self

    def remember(self, key: str, value: Any) -> None:
        """Write a fact to shared team memory. Requires join_team() first."""
        if self.memory is None:
            raise ValueError(f"{self.name} has not joined a shared memory - call join_team() first")
        self.memory.write(key, value, by=self.name)

    def recall(self, key: str, default: Any = None) -> Any:
        """Read a fact from shared team memory. Returns `default` if not joined or not found."""
        if self.memory is None:
            return default
        return self.memory.read(key, default)

    def add_worker(self, worker: "Agent", role: str = "") -> "Agent":
        """
        Register another Agent as a worker this agent can delegate to.
        If this agent has joined a shared memory, the worker joins the
        same one automatically, so results propagate between them.
        """
        self.workers[worker.name] = worker
        self.worker_roles[worker.name] = role or f"general-purpose agent named {worker.name}"
        if self.memory is not None and worker.memory is None:
            worker.join_team(self.memory)
        return self

    def delegate(self, task: str) -> str:
        """
        Break `task` into independent sub-tasks via the LLM, send each
        to its assigned worker (via send_message), run the
        worker, and combine all results into one summary.

        Sub-tasks are independent - workers can't see each other's
        results within a single delegate() call.
        """
        if not self.workers:
            raise ValueError(f"{self.name} has no workers registered - call add_worker() first")

        assignments = self.llm.delegate(task, self.worker_roles)
        if not assignments:
            print(f"[{self.name}] delegation produced no valid assignments, running task directly instead")
            return self.run_llm(task)

        results = []
        for a in assignments:
            worker = self.workers.get(a["worker"])
            if worker is None:
                print(f"[{self.name}] skipping unknown worker '{a['worker']}'")
                results.append(f"[skipped - unknown worker '{a['worker']}']")
                continue
            print(f"[{self.name} -> {worker.name}] {a['subtask']}")
            self.send_message(worker, a["subtask"])
            worker_result = worker.run_llm(a["subtask"])
            results.append(f"{worker.name}: {worker_result}")
            self.context.add_tool_result(worker.name, worker_result)
            if self.memory is not None:
                self.memory.write(worker.name, worker_result, by=worker.name)

        combined = "\n".join(results)
        self.context.add_assistant(f"[delegation complete] {combined}")
        return combined

    def add_mcp_server(self, bridge: MCPToolBridge, prefix: str = "mcp_") -> "Agent":
        """
        Connect to an MCP server and register each of its tools into
        this agent's ToolRegistry, prefixed to avoid clashing with the
        built-in tool names (e.g. 'read_file' -> 'mcp_read_file').
        """
        bridge.connect()
        self._mcp_bridges.append(bridge)
        for spec in bridge.list_tools():
            self._register_mcp_tool(bridge, spec, prefix)
        return self

    def connect_filesystem_mcp(self, root_dir: Optional[str] = None) -> "Agent":
        """
        Convenience: connect to CoreAgent's own pure-Python filesystem
        MCP server (mcp_filesystem_server.py), scoped to root_dir.
        """
        from .mcp_client import MCPToolBridge
        server_script = os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "mcp_filesystem_server.py"
        )
        root_dir = root_dir or os.getcwd()
        bridge = MCPToolBridge(command=sys.executable, args=[server_script, root_dir])
        return self.add_mcp_server(bridge, prefix="mcp_")

    def _register_mcp_tool(self, bridge: MCPToolBridge, spec: Dict[str, Any], prefix: str):
        """Wrap one MCP tool as a CoreAgent tool (ToolInput -> ToolOutput)."""
        mcp_name = spec["name"]
        full_name = f"{prefix}{mcp_name}"
        schema = spec.get("input_schema") or {}
        properties = list((schema.get("properties") or {}).keys())
        required = schema.get("required") or properties or ["input"]

        def wrapper(tool_input) -> ToolOutput:
            out = ToolOutput()
            try:
                args_str = tool_input.args
                if len(required) <= 1:
                    kwargs = {required[0]: args_str}
                elif len(required) == 2 and "|" in args_str:
                    a, b = args_str.split("|", 1)
                    kwargs = {required[0]: a, required[1]: b}
                else:
                    kwargs = json.loads(args_str)  # fallback: raw JSON args
                out.result = bridge.call_tool(mcp_name, kwargs)
                out.success = True
                out.error = ""
            except Exception as e:
                out.result = ""
                out.success = False
                out.error = str(e)
            out.elapsed_ms = 0
            return out

        if len(required) <= 1:
            args_hint = f"{required[0]} (plain string)"
        elif len(required) == 2:
            # Two-field tools use 'a|b' pipe syntax so the LLM doesn't need JSON
            args_hint = f"'{required[0]}|{required[1]}' (pipe-separated)"
        else:
            args_hint = f"JSON object with fields: {', '.join(required)}"
        description = spec.get("description", "") or "MCP tool"
        self._tool_specs[full_name] = f"{description}. args = {args_hint}"

        self.add_tool(full_name, description, wrapper)

    def run(self, task: str) -> str:
        """
        Run the agent on a task.

        Args:
            task: Natural language task description

        Returns:
            Final output string from the agent
        """
        if not task or not task.strip():
            raise ValueError("Task cannot be empty")
        return self._agent.run(task)

    def tool(self, name: str, description: str):
        """
        Decorator to register a function as a tool.

        Example:
            @agent.tool("greet", "Greet a person by name")
            def greet(input: ToolInput) -> ToolOutput:
                out = ToolOutput()
                out.result  = f"Hello, {input.args}!"
                out.success = True
                return out
        """
        def decorator(fn: Callable):
            self.add_tool(name, description, fn)
            return fn
        return decorator

    @property
    def name(self) -> str:
        return self._agent.name()

    @property
    def state(self) -> AgentState:
        return self._agent.state()

    @property
    def state_name(self) -> str:
        return self._agent.state_name()

    @property
    def context(self):
        return self._agent.context()

    @property
    def tools(self):
        return self._agent.tools()

    @property
    def tool_names(self) -> List[str]:
        return list(self._agent.tools().tool_names())

    def __repr__(self) -> str:
        return (f"<CoreAgent name='{self.name}' "
                f"state={self.state_name} "
                f"tools={len(self.tool_names)}>")

    def run_llm(self, task: str, max_steps: int = 5) -> str:
        """
        Same THINK->PLAN->ACT->REFLECT loop as run(), but Ollama does the
        planning instead of the C++ keyword planner. Only uses already-
        exposed primitives (context()/tools()) - no rebuild required.
        """
        ctx = self._agent.context()
        tools = self._agent.tools()

        # Drain the agent's inbox before planning so peer messages are visible in context
        self.receive_messages()

        # Surface shared team memory as a high-priority user message so the LLM
        # treats it as ground truth rather than reasoning from scratch
        if self.memory is not None:
            mem_summary = self.memory.summary()
            if mem_summary:
                ctx.add_user(
                    "[shared team memory - EXACT facts other agents found. "
                    "Copy these values verbatim if relevant, do not "
                    f"recompute or guess a different value]\n{mem_summary}"
                )

        ctx.add_user(task)
        final_output = ""

        active_specs = {
            name: self._tool_specs.get(name, "No description available.")
            for name in tools.tool_names()
        }

        for step in range(1, max_steps + 1):
            prompt = ctx.build_prompt()
            plan_str = self.llm.plan(
                task=task,
                tool_specs=active_specs,
                context=prompt,
            )
            print(f"[llm-plan step {step}] {plan_str}")

            sep = plan_str.find("|")
            tool_name = plan_str[:sep] if sep != -1 else plan_str
            tool_args = plan_str[sep + 1:] if sep != -1 else ""

            tool_input = cpp.ToolInput()
            tool_input.name = tool_name
            tool_input.args = tool_args

            output = tools.dispatch(tool_input)

            if output.success:
                ctx.add_tool_result(tool_name, output.result)
                final_output = output.result
                break
            # On failure, feed the error back into context so the LLM can recover
            ctx.add_tool_result(tool_name, f"ERROR: {output.error}")
        else:
            final_output = "Max steps reached without completion."

        return final_output