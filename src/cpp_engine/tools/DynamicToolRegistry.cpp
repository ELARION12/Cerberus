#include "DynamicToolRegistry.hpp"

#include <cstdio>
#include "ca_log.h"

namespace geryon {

void DynamicToolRegistry::registerMCP(const MCPToolSchema& schema,
                                      ToolFn trampoline) {
    std::lock_guard<std::mutex> lock(mcp_mutex_);

    registerTool(schema.name, schema.description, std::move(trampoline));
    mcp_schemas_[schema.server_id].push_back(schema);

    CA_LOG("[DynamicToolRegistry] registered MCP tool '%s' from server '%s'\n",
           schema.name.c_str(), schema.server_id.c_str());
}

void DynamicToolRegistry::teardownServer(const std::string& server_id) {
    std::lock_guard<std::mutex> lock(mcp_mutex_);

    auto it = mcp_schemas_.find(server_id);
    if (it == mcp_schemas_.end()) return;

    for (const auto& schema : it->second) {
        removeTool(schema.name);
        CA_LOG("[DynamicToolRegistry] removed tool '%s' (server '%s' disconnected)\n",
               schema.name.c_str(), server_id.c_str());
    }
    mcp_schemas_.erase(it);

    CA_LOG("[DynamicToolRegistry] server '%s' torn down\n", server_id.c_str());
}

void DynamicToolRegistry::refreshServer(
        const std::string& server_id,
        const std::vector<MCPToolSchema>& new_schemas,
        std::function<ToolFn(const MCPToolSchema&)> trampoline_factory) {
    teardownServer(server_id);

    for (const auto& schema : new_schemas) {
        registerMCP(schema, trampoline_factory(schema));
    }

    CA_LOG("[DynamicToolRegistry] server '%s' refreshed with %zu tools\n",
           server_id.c_str(), new_schemas.size());
}

std::vector<MCPToolSchema> DynamicToolRegistry::mcpSchemas() const {
    std::lock_guard<std::mutex> lock(mcp_mutex_);
    std::vector<MCPToolSchema> all;
    for (const auto& kv : mcp_schemas_) {
        for (const auto& s : kv.second) all.push_back(s);
    }
    return all;
}

size_t DynamicToolRegistry::serverCount() const {
    std::lock_guard<std::mutex> lock(mcp_mutex_);
    return mcp_schemas_.size();
}

} // namespace geryon
