"""
Cerberus AI — Bare-Metal Agent Engine (v1.4.0)
3-Headed Architecture: C11 + C++17 + Python3
"""

try:
    from ._cerberus_core import (
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
        simd_backend,
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
    simd_backend = None

from ._log import set_verbose, verbose
from .agent import Agent
from .shared_memory import SharedMemory
from .tools import (
    shell_tool,
    read_file_tool,
    write_file_tool,
    http_get_tool,
)

__version__ = "1.4.0"
__author__  = "Arun Kumar"

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
    "simd_backend",
    "set_verbose",
    "verbose",
]