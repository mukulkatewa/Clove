#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>

namespace clove {

class StateStore;
class AuditLogger;
class MemoryBlockStore;
class OpenRouterClient;
class InferenceGateway;
class PrivacyFilter;
class PermissionsStore;
class ArtifactStore;
class ChainStore;
class ContextAssembler;
class McpBridge;
struct KernelConfig;

struct DaemonConfig {
    bool enabled = false;
    int tick_interval_s = 60;
    int blocking_budget_ms = 15000;
    nlohmann::json subscriptions = nlohmann::json::array();
    bool dream_enabled = true;
    int dream_min_hours = 24;
    int dream_min_runs = 5;
};

struct DaemonState {
    std::string agent_name;
    std::string status; // running, sleeping, acting, dreaming, stopped
    std::chrono::steady_clock::time_point started_at;
    std::string last_tick_at;
    int ticks_total = 0;
    int actions_today = 0;
    double cost_today = 0.0;
    std::string last_consolidated;
    int runs_since_consolidation = 0;
    std::string current_action;
};

/// Manages always-on daemon agents with tick loops, subscriptions, and memory consolidation.
class DaemonManager {
public:
    using RunAgentFn = std::function<nlohmann::json(const std::string& agent_name, const std::string& goal, double budget)>;

    DaemonManager(StateStore& state_store, AuditLogger& audit_logger,
                  MemoryBlockStore* memory_blocks);

    /// Set the function that actually runs an agent (wired from ApiServer)
    void set_run_fn(RunAgentFn fn) { run_fn_ = fn; }

    /// Start a daemon for the named agent
    bool start_daemon(const std::string& agent_name);

    /// Stop a daemon
    bool stop_daemon(const std::string& agent_name);

    /// Stop all daemons (for shutdown)
    void stop_all();

    /// List all running daemons
    nlohmann::json list_daemons() const;

    /// Get state of a specific daemon
    nlohmann::json get_state(const std::string& agent_name) const;

    /// Get logs for a daemon (returns last N entries)
    nlohmann::json get_logs(const std::string& agent_name, int limit = 50) const;

    /// Trigger manual memory consolidation
    bool dream(const std::string& agent_name);

    /// Is this agent running as a daemon?
    bool is_running(const std::string& agent_name) const;

private:
    StateStore& state_store_;
    AuditLogger& audit_logger_;
    MemoryBlockStore* memory_blocks_;
    RunAgentFn run_fn_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, DaemonState> daemons_;
    std::unordered_map<std::string, std::unique_ptr<std::thread>> threads_;
    std::unordered_map<std::string, std::atomic<bool>> running_flags_;

    void tick_loop(const std::string& agent_name);
    nlohmann::json check_subscriptions(const std::string& agent_name, const DaemonConfig& config);
    void append_log(const std::string& agent_name, const std::string& type,
                    const std::string& detail, double cost = 0.0);
    void maybe_dream(const std::string& agent_name);
    DaemonConfig load_daemon_config(const std::string& agent_name);
    std::string now_iso() const;
};

} // namespace clove
