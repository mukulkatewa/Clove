#pragma once
#include <clove/config.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

namespace clove {

// Forward declarations — the API server references kernel subsystems
class AgentManager;
class StateStore;
class EventBus;
class PermissionsStore;
class InferenceGateway;
class PrivacyFilter;
class AuditLogger;
class ExecutionLogger;
class PolicyRecommender;
class McpBridge;
class A2aBridge;
class TunnelBridge;
class WorldEngine;
class LlmQueue;
class ArtifactStore;
class ChainStore;
class ContextAssembler;
class MemoryBlockStore;
class OpenRouterClient;
class OpenClawManager;
class SandboxManager;
class AgentMailboxRegistry;
class DaemonManager;
class AnthropicClient;
class StateStoreDb;
class MemoryBlockDb;
class JobQueue;
class SupabaseSync;
class WorkspaceDb;
class AgentDefDb;
class WorkspaceDataDb;
class WorkspaceOutputDb;
class AgentRunDb;
class SwarmDb;

struct ApiContext {
    KernelConfig& config;
    AgentManager& agent_manager;
    StateStore& state_store;
    EventBus& event_bus;
    PermissionsStore& permissions_store;
    InferenceGateway& inference_gateway;
    PrivacyFilter& privacy_filter;
    AuditLogger& audit_logger;
    ExecutionLogger& execution_logger;
    PolicyRecommender& policy_recommender;
    McpBridge* mcp_bridge;            // nullable
    A2aBridge* a2a_bridge;            // nullable
    TunnelBridge* tunnel_bridge;      // nullable
    WorldEngine* world_engine;        // nullable
    // New: subsystems for /api/think, /api/run, /api/fleet
    LlmQueue* llm_queue;             // nullable
    ArtifactStore* artifact_store;    // nullable
    ChainStore* chain_store;          // nullable
    ContextAssembler* assembler;      // nullable
    MemoryBlockStore* memory_blocks;  // nullable
    MemoryBlockDb*    memory_block_db; // nullable — SQLite persistence for memory blocks
    OpenRouterClient* openrouter;     // nullable
    OpenClawManager* openclaw;       // nullable
    SandboxManager* sandbox_manager; // nullable
    AgentMailboxRegistry* mailbox;   // nullable
    DaemonManager* daemon_manager;   // nullable
    AnthropicClient* anthropic;        // nullable — native Claude API
    StateStoreDb*    persistent_store; // nullable — SQLite-backed agent-defs persistence
    // New typed DB layers
    WorkspaceDb*      workspace_db;    // nullable
    AgentDefDb*       agent_def_db;    // nullable
    WorkspaceDataDb*  ws_data_db;      // nullable
    WorkspaceOutputDb* ws_output_db;   // nullable
    AgentRunDb*       agent_run_db;    // nullable
    SwarmDb*          swarm_db;        // nullable
    SupabaseSync*     supabase;        // nullable — cloud sync
    JobQueue*         job_queue;       // nullable — async pipeline
};

class ApiServer {
public:
    explicit ApiServer(ApiContext ctx);
    ~ApiServer();

    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    bool start(uint16_t port, const std::string& api_key = "");
    void stop();
    bool is_running() const { return running_; }

private:
    ApiContext ctx_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> server_thread_;
    std::string api_key_;
    std::chrono::steady_clock::time_point start_time_;

    class Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<std::thread> scheduler_thread_;
    std::atomic<bool> scheduler_running_{false};

    void setup_routes();
    void run_scheduler();
    void fire_webhooks(const std::string& event_type, const nlohmann::json& data);
};

} // namespace clove
