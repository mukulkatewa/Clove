#pragma once

#include <clove/config.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <vector>
#include <atomic>

namespace clove {

// Forward declarations
class OpenRouterClient;
class InferenceGateway;
class PrivacyFilter;
class AuditLogger;
class StateStore;
class ArtifactStore;
class ChainStore;
class MemoryBlockStore;
class McpBridge;
class ContextAssembler;
class PermissionsStore;

/// Event emitted during a run — streamed to SSE or collected for sync response.
struct RunEvent {
    std::string type;      // "start", "thinking", "tool_call", "tool_result", "done", "error"
    nlohmann::json data;
};

/// Configuration for a single agent run.
struct RunConfig {
    std::string goal;
    std::string model;
    double budget_usd = 1.0;
    int max_steps = 20;
    std::vector<std::string> allowed_tools;
    std::string agent_name = "agent";
    std::string chain_id;        // If empty, engine creates one
};

/// Result of a completed run.
struct RunResult {
    bool success = false;
    std::string content;
    std::string error;
    std::string chain_id;
    int steps = 0;
    int total_tokens = 0;
    double total_cost_usd = 0.0;
    nlohmann::json step_log;
};

using EventCallback = std::function<void(const RunEvent&)>;

/// RunEngine — the real agent. Executes a tool-calling loop using kernel subsystems.
/// Each tool call goes through real kernel operations: file I/O, HTTP (CURL), exec (popen),
/// MCP, memory blocks, state store. All permission-gated and audited.
class RunEngine {
public:
    RunEngine(
        OpenRouterClient& openrouter,
        InferenceGateway& gateway,
        PrivacyFilter& privacy,
        AuditLogger& audit,
        StateStore& state,
        PermissionsStore& permissions,
        ArtifactStore* artifacts,        // nullable
        ChainStore* chains,              // nullable
        MemoryBlockStore* memory,        // nullable
        McpBridge* mcp,                  // nullable
        ContextAssembler* assembler,     // nullable
        const KernelConfig& config
    );

    /// Run the agent loop synchronously. Calls on_event for each step.
    RunResult execute(const RunConfig& cfg, EventCallback on_event = nullptr);

    /// Request cancellation (from another thread).
    void cancel() { cancelled_ = true; }
    bool is_cancelled() const { return cancelled_; }

private:
    OpenRouterClient& openrouter_;
    InferenceGateway& gateway_;
    PrivacyFilter& privacy_;
    AuditLogger& audit_;
    StateStore& state_;
    PermissionsStore& permissions_;
    ArtifactStore* artifacts_;
    ChainStore* chains_;
    MemoryBlockStore* memory_;
    McpBridge* mcp_;
    ContextAssembler* assembler_;
    const KernelConfig& config_;
    std::atomic<bool> cancelled_{false};
    uint32_t agent_id_ = 0;  // set per-run for permission checks

    nlohmann::json build_tools(const std::vector<std::string>& allowed);
    std::string execute_tool(const std::string& name, const nlohmann::json& args,
                             const std::string& model, double& cost, int& tokens);
    void emit(EventCallback& cb, const std::string& type, const nlohmann::json& data);

    // Real tool implementations
    std::string tool_read_file(const std::string& path);
    std::string tool_write_file(const std::string& path, const std::string& content);
    std::string tool_exec(const std::string& command);
    std::string tool_http(const std::string& url, const std::string& method, const std::string& body);
    std::string tool_search(const std::string& query, const std::string& model, double& cost, int& tokens);
    std::string tool_mcp_call(const std::string& server, const std::string& tool, const nlohmann::json& args);
    std::string tool_remember(const std::string& fact);
    std::string tool_recall(const std::string& query);
};

} // namespace clove
