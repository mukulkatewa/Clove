#pragma once

#include <clove/config.hpp>
#include <clove/protocol.hpp>
#include <atomic>
#include <memory>
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

    // Routing
    std::unique_ptr<SyscallRouter> syscall_router_;
    std::unique_ptr<KernelContext> context_;
    std::vector<std::unique_ptr<KernelModule>> modules_;

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
