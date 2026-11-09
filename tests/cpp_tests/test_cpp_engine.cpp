#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <atomic>
#include <cctype>
#include "Arena.hpp"
#include "ThreadPool.hpp"
#include "Executor.hpp"
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
#include "Agent.hpp"

using namespace cerberus;

/* ── Test: Arena RAII wrapper ────────────────────────────── */
void test_arena_cpp() {
    printf("\n[TEST] C++ Arena Wrapper\n");
    Arena arena(1024 * 1024);

    int* nums = (int*)arena.alloc(10 * sizeof(int));
    assert(nums != nullptr);
    for (int i = 0; i < 10; i++) nums[i] = i;
    assert(nums[9] == 9);

    auto mark = arena.save();
    arena.alloc(512);
    arena.restore(mark);
    assert(arena.used() == (size_t)mark);

    arena.printStats();
    printf("[TEST] C++ Arena Wrapper PASSED ✅\n");
}

/* ── Test: ThreadPool RAII wrapper ───────────────────────── */
static std::atomic<int> cpp_task_count{0};

void test_thread_pool_cpp() {
    printf("\n[TEST] C++ ThreadPool Wrapper\n");
    ThreadPool pool(4);
    cpp_task_count = 0;

    auto task = [](void* arg) {
        (void)arg;
        cpp_task_count.fetch_add(1);
    };

    for (int i = 0; i < 50; i++)
        pool.submit(task, nullptr);

    pool.wait();
    assert(cpp_task_count == 50);
    printf("[TEST] C++ ThreadPool: %d tasks done ✅\n", cpp_task_count.load());
}

/* ── Test: Executor wrapper ──────────────────────────────── */
void test_executor_cpp() {
    printf("\n[TEST] C++ Executor Wrapper\n");
    auto r1 = Executor::shell("echo 'Cerberus CPP'", 2000);
    assert(r1.ok());
    assert(r1.stdout_data.find("Cerberus CPP") != std::string::npos);

    auto r2 = Executor::shell("sleep 10", 300);
    assert(r2.timeout());
    printf("[TEST] C++ Executor Wrapper PASSED ✅\n");
}

/* ── Test: DynamicToolRegistry ────────────────────────────── */
void test_tool_registry() {
    printf("\n[TEST] DynamicToolRegistry\n");
    DynamicToolRegistry registry;

    registry.registerTool("add",
        "Add two numbers. args = 'a b'",
        [](const ToolInput& input) -> ToolOutput {
            int a = 0, b = 0;
            sscanf(input.args.c_str(), "%d %d", &a, &b);
            return ToolOutput{std::to_string(a + b), true, "", 0};
        }
    );

    MCPToolSchema schema{"mcp_ping", "Ping server", "server1", {"target"}};
    registry.registerMCP(schema, [](const ToolInput& in) -> ToolOutput {
        return ToolOutput{"pong: " + in.args, true, "", 0};
    });

    assert(registry.size() == 2);
    assert(registry.hasTool("mcp_ping"));
    assert(registry.serverCount() == 1);

    auto r1 = registry.dispatch({"mcp_ping", "127.0.0.1"});
    assert(r1.success);
    assert(r1.result == "pong: 127.0.0.1");

    registry.teardownServer("server1");
    assert(!registry.hasTool("mcp_ping"));
    assert(registry.serverCount() == 0);

    printf("[TEST] DynamicToolRegistry PASSED ✅\n");
}

/* ── Test: OutputParser ───────────────────────────────────── */
void test_output_parser() {
    printf("\n[TEST] OutputParser\n");
    Arena arena(1024 * 64);

    std::string json_obj = R"({"status": "ok", "code": 200})";
    ParsedResult r1 = OutputParser::parse(arena.raw(), json_obj);
    assert(r1.kind == ParsedKind::JSON_OBJECT);
    assert(r1.extracted == "ok");

    std::string json_arr = R"(["file1.txt", "file2.txt"])";
    ParsedResult r2 = OutputParser::parse(arena.raw(), json_arr);
    assert(r2.kind == ParsedKind::JSON_ARRAY);
    assert(r2.extracted == "file1.txt");

    std::string plain = "Hello world";
    ParsedResult r3 = OutputParser::parse(arena.raw(), plain);
    assert(r3.kind == ParsedKind::PLAIN_TEXT);
    assert(r3.extracted == "Hello world");

    printf("[TEST] OutputParser PASSED ✅\n");
}

/* ── Test: SharedMemory ───────────────────────────────────── */
void test_shared_memory() {
    printf("\n[TEST] SharedMemory Native\n");
    SharedMemory shm(1024 * 1024);
    shm.write("status", "running", "agent_1");
    shm.write("step", "2", "agent_2");

    assert(shm.count() == 2);
    assert(shm.read("status") == "running");

    std::string summary = shm.summary();
    assert(summary.find("status=running") != std::string::npos);
    assert(summary.find("step=2") != std::string::npos);

    printf("[TEST] SharedMemory PASSED ✅\n");
}

/* ── Test: ContextBuffer ──────────────────────────────────── */
void test_context_buffer() {
    printf("\n[TEST] ContextBuffer\n");
    ContextBuffer ctx(4096, 4096, 200);

    ctx.addSystem("You are a helpful agent.");
    ctx.addUser("What is 2 + 2?");
    ctx.addAssistant("The answer is 4.");
    ctx.addToolResult("calculator", "4");

    assert(ctx.messageCount() == 4);
    std::string prompt(ctx.buildPrompt());
    assert(prompt.find("TOOL:calculator") != std::string::npos);

    printf("[TEST] ContextBuffer PASSED ✅\n");
}

