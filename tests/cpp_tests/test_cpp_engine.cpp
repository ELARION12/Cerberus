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
#include "Agent.hpp"

using namespace coreagent;

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
    printf("[TEST] C++ ThreadPool: %d tasks done ✅\n",
           cpp_task_count.load());
}

/* ── Test: Executor wrapper ──────────────────────────────── */
void test_executor_cpp() {
    printf("\n[TEST] C++ Executor Wrapper\n");

    auto r1 = Executor::shell("echo 'CoreAgent CPP'", 2000);
    assert(r1.ok());
    assert(r1.stdout_data.find("CoreAgent CPP") != std::string::npos);
    printf("[TEST] shell output: %s", r1.stdout_data.c_str());

    auto r2 = Executor::shell("sleep 10", 300);
    assert(r2.timeout());
    printf("[TEST] Timeout correctly caught ✅\n");

    printf("[TEST] C++ Executor Wrapper PASSED ✅\n");
}

/* ── Test: ToolRegistry & DynamicToolRegistry ────────────── */
void test_tool_registry() {
    printf("\n[TEST] DynamicToolRegistry (Pillar 2)\n");

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

/* ── Test: OutputParser (Pillar 1) ────────────────────────── */
void test_output_parser() {
    printf("\n[TEST] OutputParser (Pillar 1)\n");

    Arena arena(1024 * 64);

    /* 1. JSON Object */
    std::string json_obj = R"({"status": "ok", "code": 200})";
    ParsedResult r1 = OutputParser::parse(arena.raw(), json_obj);
    assert(r1.kind == ParsedKind::JSON_OBJECT);
    assert(r1.extracted == "ok");

    /* 2. JSON Array */
    std::string json_arr = R"(["file1.txt", "file2.txt"])";
    ParsedResult r2 = OutputParser::parse(arena.raw(), json_arr);
    assert(r2.kind == ParsedKind::JSON_ARRAY);
    assert(r2.extracted == "file1.txt");

    /* 3. Plain text */
    std::string plain = "Hello world";
    ParsedResult r3 = OutputParser::parse(arena.raw(), plain);
    assert(r3.kind == ParsedKind::PLAIN_TEXT);
    assert(r3.extracted == "Hello world");

    printf("[TEST] OutputParser PASSED ✅\n");
}

/* ── Test: SharedMemory (Pillar 3) ────────────────────────── */
void test_shared_memory() {
    printf("\n[TEST] SharedMemory Native (Pillar 3)\n");

    SharedMemory shm(1024 * 1024);
    shm.write("status", "running", "agent_1");
    shm.write("step", "2", "agent_2");

    assert(shm.count() == 2);
    assert(shm.read("status") == "running");

    std::string summary = shm.summary();
    assert(summary.find("status=running") != std::string::npos);
    assert(summary.find("step=2") != std::string::npos);

    printf("[TEST] SharedMemory Summary:\n%s", summary.c_str());
    printf("[TEST] SharedMemory PASSED ✅\n");
}

/* ── Test: ContextBuffer ─────────────────────────────────── */
void test_context_buffer() {
    printf("\n[TEST] ContextBuffer\n");

    ContextBuffer ctx(4096, 4096, 200);

    ctx.addSystem("You are a helpful agent.");
    ctx.addUser("What is 2 + 2?");
    ctx.addAssistant("The answer is 4.");
    ctx.addToolResult("calculator", "4");

    assert(ctx.messageCount() == 4);

    std::string prompt(ctx.buildPrompt());
    assert(prompt.find("SYSTEM") != std::string::npos);
    assert(prompt.find("USER")   != std::string::npos);
    assert(prompt.find("TOOL:calculator") != std::string::npos);

    ctx.printSummary();
    printf("[TEST] ContextBuffer PASSED ✅\n");
}

/* ── Test: Full Agent Loop ───────────────────────────────── */
void test_agent() {
    printf("\n[TEST] Agent State Machine with V1.2.0 features\n");

    AgentConfig config;
    config.name        = "TestAgent";
    config.max_steps   = 3;
    config.num_threads = 2;

    Agent agent(config);

    SharedMemory shm;
    shm.write("goal", "test_v1.2", "initializer");
    agent.setSharedMemory(&shm);

    std::string r1 = agent.run("list the files in current directory");
    assert(!r1.empty());

    printf("[TEST] Agent State Machine PASSED ✅\n");
}

/* ── Main ────────────────────────────────────────────────── */
int main() {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   CoreAgent C++ Engine Tests (V1.2.0)    ║\n");
    printf("╚══════════════════════════════════════════╝\n");

    test_arena_cpp();
    test_thread_pool_cpp();
    test_executor_cpp();
    test_tool_registry();
    test_output_parser();
    test_shared_memory();
    test_context_buffer();
    test_agent();

    printf("\n✅ All C++ Engine V1.2.0 tests PASSED!\n\n");
    return 0;
}