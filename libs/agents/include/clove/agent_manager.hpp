#pragma once
#include <clove/agent_process.hpp>
#include <clove/sandbox.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <chrono>

namespace clove {

class AgentManager {
public:
    explicit AgentManager(const std::string& kernel_socket);
    ~AgentManager();

    std::shared_ptr<AgentProcess> spawn_agent(const AgentConfig& config);
    std::shared_ptr<AgentProcess> get_agent(const std::string& name);
    std::shared_ptr<AgentProcess> get_agent(uint32_t id);
    bool kill_agent(const std::string& name);
    bool kill_agent(uint32_t id);
    bool pause_agent(const std::string& name);
    bool pause_agent(uint32_t id);
    bool resume_agent(const std::string& name);
    bool resume_agent(uint32_t id);
    std::vector<std::shared_ptr<AgentProcess>> list_agents() const;
    void stop_all();

    void reap_and_restart_agents();
    void process_pending_restarts();

    using RestartEventCallback = std::function<void(
        const std::string& event_type, const std::string& agent_name,
        uint32_t restart_count, int exit_code)>;
    void set_restart_event_callback(RestartEventCallback callback);

    void notify_child_exit(pid_t pid, int status);
    void set_egress_proxy(const std::string& url) { egress_proxy_url_ = url; }

private:
    std::string kernel_socket_;
    std::unordered_map<std::string, std::shared_ptr<AgentProcess>> agents_by_name_;
    std::unordered_map<uint32_t, std::shared_ptr<AgentProcess>> agents_by_id_;
    std::unordered_map<pid_t, std::string> pid_to_name_;
    SandboxManager sandbox_manager_;

    struct RestartState {
        uint32_t restart_count = 0;
        std::chrono::steady_clock::time_point window_start;
        uint32_t consecutive_failures = 0;
        bool escalated = false;
    };
    std::unordered_map<std::string, RestartState> restart_states_;
    std::unordered_map<std::string, AgentConfig> saved_configs_;

    struct PendingRestart {
        std::string agent_name;
        std::chrono::steady_clock::time_point scheduled_time;
        AgentConfig config;
    };
    std::vector<PendingRestart> pending_restarts_;
    RestartEventCallback restart_event_callback_;
    std::string egress_proxy_url_;

    uint32_t calculate_backoff_delay(const RestartConfig& config, uint32_t consecutive_failures);
};

} // namespace clove
