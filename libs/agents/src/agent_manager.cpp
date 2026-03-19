#include <clove/agent_manager.hpp>
#include <sys/wait.h>
#include <cstdlib>

namespace clove {

AgentManager::AgentManager(const std::string& kernel_socket)
    : kernel_socket_(kernel_socket) {}

AgentManager::~AgentManager() {
    stop_all();
}

std::shared_ptr<AgentProcess> AgentManager::spawn_agent(const AgentConfig& config) {
    if (agents_by_name_.count(config.name)) return nullptr;

    AgentConfig final_config = config;
    if (final_config.socket_path.empty()) {
        final_config.socket_path = kernel_socket_;
    }

    // Inject egress proxy env var if configured
    if (!egress_proxy_url_.empty()) {
        setenv("CLOVE_EGRESS_PROXY", egress_proxy_url_.c_str(), 1);
    }

    auto agent = std::make_shared<AgentProcess>(final_config);
    if (!agent->start()) return nullptr;

    agents_by_name_[config.name] = agent;
    agents_by_id_[agent->id()] = agent;
    if (agent->pid() > 0) {
        pid_to_name_[agent->pid()] = config.name;
    }

    if (config.restart.policy != RestartPolicy::NEVER) {
        saved_configs_[config.name] = final_config;
        if (restart_states_.find(config.name) == restart_states_.end()) {
            RestartState state;
            state.window_start = std::chrono::steady_clock::now();
            restart_states_[config.name] = state;
        }
    }

    return agent;
}

std::shared_ptr<AgentProcess> AgentManager::get_agent(const std::string& name) {
    auto it = agents_by_name_.find(name);
    return (it != agents_by_name_.end()) ? it->second : nullptr;
}

std::shared_ptr<AgentProcess> AgentManager::get_agent(uint32_t id) {
    auto it = agents_by_id_.find(id);
    return (it != agents_by_id_.end()) ? it->second : nullptr;
}

bool AgentManager::kill_agent(const std::string& name) {
    auto it = agents_by_name_.find(name);
    if (it == agents_by_name_.end()) return false;
    auto agent = it->second;
    pid_to_name_.erase(agent->pid());
    agent->stop();
    agents_by_id_.erase(agent->id());
    agents_by_name_.erase(it);
    return true;
}

bool AgentManager::kill_agent(uint32_t id) {
    auto it = agents_by_id_.find(id);
    if (it == agents_by_id_.end()) return false;
    auto agent = it->second;
    pid_to_name_.erase(agent->pid());
    agent->stop();
    agents_by_name_.erase(agent->name());
    agents_by_id_.erase(it);
    return true;
}

bool AgentManager::pause_agent(const std::string& name) {
    auto it = agents_by_name_.find(name);
    return (it != agents_by_name_.end()) ? it->second->pause() : false;
}

bool AgentManager::pause_agent(uint32_t id) {
    auto it = agents_by_id_.find(id);
    return (it != agents_by_id_.end()) ? it->second->pause() : false;
}

bool AgentManager::resume_agent(const std::string& name) {
    auto it = agents_by_name_.find(name);
    return (it != agents_by_name_.end()) ? it->second->resume() : false;
}

bool AgentManager::resume_agent(uint32_t id) {
    auto it = agents_by_id_.find(id);
    return (it != agents_by_id_.end()) ? it->second->resume() : false;
}

std::vector<std::shared_ptr<AgentProcess>> AgentManager::list_agents() const {
    std::vector<std::shared_ptr<AgentProcess>> result;
    for (const auto& [_, agent] : agents_by_name_) {
        result.push_back(agent);
    }
    return result;
}

void AgentManager::stop_all() {
    for (auto& [_, agent] : agents_by_name_) {
        agent->stop();
    }
    agents_by_name_.clear();
    agents_by_id_.clear();
    pid_to_name_.clear();
}

void AgentManager::set_restart_event_callback(RestartEventCallback callback) {
    restart_event_callback_ = std::move(callback);
}

uint32_t AgentManager::calculate_backoff_delay(const RestartConfig& config, uint32_t consecutive_failures) {
    if (consecutive_failures == 0) return config.backoff_initial_ms;
    double delay = config.backoff_initial_ms;
    for (uint32_t i = 0; i < consecutive_failures; ++i) {
        delay *= config.backoff_multiplier;
        if (delay >= config.backoff_max_ms) return config.backoff_max_ms;
    }
    return static_cast<uint32_t>(delay);
}

void AgentManager::notify_child_exit(pid_t pid, int status) {
    auto pit = pid_to_name_.find(pid);
    if (pit == pid_to_name_.end()) return;

    std::string name = pit->second;
    pid_to_name_.erase(pit);

    auto ait = agents_by_name_.find(name);
    if (ait == agents_by_name_.end()) return;

    auto agent = ait->second;
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    agents_by_id_.erase(agent->id());
    agents_by_name_.erase(name);

    auto config_it = saved_configs_.find(name);
    if (config_it == saved_configs_.end()) return;

    const AgentConfig& config = config_it->second;
    RestartState& state = restart_states_[name];

    bool should_restart = false;
    switch (config.restart.policy) {
        case RestartPolicy::ALWAYS: should_restart = true; break;
        case RestartPolicy::ON_FAILURE: should_restart = (exit_code != 0); break;
        default: break;
    }

    if (!should_restart) {
        saved_configs_.erase(name);
        restart_states_.erase(name);
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto window_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - state.window_start).count();

    if (window_elapsed >= config.restart.restart_window_sec) {
        state.window_start = now;
        state.restart_count = 0;
        state.consecutive_failures = 0;
    }

    if (state.restart_count >= config.restart.max_restarts) {
        if (!state.escalated) {
            state.escalated = true;
            if (restart_event_callback_) {
                restart_event_callback_("AGENT_ESCALATED", name, state.restart_count, exit_code);
            }
        }
        return;
    }

    uint32_t backoff_ms = calculate_backoff_delay(config.restart, state.consecutive_failures);

    PendingRestart pending;
    pending.agent_name = name;
    pending.scheduled_time = now + std::chrono::milliseconds(backoff_ms);
    pending.config = config;
    pending_restarts_.push_back(pending);

    state.restart_count++;
    state.consecutive_failures++;

    if (restart_event_callback_) {
        restart_event_callback_("AGENT_RESTARTING", name, state.restart_count, exit_code);
    }
}

void AgentManager::reap_and_restart_agents() {
    std::vector<std::string> dead;
    for (auto& [name, agent] : agents_by_name_) {
        if (!agent->is_running() && agent->state() == AgentState::RUNNING) {
            dead.push_back(name);
        }
    }

    for (const auto& name : dead) {
        auto agent = agents_by_name_[name];
        int exit_code = agent->exit_code();
        pid_to_name_.erase(agent->pid());
        agents_by_id_.erase(agent->id());
        agents_by_name_.erase(name);

        auto config_it = saved_configs_.find(name);
        if (config_it == saved_configs_.end()) continue;

        const AgentConfig& config = config_it->second;
        RestartState& state = restart_states_[name];

        bool should_restart = false;
        switch (config.restart.policy) {
            case RestartPolicy::ALWAYS: should_restart = true; break;
            case RestartPolicy::ON_FAILURE: should_restart = (exit_code != 0); break;
            default: break;
        }
        if (!should_restart) {
            saved_configs_.erase(name);
            restart_states_.erase(name);
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - state.window_start).count();
        if (elapsed >= config.restart.restart_window_sec) {
            state.window_start = now;
            state.restart_count = 0;
            state.consecutive_failures = 0;
        }
        if (state.restart_count >= config.restart.max_restarts) {
            if (!state.escalated) {
                state.escalated = true;
                if (restart_event_callback_) {
                    restart_event_callback_("AGENT_ESCALATED", name, state.restart_count, exit_code);
                }
            }
            continue;
        }

        uint32_t backoff_ms = calculate_backoff_delay(config.restart, state.consecutive_failures);
        PendingRestart pending;
        pending.agent_name = name;
        pending.scheduled_time = now + std::chrono::milliseconds(backoff_ms);
        pending.config = config;
        pending_restarts_.push_back(pending);
        state.restart_count++;
        state.consecutive_failures++;

        if (restart_event_callback_) {
            restart_event_callback_("AGENT_RESTARTING", name, state.restart_count, exit_code);
        }
    }
}

void AgentManager::process_pending_restarts() {
    if (pending_restarts_.empty()) return;

    auto now = std::chrono::steady_clock::now();
    std::vector<PendingRestart> still_pending;

    for (auto& pending : pending_restarts_) {
        if (now >= pending.scheduled_time) {
            auto agent = std::make_shared<AgentProcess>(pending.config);
            if (agent->start()) {
                agents_by_name_[pending.agent_name] = agent;
                agents_by_id_[agent->id()] = agent;
                if (agent->pid() > 0) {
                    pid_to_name_[agent->pid()] = pending.agent_name;
                }
            }
        } else {
            still_pending.push_back(pending);
        }
    }

    pending_restarts_ = std::move(still_pending);
}

} // namespace clove
