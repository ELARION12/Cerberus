#pragma once

#include "AgentState.hpp"
#include "DynamicToolRegistry.hpp"
#include "ContextBuffer.hpp"
#include "Arena.hpp"
#include "Executor.hpp"
#include "ThreadPool.hpp"
#include "OutputParser.hpp"
#include "SharedMemory.hpp"

#include <string>
#include <functional>
#include <memory>
#include <cstdint>

namespace geryon {

/*
 * All configuration for a single Agent instance.
 * Passed by value at construction; changing fields after creation has no effect.
 */
struct AgentConfig {
    std::string name          = "Geryon";
    std::string system_prompt = "You are a fast, efficient AI agent. "
                                "Use available tools to complete tasks concisely.";
    size_t      max_tokens    = 4096;  /* token budget for the context window */
    uint32_t    num_threads   = 4;     /* thread pool size for parallel tool calls */
    uint32_t    max_steps     = 10;    /* maximum THINK-PLAN-ACT-REFLECT iterations */
    uint32_t    tool_timeout  = 10000; /* per-tool timeout in milliseconds */

    /* Arena sizes for ContextBuffer:
     *   context_arena_capacity   — retained_ and standby_ arenas (must be equal).
     *   context_scratch_capacity — scratch arena for a single serialised prompt. */
    size_t      context_arena_capacity   = 4 * 1024 * 1024;
    size_t      context_scratch_capacity = 256 * 1024;
};

/*
 * Agent — synchronous reasoning loop.
 * State machine:  IDLE → THINKING → PLANNING → ACTING → REFLECTING
 *                             ↑____________________________________|
 *                                  (repeat until DONE or ERROR)
 */
class Agent {
public:
    explicit Agent(const AgentConfig& config = AgentConfig{});
    ~Agent() = default;

    Agent(const Agent&)            = delete;
    Agent& operator=(const Agent&) = delete;

    void registerTool(const std::string& name,
                      const std::string& description,
                      ToolFn fn);

    std::string run(const std::string& task);

    AgentState    state()     const { return state_;   }
    std::string   stateName() const { return agentStateToString(state_); }
    std::string   name()      const { return config_.name; }

    ContextBuffer& context() { return context_; }
    DynamicToolRegistry& tools() { return registry_; }

    void setSharedMemory(SharedMemory* shm) { shared_memory_ = shm; }
    SharedMemory* sharedMemory() const { return shared_memory_; }

private:
    AgentConfig          config_;
    AgentState           state_;
    ContextBuffer        context_;
    DynamicToolRegistry  registry_;
    ThreadPool           thread_pool_;
    SharedMemory*        shared_memory_ = nullptr;
    Arena                parse_arena_{64 * 1024};

    std::string think  (const std::string& task);
    std::string plan   (const std::string& thought);
    ToolOutput  act    (const std::string& plan_str);
    bool        reflect(const ToolOutput& result,
                        std::string& final_output);

    void setState(AgentState s);
    void setupBuiltinTools();
};

} // namespace geryon