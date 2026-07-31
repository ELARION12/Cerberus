"""
CoreAgent — Bare-Metal AI Agent Runtime (v1.4.0)
C + C++ + Python
"""

try:
    from ._coreagent_cpp import (
        Agent         as _CppAgent,
        AgentConfig,
        AgentState,
        ToolInput,
        ToolOutput,
        ToolRegistry,
        DynamicToolRegistry,
        ContextBuffer,
        MessageRole,
        ParsedKind,
        ParsedResult,
        OutputParser,
        VectorBuffer,
        VectorMatch,
        KVCacheArena,
        PageBlock,
        Telemetry,
        TelemetryKind,
        SwarmResult,
        SwarmSummary,
        version,
        state_name,
    )
    _BACKEND_AVAILABLE = True
except ImportError as e:
    _BACKEND_AVAILABLE = False
    _IMPORT_ERROR = str(e)
    VectorBuffer = None
    VectorMatch = None
    KVCacheArena = None
    PageBlock = None
    Telemetry = None
    TelemetryKind = None
    SwarmResult = None
    SwarmSummary = None

from .agent import Agent
from .shared_memory import SharedMemory
from .tools import (
    shell_tool,
    read_file_tool,
    write_file_tool,
    http_get_tool,
)

__version__ = "1.4.0"
__author__  = "CoreAgent Contributors"

__all__ = [
    "Agent",
    "AgentConfig",
    "AgentState",
    "ToolInput",
    "ToolOutput",
    "ToolRegistry",
    "DynamicToolRegistry",
    "ContextBuffer",
    "MessageRole",
    "ParsedKind",
    "ParsedResult",
    "OutputParser",
    "SharedMemory",
    "VectorBuffer",
    "VectorMatch",
    "KVCacheArena",
    "PageBlock",
    "Telemetry",
    "TelemetryKind",
    "SwarmResult",
    "SwarmSummary",
    "shell_tool",
    "read_file_tool",
    "write_file_tool",
    "http_get_tool",
    "version",
    "state_name",
]