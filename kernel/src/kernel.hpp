#pragma once

#include <clove/config.hpp>
#include <clove/protocol.hpp>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace clove {

// Forward declarations
class Reactor;
class SocketServer;
class AgentManager;
class AgentMailboxRegistry;
class StateStore;
class EventBus;
class LlmQueue;
class AsyncTaskManager;
class PermissionsStore;
class InferenceGateway;
class PrivacyFilter;
class AuditLogger;
class ExecutionLogger;
class PolicyWatcher;
class PolicyRecommender;
class SyscallRouter;
class KernelModule;
class Database;
class AuditStore;
class StateStoreDb;
class McpBridge;
class A2aBridge;
class TunnelBridge;
class WorldEngine;
class ApiServer;
class ArtifactStore;
class ChainStore;
class ContextAssembler;
class ArtifactStoreDb;
class MemoryBlockStore;
class MemoryBlockDb;
class AgentScheduler;
class OpenRouterClient;
class SandboxManager;
class OpenClawManager;
struct KernelContext;
struct Manifest;

class Kernel {
public:
    Kernel();
    explicit Kernel(const KernelConfig& config);
    ~Kernel();

    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    bool init();
    void run();
    void shutdown();
    bool is_running() const { return running_; }

    AgentManager& agents();
    const KernelConfig& get_config() const { return config_; }

private:
    KernelConfig config_;
    std::atomic<bool> running_{false};

    // Subsystems (owned)
    std::unique_ptr<Reactor> reactor_;
    std::unique_ptr<SocketServer> socket_server_;
    std::unique_ptr<AgentManager> agent_manager_;
    std::unique_ptr<AgentMailboxRegistry> mailbox_registry_;
    std::unique_ptr<StateStore> state_store_;
    std::unique_ptr<EventBus> event_bus_;
    std::unique_ptr<LlmQueue> llm_queue_;
    std::unique_ptr<AsyncTaskManager> async_tasks_;
    std::unique_ptr<PermissionsStore> permissions_store_;
    std::unique_ptr<InferenceGateway> inference_gateway_;
    std::unique_ptr<PrivacyFilter> privacy_filter_;
    std::unique_ptr<AuditLogger> audit_logger_;
    std::unique_ptr<ExecutionLogger> execution_logger_;
    std::unique_ptr<PolicyWatcher> policy_watcher_;
    std::unique_ptr<PolicyRecommender> policy_recommender_;

    // Persistence
    std::unique_ptr<Database> database_;
    std::unique_ptr<AuditStore> audit_store_;
    std::unique_ptr<StateStoreDb> state_store_db_;
    std::unique_ptr<McpBridge> mcp_bridge_;
    std::unique_ptr<A2aBridge> a2a_bridge_;
    std::unique_ptr<TunnelBridge> tunnel_bridge_;
    std::unique_ptr<WorldEngine> world_engine_;
    std::unique_ptr<ApiServer> api_server_;
    std::unique_ptr<ArtifactStore> artifact_store_;
    std::unique_ptr<ChainStore> chain_store_;
    std::unique_ptr<ContextAssembler> context_assembler_;
    std::unique_ptr<ArtifactStoreDb> artifact_store_db_;
    std::unique_ptr<MemoryBlockStore> memory_block_store_;
    std::unique_ptr<MemoryBlockDb> memory_block_db_;
    std::unique_ptr<AgentScheduler> scheduler_;
    std::shared_ptr<OpenRouterClient> openrouter_;
    std::unique_ptr<SandboxManager> sandbox_manager_;
    std::unique_ptr<OpenClawManager> openclaw_manager_;

    // Routing
    std::unique_ptr<SyscallRouter> syscall_router_;
    std::unique_ptr<KernelContext> context_;
    std::vector<std::unique_ptr<KernelModule>> modules_;

    // Tracks per-fd writable registration to avoid redundant kqueue modify calls.
    std::unordered_map<int, bool> client_write_state_;

    // Event handlers
    void on_server_event(int fd, uint32_t events);
    void on_client_event(int fd, uint32_t events);
    void update_client_events(int fd);
    Message handle_message(const Message& msg);

    // Lifecycle
    bool load_manifest(const std::string& path);
    void on_policy_change(const std::string& contents);
};

} // namespace clove
