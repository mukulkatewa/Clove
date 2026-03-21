#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace clove {

struct McpServerConfig {
    std::string name;
    std::string command;                     // e.g. "npx"
    std::vector<std::string> args;           // e.g. ["@modelcontextprotocol/server-filesystem", "/tmp"]
    std::string transport = "stdio";         // "stdio" or "sse"
    std::string url;                         // For SSE transport
    std::vector<std::string> allowed_agents; // Empty = all agents
    int max_concurrent = 10;
};

struct McpTool {
    std::string server_name;
    std::string name;
    std::string description;
    nlohmann::json input_schema;
};

struct McpCallResult {
    bool success = false;
    nlohmann::json content;   // Array of {type, text} or {type, data}
    std::string error;
    double duration_ms = 0;
};

struct McpServerStatus {
    std::string name;
    bool connected = false;
    int pid = -1;
    size_t tool_count = 0;
    uint64_t total_calls = 0;
};

class McpBridge {
public:
    McpBridge();
    ~McpBridge();

    McpBridge(const McpBridge&) = delete;
    McpBridge& operator=(const McpBridge&) = delete;

    // Server management
    bool add_server(const McpServerConfig& config);
    bool remove_server(const std::string& name);
    bool start_server(const std::string& name);
    void start_all();
    void stop_all();

    // Tool discovery — filtered by agent permissions
    std::vector<McpTool> list_tools(uint32_t agent_id = 0,
                                     const std::string& agent_name = "") const;

    // Tool invocation — mediated through kernel
    McpCallResult call_tool(uint32_t agent_id,
                            const std::string& server_name,
                            const std::string& tool_name,
                            const nlohmann::json& arguments);

    // Status
    std::vector<McpServerStatus> status() const;
    bool has_server(const std::string& name) const;

private:
    struct McpSession {
        McpServerConfig config;
        int pid = -1;
        int stdin_fd = -1;     // Write to server
        int stdout_fd = -1;    // Read from server
        bool connected = false;
        int next_id = 1;
        uint64_t total_calls = 0;
        std::vector<McpTool> tools;

        // JSON-RPC line buffer
        std::string read_buf;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, McpSession> sessions_;

    // Spawn subprocess, set up pipes
    bool spawn_process(McpSession& session);

    // JSON-RPC communication
    nlohmann::json send_request(McpSession& session,
                                 const std::string& method,
                                 const nlohmann::json& params = {});
    void send_notification(McpSession& session,
                           const std::string& method,
                           const nlohmann::json& params = {});
    nlohmann::json read_response(McpSession& session, int timeout_ms = 10000);

    // Write raw JSON to stdin
    bool write_json(McpSession& session, const nlohmann::json& msg);

    // MCP protocol
    bool initialize_session(McpSession& session);
    bool discover_tools(McpSession& session);

    // Permission check
    bool agent_allowed(const McpSession& session,
                       const std::string& agent_name) const;

    // Cleanup
    void kill_process(McpSession& session);
};

} // namespace clove
