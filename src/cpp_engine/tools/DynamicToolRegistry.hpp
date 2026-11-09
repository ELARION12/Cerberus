#pragma once

#include "ToolRegistry.hpp"

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <functional>

namespace geryon {

struct MCPToolSchema {
    std::string name;
    std::string description;
    std::string server_id;
    std::vector<std::string> required_args;
};

class DynamicToolRegistry : public ToolRegistry {
public:
    DynamicToolRegistry() = default;
    ~DynamicToolRegistry() override = default;

    void registerMCP(const MCPToolSchema& schema, ToolFn trampoline);
    void teardownServer(const std::string& server_id);
    void refreshServer(
        const std::string& server_id,
        const std::vector<MCPToolSchema>& new_schemas,
        std::function<ToolFn(const MCPToolSchema&)> trampoline_factory);

    std::vector<MCPToolSchema> mcpSchemas() const;
    size_t serverCount() const;

private:
    mutable std::mutex mcp_mutex_;
    std::unordered_map<std::string, std::vector<MCPToolSchema>> mcp_schemas_;
};

} // namespace geryon
