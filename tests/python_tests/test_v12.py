"""
V1.2.0 Feature Tests (Pillars 1, 2, 3)
"""

from geryon import (
    Agent, SharedMemory, DynamicToolRegistry,
    OutputParser, ParsedKind, ToolInput, ToolOutput
)


def test_v12_shared_memory():
    shm = SharedMemory()
    shm.write("task_id", "42", by="agent_1")
    shm.write("status", "in_progress", by="agent_2")

    assert shm.count() == 2
    assert shm.read("task_id") == "42"
    assert shm.read("status") == "in_progress"

    summary = shm.summary()
    assert "task_id=42" in summary
    assert "status=in_progress" in summary
    print("\n✅ V1.2.0 SharedMemory PASSED")


def test_v12_agent_shared_memory_integration():
    agent = Agent(name="MemoryBot")
    shm = SharedMemory()
    shm.write("current_phase", "V1.2.0", by="orchestrator")

    agent.join_team(shm)
    agent.remember("bot_state", "ready")

    assert shm.read("bot_state") == "ready"
    assert agent.recall("current_phase") == "V1.2.0"
    print("\n✅ V1.2.0 Agent SharedMemory Integration PASSED")


def test_v12_dynamic_tool_registry():
    reg = DynamicToolRegistry()
    assert reg.server_count() == 0

    def mock_mcp_tool(inp: ToolInput) -> ToolOutput:
        out = ToolOutput()
        out.result = f"mcp_res:{inp.args}"
        out.success = True
        return out

    _ = {
        "name": "mcp_search",
        "description": "Search remote database",
        "server_id": "db_server",
        "required_args": ["query"]
    }

    # Register via dynamic registry
    reg.register_tool("mcp_search", "Search remote database", mock_mcp_tool)
    assert reg.has_tool("mcp_search")
    assert reg.size() == 1

    reg.remove_tool("mcp_search")
    assert not reg.has_tool("mcp_search")
    assert reg.size() == 0
    print("\n✅ V1.2.0 DynamicToolRegistry PASSED")


def test_v12_output_parser():
    assert OutputParser.kind_name(ParsedKind.JSON_OBJECT) == "JSON_OBJECT"
    assert OutputParser.kind_name(ParsedKind.PLAIN_TEXT) == "PLAIN_TEXT"
    print("\n✅ V1.2.0 OutputParser bindings PASSED")
