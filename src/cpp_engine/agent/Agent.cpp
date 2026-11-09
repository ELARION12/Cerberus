#include "Agent.hpp"

#include <cstdio>
#include "ca_log.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <fstream>
#include <sstream>

namespace geryon {

Agent::Agent(const AgentConfig& config)
    : config_(config),
      state_(AgentState::IDLE),
      context_(config.context_arena_capacity, config.context_scratch_capacity, config.max_tokens),
      thread_pool_(config.num_threads)
{
    context_.addSystem(config.system_prompt);
    setupBuiltinTools();

    CA_LOG("\n[%s] ✅ Agent initialized\n",   config_.name.c_str());
    CA_LOG("[%s]    Threads  : %u\n",         config_.name.c_str(), config_.num_threads);
    CA_LOG("[%s]    MaxSteps : %u\n",         config_.name.c_str(), config_.max_steps);
    CA_LOG("[%s]    MaxTokens: %zu\n\n",      config_.name.c_str(), config_.max_tokens);
}

void Agent::setState(AgentState s) {
    CA_LOG("[%s] %-12s → %s\n",
           config_.name.c_str(),
           agentStateToString(state_).c_str(),
           agentStateToString(s).c_str());
    state_ = s;
}

void Agent::registerTool(const std::string& name,
                          const std::string& description,
                          ToolFn fn) {
    registry_.registerTool(name, description, fn);
}

void Agent::setupBuiltinTools() {
    registry_.registerTool("shell",
        "Execute a shell command, returns stdout",
        [this](const ToolInput& input) -> ToolOutput {
            auto r = Executor::shell(input.args, config_.tool_timeout);
            return ToolOutput{
                r.stdout_data,
                r.ok(),
                r.stderr_data,
                r.elapsed_ms
            };
        }
    );

    registry_.registerTool("read_file",
        "Read contents of a file. args = file path",
        [](const ToolInput& input) -> ToolOutput {
            std::ifstream f(input.args);
            if (!f.is_open())
                return ToolOutput{"", false,
                    "Cannot open file: " + input.args, 0};
            std::ostringstream ss;
            ss << f.rdbuf();
            return ToolOutput{ss.str(), true, "", 0};
        }
    );

    registry_.registerTool("write_file",
        "Write content to a file. args = 'path|content'",
        [](const ToolInput& input) -> ToolOutput {
            size_t sep = input.args.find('|');
            if (sep == std::string::npos)
                return ToolOutput{"", false,
                    "Format: 'path|content'", 0};

            std::string path    = input.args.substr(0, sep);
            std::string content = input.args.substr(sep + 1);

            std::ofstream f(path);
            if (!f.is_open())
                return ToolOutput{"", false,
                    "Cannot write file: " + path, 0};
            f << content;
            return ToolOutput{"Written: " + path, true, "", 0};
        }
    );

    registry_.registerTool("echo",
        "Echo back the input args. Used for testing.",
        [](const ToolInput& input) -> ToolOutput {
            return ToolOutput{input.args, true, "", 0};
        }
    );
}

std::string Agent::think(const std::string& task) {
    setState(AgentState::THINKING);

    if (shared_memory_) {
        std::string mem_summary = shared_memory_->summary();
        if (!mem_summary.empty()) {
            context_.addUser("[Shared Memory Blackboard]\n" + mem_summary);
        }
    }

    context_.addUser(task);

    std::string thought =
        "Task received: [" + task + "]. "
        "I will analyze what tools are needed and form a plan.";

    context_.addAssistant(thought);

    CA_LOG("[%s] 💭 Thought: %s\n\n",
           config_.name.c_str(), thought.c_str());

    return thought;
}

std::string Agent::plan(const std::string& thought) {
    setState(AgentState::PLANNING);

    std::string prompt(context_.buildPrompt());
    std::string plan_str;

    if (prompt.find("list") != std::string::npos ||
        prompt.find("ls")   != std::string::npos ||
        prompt.find("files") != std::string::npos) {
        plan_str = "shell|ls -la";
    }
    else if (prompt.find("date") != std::string::npos ||
             prompt.find("time") != std::string::npos) {
        plan_str = "shell|date";
    }
    else if (prompt.find("disk") != std::string::npos ||
             prompt.find("space") != std::string::npos) {
        plan_str = "shell|df -h";
    }
    else if (prompt.find("memory") != std::string::npos ||
             prompt.find("ram")  != std::string::npos) {
        plan_str = "shell|free -h";
    }
    else if (prompt.find("read") != std::string::npos) {
        plan_str = "echo|No file specified";
    }
    else {
        plan_str = "echo|" + thought;
    }

    CA_LOG("[%s] 📋 Plan: %s\n\n",
           config_.name.c_str(), plan_str.c_str());

    return plan_str;
}

ToolOutput Agent::act(const std::string& plan_str) {
    setState(AgentState::ACTING);

    size_t sep = plan_str.find('|');
    std::string tool_name = (sep != std::string::npos)
                            ? plan_str.substr(0, sep)
                            : plan_str;
    std::string tool_args = (sep != std::string::npos)
                            ? plan_str.substr(sep + 1)
                            : "";

    ToolInput input{tool_name, tool_args};

    CA_LOG("[%s] 🔧 Calling tool: [%s] with args: [%s]\n",
           config_.name.c_str(),
           tool_name.c_str(),
           tool_args.c_str());

    ToolOutput output = registry_.dispatch(input);

    context_.addToolResult(tool_name,
        output.success ? output.result : ("ERROR: " + output.error));

    return output;
}

bool Agent::reflect(const ToolOutput& result, std::string& final_output) {
    setState(AgentState::REFLECTING);

    if (result.success) {
        parse_arena_.reset();
        ParsedResult parsed = OutputParser::parse(parse_arena_.raw(), result.result);
        final_output = std::string(parsed.extracted);

        CA_LOG("[%s] ✅ Tool succeeded in %ums (Format: %s)\n",
               config_.name.c_str(), result.elapsed_ms,
               OutputParser::kindName(parsed.kind));
        CA_LOG("[%s] 📄 Result (Extracted):\n%s\n",
               config_.name.c_str(), final_output.c_str());
        return true;
    } else {
        printf("[%s] ❌ Tool failed: %s\n",
               config_.name.c_str(), result.error.c_str());
        return false;
    }
}

std::string Agent::run(const std::string& task) {
    CA_LOG("\n╔══════════════════════════════════════════╗\n");
    CA_LOG("║  [%s] Starting task                  \n", config_.name.c_str());
    CA_LOG("║  Task: %-35s\n", task.c_str());
    CA_LOG("╚══════════════════════════════════════════╝\n\n");

    setState(AgentState::IDLE);

    std::string final_output;
    uint32_t    steps = 0;

    while (steps < config_.max_steps) {
        steps++;
        CA_LOG("[%s] ── Step %u/%u ───────────────────────\n\n",
               config_.name.c_str(), steps, config_.max_steps);

        std::string thought  = think(task);
        std::string plan_str = plan(thought);
        ToolOutput  result   = act(plan_str);
        bool        done     = reflect(result, final_output);

        if (done) {
            setState(AgentState::DONE);
            break;
        }

        if (steps >= config_.max_steps) {
            setState(AgentState::ERROR);
            final_output = "Max steps reached without completion.";
        }
    }

    if (ca_log_enabled()) context_.printSummary();

    CA_LOG("\n╔══════════════════════════════════════════╗\n");
    CA_LOG("║  [%s] Task Complete ✅               \n", config_.name.c_str());
    CA_LOG("╚══════════════════════════════════════════╝\n\n");

    return final_output;
}

} // namespace geryon