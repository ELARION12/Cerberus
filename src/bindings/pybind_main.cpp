#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "Agent.hpp"
#include "AgentState.hpp"
#include "ToolRegistry.hpp"
#include "DynamicToolRegistry.hpp"
#include "ContextBuffer.hpp"
#include "OutputParser.hpp"
#include "SharedMemory.hpp"
#include "VectorBuffer.hpp"
#include "SQ8Quantizer.hpp"
#include "HNSWIndex.hpp"
#include "KVCacheArena.hpp"
#include "Telemetry.hpp"
#include "SwarmOrchestrator.hpp"

namespace py = pybind11;
using namespace coreagent;

PYBIND11_MODULE(_coreagent_cpp, m) {
    /* ── State & Role Enums ──────────────────────────────────────────────── */
    py::enum_<AgentState>(m, "AgentState")
        .value("IDLE",       AgentState::IDLE)
        .value("THINKING",   AgentState::THINKING)
        .value("PLANNING",   AgentState::PLANNING)
        .value("ACTING",     AgentState::ACTING)
        .value("REFLECTING", AgentState::REFLECTING)
        .value("DONE",       AgentState::DONE)
        .value("ERROR",      AgentState::ERROR)
        .export_values();

    py::enum_<MessageRole>(m, "MessageRole")
        .value("SYSTEM",    MessageRole::SYSTEM)
        .value("USER",      MessageRole::USER)
        .value("ASSISTANT", MessageRole::ASSISTANT)
        .value("TOOL",      MessageRole::TOOL)
        .export_values();

    py::enum_<ParsedKind>(m, "ParsedKind")
        .value("JSON_OBJECT", ParsedKind::JSON_OBJECT)
        .value("JSON_ARRAY",  ParsedKind::JSON_ARRAY)
        .value("SCALAR",      ParsedKind::SCALAR)
        .value("PLAIN_TEXT",  ParsedKind::PLAIN_TEXT)
        .export_values();

    /* ── ParsedResult & OutputParser ────────────────────────────────────── */
    py::class_<ParsedResult>(m, "ParsedResult")
        .def(py::init<>())
        .def_property_readonly("kind", [](const ParsedResult& r) { return r.kind; })
        .def_property_readonly("raw", [](const ParsedResult& r) { return std::string(r.raw); })
        .def_property_readonly("extracted", [](const ParsedResult& r) { return std::string(r.extracted); });

    py::class_<OutputParser>(m, "OutputParser")
        .def_static("kind_name", &OutputParser::kindName);

    /* ── Tools ───────────────────────────────────────────────────────────── */
    py::class_<ToolInput>(m, "ToolInput")
        .def(py::init<>())
        .def_readwrite("name", &ToolInput::name)
        .def_readwrite("args", &ToolInput::args)
        .def("__repr__", [](const ToolInput& t) {
            return "<ToolInput name='" + t.name + "' args='" + t.args + "'>";
        });

    py::class_<ToolOutput>(m, "ToolOutput")
        .def(py::init<>())
        .def_readwrite("result",     &ToolOutput::result)
        .def_readwrite("success",    &ToolOutput::success)
        .def_readwrite("error",      &ToolOutput::error)
        .def_readwrite("elapsed_ms", &ToolOutput::elapsed_ms)
        .def("__repr__", [](const ToolOutput& o) {
            return "<ToolOutput success=" +
                   std::string(o.success ? "True" : "False") +
                   " result='" + o.result.substr(0, 40) + "'>";
        });

    py::class_<ToolRegistry>(m, "ToolRegistry")
        .def(py::init<>())
        .def("register_tool",
            [](ToolRegistry& reg,
               const std::string& name,
               const std::string& desc,
               py::function py_fn) {
                reg.registerTool(name, desc,
                    [py_fn](const ToolInput& input) -> ToolOutput {
                        py::gil_scoped_acquire acquire;
                        try {
                            py::object result = py_fn(input);
                            return result.cast<ToolOutput>();
                        } catch (const py::error_already_set& e) {
                            return ToolOutput{"", false, std::string("Python error: ") + e.what(), 0};
                        }
                    });
            },
            py::arg("name"), py::arg("description"), py::arg("fn")
        )
        .def("remove_tool", &ToolRegistry::removeTool)
        .def("dispatch",    &ToolRegistry::dispatch)
        .def("has_tool",    &ToolRegistry::hasTool)
        .def("tool_names",  &ToolRegistry::toolNames)
        .def("size",        &ToolRegistry::size)
        .def("print_all",   &ToolRegistry::printAll);

    py::class_<MCPToolSchema>(m, "MCPToolSchema")
        .def(py::init<>())
        .def_readwrite("name",          &MCPToolSchema::name)
        .def_readwrite("description",   &MCPToolSchema::description)
        .def_readwrite("server_id",     &MCPToolSchema::server_id)
        .def_readwrite("required_args", &MCPToolSchema::required_args);

    py::class_<DynamicToolRegistry, ToolRegistry>(m, "DynamicToolRegistry")
        .def(py::init<>())
        .def("register_mcp",
            [](DynamicToolRegistry& reg, const MCPToolSchema& schema, py::function py_fn) {
                reg.registerMCP(schema, [py_fn](const ToolInput& input) -> ToolOutput {
                    py::gil_scoped_acquire acquire;
                    try {
                        py::object result = py_fn(input);
                        return result.cast<ToolOutput>();
                    } catch (const py::error_already_set& e) {
                        return ToolOutput{"", false, std::string("Python error: ") + e.what(), 0};
                    }
                });
            })
        .def("teardown_server", &DynamicToolRegistry::teardownServer)
        .def("mcp_schemas",     &DynamicToolRegistry::mcpSchemas)
        .def("server_count",    &DynamicToolRegistry::serverCount);

    /* ── Shared Memory ──────────────────────────────────────────────────── */
    py::class_<SharedMemory>(m, "SharedMemory")
        .def(py::init<size_t>(), py::arg("capacity") = 2 * 1024 * 1024)
        .def("write",   &SharedMemory::write, py::arg("key"), py::arg("value"), py::arg("author") = "agent")
        .def("read",    &SharedMemory::read,  py::arg("key"), py::arg("default_val") = "")
        .def("summary", &SharedMemory::summary)
        .def("count",   &SharedMemory::count);

    /* ── Context Buffer ──────────────────────────────────────────────────── */
    py::class_<Message>(m, "Message")
        .def(py::init<>())
        .def_readwrite("role",         &Message::role)
        .def_readwrite("content",      &Message::content)
        .def_readwrite("tool_name",    &Message::tool_name)
        .def_readwrite("timestamp_ms", &Message::timestamp_ms);

    py::class_<ContextBuffer>(m, "ContextBuffer")
        .def(py::init<size_t, size_t, size_t>(),
             py::arg("arena_capacity"), py::arg("scratch_capacity"), py::arg("max_tokens") = 4096)
        .def("add_system",      &ContextBuffer::addSystem)
        .def("add_user",        &ContextBuffer::addUser)
        .def("add_assistant",   &ContextBuffer::addAssistant)
        .def("add_tool_result", &ContextBuffer::addToolResult)
        .def("build_prompt", [](ContextBuffer& cb) -> py::memoryview {
            std::string_view sv = cb.buildPrompt();
            return py::memoryview::from_memory(const_cast<char*>(sv.data()), sv.size(), true);
        })
        .def("messages",            &ContextBuffer::messages)
        .def("message_count",       &ContextBuffer::messageCount)
        .def("token_count",         &ContextBuffer::tokenCount)
        .def("retained_bytes_used", &ContextBuffer::retainedBytesUsed)
        .def("clear",               &ContextBuffer::clear)
        .def("print_summary",       &ContextBuffer::printSummary);

    py::enum_<ca_event_kind_t>(m, "TelemetryKind")
        .value("NONE",        CA_EVT_NONE)
        .value("THINK",       CA_EVT_THINK)
        .value("PLAN",        CA_EVT_PLAN)
        .value("ACT",         CA_EVT_ACT)
        .value("REFLECT",     CA_EVT_REFLECT)
        .value("TOOL_CALL",   CA_EVT_TOOL_CALL)
        .value("TOOL_OK",     CA_EVT_TOOL_OK)
        .value("TOOL_ERR",    CA_EVT_TOOL_ERR)
        .value("SWARM_SPAWN", CA_EVT_SWARM_SPAWN)
        .value("SWARM_JOIN",  CA_EVT_SWARM_JOIN)
        .value("VECTOR_PUSH", CA_EVT_VECTOR_PUSH)
        .value("VECTOR_QUERY",CA_EVT_VECTOR_QUERY)
        .value("CONTEXT_GC",  CA_EVT_CONTEXT_GC)
        .value("AGENT_DONE",  CA_EVT_AGENT_DONE)
        .value("CUSTOM",      CA_EVT_CUSTOM)
        .export_values();

    /* ── V1.4.0 KVCacheArena (Zero-Copy Paged KV-Cache Memory) ──────────── */
    py::class_<PageBlock, std::shared_ptr<PageBlock>>(m, "PageBlock")
        .def_readonly("block_id",    &PageBlock::block_id)
        .def_readonly("page_count",  &PageBlock::page_count)
        .def_readonly("size_bytes",  &PageBlock::size_bytes)
        .def_readonly("shm_name",    &PageBlock::shm_name)
        .def_readonly("prefix_hash", &PageBlock::prefix_hash)
        .def("ref_count", [](const PageBlock& pb) { return pb.ref_count.load(); });

    py::class_<KVCacheArena>(m, "KVCacheArena")
        .def(py::init<size_t, std::string>(), py::arg("capacity_pages") = 1024, py::arg("shm_prefix") = "/ironagent_kv_")
        .def("allocate_block", &KVCacheArena::allocateBlock, py::arg("token_prefix"), py::arg("min_bytes"))
        .def("lookup_prefix",  &KVCacheArena::lookupPrefix,  py::arg("token_prefix"))
        .def("release_block",  &KVCacheArena::releaseBlock,  py::arg("block_id"))
        .def_static("hash_prefix", &KVCacheArena::hashPrefix, py::arg("tokens"))
        .def("capacity_pages", &KVCacheArena::capacityPages)
        .def("used_pages",     &KVCacheArena::usedPages)
        .def("active_blocks",  &KVCacheArena::activeBlocks)
        .def("bytes_used",     &KVCacheArena::bytesUsed);

    /* ── V1.4.0 VectorBuffer & SQ8 / HNSW Upgrades ───────────────────────── */
    py::class_<VectorMatch>(m, "VectorMatch")
        .def(py::init<>())
        .def_readwrite("index", &VectorMatch::index)
        .def_readwrite("score", &VectorMatch::score)
        .def_readwrite("text",  &VectorMatch::text)
        .def("__repr__", [](const VectorMatch& vm) {
            return "<VectorMatch idx=" + std::to_string(vm.index) +
                   " score=" + std::to_string(vm.score).substr(0, 5) +
                   " text='" + vm.text.substr(0, 30) + "'>";
        });

    py::class_<VectorBuffer>(m, "VectorBuffer")
        .def(py::init<uint32_t, uint32_t>(), py::arg("dim"), py::arg("max_vectors") = 4096)
        .def("enable_sq8",         &VectorBuffer::enableSQ8, py::arg("enable") = true)
        .def("build_hnsw_index",   &VectorBuffer::buildHNSWIndex, py::arg("M") = 16, py::arg("ef_construction") = 64, py::arg("ef_search") = 32)
        .def("has_hnsw",           &VectorBuffer::hasHNSW)
        .def("is_sq8_enabled",     &VectorBuffer::isSQ8Enabled)
        .def("push",               &VectorBuffer::push, py::arg("embedding"), py::arg("text"))
        .def("query",              &VectorBuffer::query, py::arg("embedding"), py::arg("top_k") = 5)
        .def("rag_context",        &VectorBuffer::ragContext, py::arg("query_vec"), py::arg("top_k") = 3)
        .def("size",               &VectorBuffer::size)
        .def("dim",                &VectorBuffer::dim)
        .def("empty",              &VectorBuffer::empty)
        .def("clear",              &VectorBuffer::clear)
        .def("text",               &VectorBuffer::text)
        .def("bytes_used",         &VectorBuffer::bytesUsed)
        .def("print_stats",        &VectorBuffer::printStats);

    /* ── V1.3.0 Telemetry (Lockless Ring Buffer) ─────────────────────────── */
    py::class_<Telemetry>(m, "Telemetry")
        .def(py::init<uint32_t>(), py::arg("capacity") = CA_RING_DEFAULT_CAP)
        .def("emit",                &Telemetry::emit, py::arg("kind"), py::arg("agent_id"), py::arg("label") = "")
        .def("available",           &Telemetry::available)
        .def("dropped",             &Telemetry::dropped)
        .def("print",               &Telemetry::print, py::arg("max_events") = 256)
        .def("export_otel_to_file", &Telemetry::exportOtelToFile, py::arg("path"))
        .def("reset",               &Telemetry::reset);

    /* ── V1.3.0 SwarmOrchestrator ────────────────────────────────────────── */
    py::class_<SwarmResult>(m, "SwarmResult")
        .def(py::init<>())
        .def_readwrite("worker_name", &SwarmResult::worker_name)
        .def_readwrite("output",      &SwarmResult::output)
        .def_readwrite("success",     &SwarmResult::success)
        .def_readwrite("error",       &SwarmResult::error)
        .def_readwrite("elapsed_ms",  &SwarmResult::elapsed_ms);

    py::class_<SwarmSummary>(m, "SwarmSummary")
        .def(py::init<>())
        .def_readwrite("results",          &SwarmSummary::results)
        .def_readwrite("succeeded",        &SwarmSummary::succeeded)
        .def_readwrite("failed",           &SwarmSummary::failed)
        .def_readwrite("total_elapsed_ms", &SwarmSummary::total_elapsed_ms)
        .def("combined",                   &SwarmSummary::combined);

    /* ── Agent & Config ──────────────────────────────────────────────────── */
    py::class_<AgentConfig>(m, "AgentConfig")
        .def(py::init<>())
        .def_readwrite("name",                     &AgentConfig::name)
        .def_readwrite("system_prompt",            &AgentConfig::system_prompt)
        .def_readwrite("max_tokens",               &AgentConfig::max_tokens)
        .def_readwrite("num_threads",              &AgentConfig::num_threads)
        .def_readwrite("max_steps",                &AgentConfig::max_steps)
        .def_readwrite("tool_timeout",             &AgentConfig::tool_timeout)
        .def_readwrite("context_arena_capacity",   &AgentConfig::context_arena_capacity)
        .def_readwrite("context_scratch_capacity", &AgentConfig::context_scratch_capacity);

    py::class_<Agent>(m, "Agent")
        .def(py::init<const AgentConfig&>(), py::arg("config") = AgentConfig{})
        .def("run", &Agent::run, py::arg("task"), py::call_guard<py::gil_scoped_release>())
        .def("register_tool",
            [](Agent& agent, const std::string& name, const std::string& desc, py::function py_fn) {
                agent.registerTool(name, desc, [py_fn](const ToolInput& input) -> ToolOutput {
                    py::gil_scoped_acquire acquire;
                    try {
                        py::object result = py_fn(input);
                        return result.cast<ToolOutput>();
                    } catch (const py::error_already_set& e) {
                        return ToolOutput{"", false, std::string("Python error: ") + e.what(), 0};
                    }
                });
            },
            py::arg("name"), py::arg("description"), py::arg("fn")
        )
        .def("state",             &Agent::state)
        .def("state_name",        &Agent::stateName)
        .def("name",              &Agent::name)
        .def("set_shared_memory", &Agent::setSharedMemory)
        .def("context", [](Agent& a) -> ContextBuffer& { return a.context(); }, py::return_value_policy::reference_internal)
        .def("tools",   [](Agent& a) -> DynamicToolRegistry& { return a.tools(); }, py::return_value_policy::reference_internal)
        .def("__repr__", [](const Agent& a) {
            return "<CoreAgent name='" + a.name() + "' state=" + a.stateName() + ">";
        });

    m.def("version", []() { return "1.4.0"; });
    m.def("state_name", [](AgentState s) { return agentStateToString(s); });
}