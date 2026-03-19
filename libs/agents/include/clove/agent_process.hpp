#pragma once
#include <clove/types.hpp>
#include <clove/sandbox.hpp>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <cstdint>
#include <chrono>

namespace clove {

class AgentProcess;
using AgentEventCallback = std::function<void(AgentProcess*, AgentState)>;

class AgentProcess {
public:
    explicit AgentProcess(const AgentConfig& config);
    ~AgentProcess();

    AgentProcess(const AgentProcess&) = delete;
    AgentProcess& operator=(const AgentProcess&) = delete;

    bool start();
    bool stop(int timeout_ms = 5000);
    bool restart();
    bool pause();
    bool resume();

    AgentState state() const { return state_; }
    const std::string& name() const { return config_.name; }
    uint32_t id() const { return id_; }
    pid_t pid() const;
    int wait();
    bool is_running() const;
    int exit_code() const;

    void set_event_callback(AgentEventCallback callback);

    AgentMetrics get_metrics() const;
    void record_llm_call(int tokens);
    IsolationStatus get_isolation_status() const;

    void set_parent_id(uint32_t parent_id) { parent_id_ = parent_id; }
    uint32_t parent_id() const { return parent_id_; }
    void add_child(uint32_t child_id);
    const std::vector<uint32_t>& child_ids() const { return child_ids_; }

    static uint32_t generate_id();

private:
    AgentConfig config_;
    AgentState state_ = AgentState::CREATED;
    uint32_t id_;
    int exit_code_ = -1;

    std::unique_ptr<Sandbox> sandbox_;
    AgentEventCallback event_callback_;

    uint64_t llm_request_count_ = 0;
    uint64_t llm_tokens_used_ = 0;
    uint32_t parent_id_ = 0;
    std::vector<uint32_t> child_ids_;
    uint64_t created_at_ms_ = 0;

    void set_state(AgentState new_state);
    std::vector<std::string> build_args() const;
};

} // namespace clove
