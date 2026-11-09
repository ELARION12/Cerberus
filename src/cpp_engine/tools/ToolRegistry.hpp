#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include "ca_log.h"
#include <cstdint>  

namespace geryon {

/* All data a tool receives from the agent planner. */
struct ToolInput {
    std::string name; /* registered name of the tool */
    std::string args; /* raw argument string; each tool parses this itself */
};

/* All data a tool must return to the agent. */
struct ToolOutput {
    std::string result;     /* tool output on success */
    bool        success;    /* false if the tool encountered an error */
    std::string error;      /* human-readable error message (empty on success) */
    uint32_t    elapsed_ms; /* wall-clock execution time */
};

/* Every tool is a plain callable with this signature. Stored as std::function
 * so lambdas, member functions, and plain function pointers all work. */
using ToolFn = std::function<ToolOutput(const ToolInput&)>;

struct ToolDef {
    std::string name;
    std::string description;
    ToolFn      fn;
};

/* Registry of named tools. Supports dynamic registration and name-based dispatch. */
class ToolRegistry {
public:
    virtual ~ToolRegistry() = default;

    void registerTool(const std::string& name,
                      const std::string& description,
                      ToolFn fn) {
        if (tools_.count(name))
            throw std::runtime_error("[ToolRegistry] Already registered: " + name);

        tools_[name] = ToolDef{name, description, fn};
        names_.push_back(name);

        CA_LOG("[ToolRegistry] Registered: %-18s → %s\n",
               name.c_str(), description.c_str());
    }

    void removeTool(const std::string& name) {
        tools_.erase(name);
        for (auto it = names_.begin(); it != names_.end(); ++it) {
            if (*it == name) {
                names_.erase(it);
                break;
            }
        }
    }

    ToolOutput dispatch(const ToolInput& input) const {
        auto it = tools_.find(input.name);
        if (it == tools_.end())
            return ToolOutput{"", false, "Unknown tool: " + input.name, 0};
        try {
            return it->second.fn(input);
        } catch (const std::exception& e) {
            return ToolOutput{"", false,
                std::string("[ToolRegistry] Exception: ") + e.what(), 0};
        }
    }

    bool hasTool(const std::string& name) const {
        return tools_.count(name) > 0;
    }

    const std::vector<std::string>& toolNames() const { return names_; }
    size_t size() const { return tools_.size(); }

    void printAll() const {
        printf("\n[ToolRegistry] Available tools (%zu):\n", tools_.size());
        for (auto& name : names_) {
            auto& t = tools_.at(name);
            printf("  %-20s : %s\n", t.name.c_str(), t.description.c_str());
        }
    }

private:
    std::unordered_map<std::string, ToolDef> tools_;
    std::vector<std::string>                 names_;
};

} // namespace geryon