/* ── Test: V1.4.0 KVCacheArena ────────────────────────────── */
void test_kv_cache_arena() {
    printf("\n[TEST] V1.4.0 KVCacheArena (Zero-Copy Paged KV-Cache)\n");
    KVCacheArena kv(100, "/cerberus_test_kv_");

    std::string sys_prompt = "<system>You are Cerberus AI assistant.</system>";
    auto block1 = kv.allocateBlock(sys_prompt, 8192); // 2 pages (8KB)
    assert(block1 != nullptr);
    assert(block1->page_count == 2);
    assert(block1->size_bytes == 8192);

    /* Test prefix hit */
    auto hit = kv.lookupPrefix(sys_prompt);
    assert(hit != nullptr);
    assert(hit->block_id == block1->block_id);
    assert(hit->ref_count() == 3);

    kv.releaseBlock(block1->block_id);
    kv.releaseBlock(block1->block_id);
    kv.releaseBlock(block1->block_id);

    assert(kv.activeBlocks() == 0);
    printf("[TEST] KVCacheArena PASSED ✅\n");
}

/* ── Test: V1.4.0 SQ8 & HNSW VectorBuffer Upgrades ─────────── */
void test_vector_buffer_v140() {
    printf("\n[TEST] V1.4.0 SQ8 Quantizer & HNSW Indexing\n");
    VectorBuffer vb(4);
    vb.enableSQ8(true);
    vb.buildHNSWIndex(16, 64, 32);

    assert(vb.isSQ8Enabled());
    assert(vb.hasHNSW());

    vb.push({1.0f, 0.0f, 0.0f, 0.0f}, "CPU Architecture Manual");
    vb.push({0.0f, 1.0f, 0.0f, 0.0f}, "Database Indexing Guide");
    vb.push({0.0f, 0.0f, 1.0f, 0.0f}, "Neural Network Tuning");

    assert(vb.size() == 3);

    auto matches = vb.query({0.95f, 0.05f, 0.0f, 0.0f}, 2);
    assert(!matches.empty());
    assert(matches[0].text == "CPU Architecture Manual");

    vb.printStats();
    printf("[TEST] SQ8 & HNSW VectorBuffer PASSED ✅\n");
}

/* ── Test: V1.3.0 Telemetry (Lockless Ring Buffer) ────────── */
void test_telemetry() {
    printf("\n[TEST] V1.3.0 Telemetry (Lockless Ring Buffer)\n");
    Telemetry telem(1024);

    telem.emit(CA_EVT_THINK,      "AgentA", "thinking_step_1");
    telem.emit(CA_EVT_ACT,        "AgentA", "call_shell_ls");
    telem.emit(CA_EVT_AGENT_DONE, "AgentA", "task_completed");

    assert(telem.available() == 3);
    assert(!telem.dropped());

    auto events = telem.drain(10);
    assert(events.size() == 3);
    assert(std::string(events[0].agent_id) == "AgentA");
    assert(std::string(events[1].label) == "call_shell_ls");

    printf("[TEST] Telemetry PASSED ✅\n");
}

/* ── Test: V1.3.0 SwarmOrchestrator ───────────────────────── */
void test_swarm_orchestrator() {
    printf("\n[TEST] V1.3.0 SwarmOrchestrator (Hierarchical Swarm)\n");
    ThreadPool pool(4);
    Telemetry ring(1024);

    SwarmOrchestrator swarm(pool.raw_pool(), ring.raw(), "TestSwarm");

    swarm.spawn("worker_1", []() { return "worker_1 output"; });
    swarm.spawn("worker_2", []() { return "worker_2 output"; });
    swarm.spawn("worker_3", []() { return "worker_3 output"; });

    assert(swarm.pendingCount() == 3);

    SwarmSummary summary = swarm.await(5000);
    assert(summary.succeeded == 3);
    assert(summary.failed == 0);
    assert(summary.results.size() == 3);

    std::string combined = summary.combined();
    assert(combined.find("[worker_1]: worker_1 output") != std::string::npos);
    assert(combined.find("[worker_2]: worker_2 output") != std::string::npos);

    printf("[TEST] SwarmOrchestrator PASSED ✅\n");
}

/* ── Test: Full Agent Loop ───────────────────────────────── */
void test_agent() {
    printf("\n[TEST] Agent State Machine with V1.4.0 features\n");
    AgentConfig config;
    config.name        = "TestAgent";
    config.max_steps   = 3;
    config.num_threads = 2;

    Agent agent(config);

    SharedMemory shm;
    shm.write("goal", "test_v1.4", "initializer");
    agent.setSharedMemory(&shm);

    std::string r1 = agent.run("list the files in current directory");
    assert(!r1.empty());

    printf("[TEST] Agent State Machine PASSED ✅\n");
}

/* ── Main ────────────────────────────────────────────────── */
int main() {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   Cerberus C++ Engine Tests (V1.4.0)    ║\n");
    printf("╚══════════════════════════════════════════╝\n");

    test_arena_cpp();
    test_thread_pool_cpp();
    test_executor_cpp();
    test_tool_registry();
    test_output_parser();
    test_shared_memory();
    test_context_buffer();
    test_kv_cache_arena();
    test_vector_buffer_v140();
    test_telemetry();
    test_swarm_orchestrator();
    test_agent();

    printf("\n✅ All C++ Engine V1.4.0 tests PASSED!\n\n");
    return 0;
}