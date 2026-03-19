#include <clove/agent_process.hpp>
#include <atomic>
#include <chrono>
#include <sys/wait.h>

namespace clove {

static std::atomic<uint32_t> g_next_id{1};

uint32_t AgentProcess::generate_id() {
    return g_next_id.fetch_add(1, std::memory_order_relaxed);
}

AgentProcess::AgentProcess(const AgentConfig& config)
    : config_(config), id_(generate_id()) {
    auto now = std::chrono::system_clock::now();
    created_at_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

AgentProcess::~AgentProcess() {
    if (state_ == AgentState::RUNNING || state_ == AgentState::PAUSED) {
        stop();
    }
}

bool AgentProcess::start() {
    if (state_ == AgentState::RUNNING) return false;

    set_state(AgentState::STARTING);

    SandboxConfig sbox_config;
    sbox_config.name = config_.name;
    sbox_config.socket_path = config_.socket_path;
    sbox_config.limits = config_.limits;
    sbox_config.enable_network = config_.enable_network;

    sandbox_ = std::make_unique<Sandbox>(sbox_config);
    if (!sandbox_->create()) {
        set_state(AgentState::FAILED);
        return false;
    }

    std::vector<std::string> args = build_args();
    if (!sandbox_->start(config_.script_path.empty() ? config_.python_path : config_.python_path, args)) {
        set_state(AgentState::FAILED);
        return false;
    }

    set_state(AgentState::RUNNING);
    return true;
}

bool AgentProcess::stop(int timeout_ms) {
    if (!sandbox_) return true;
    bool ok = sandbox_->stop(timeout_ms);
    exit_code_ = sandbox_->exit_code();
    set_state(AgentState::STOPPED);
    return ok;
}

bool AgentProcess::restart() {
    stop();
    return start();
}

bool AgentProcess::pause() {
    if (!sandbox_ || state_ != AgentState::RUNNING) return false;
    if (sandbox_->pause()) {
        set_state(AgentState::PAUSED);
        return true;
    }
    return false;
}

bool AgentProcess::resume() {
    if (!sandbox_ || state_ != AgentState::PAUSED) return false;
    if (sandbox_->resume()) {
        set_state(AgentState::RUNNING);
        return true;
    }
    return false;
}

pid_t AgentProcess::pid() const {
    return sandbox_ ? sandbox_->pid() : -1;
}

int AgentProcess::wait() {
    if (!sandbox_) return -1;
    int code = sandbox_->wait();
    exit_code_ = code;
    set_state(AgentState::STOPPED);
    return code;
}

bool AgentProcess::is_running() const {
    if (!sandbox_ || state_ != AgentState::RUNNING) return false;
    return sandbox_->is_running();
}

int AgentProcess::exit_code() const {
    if (sandbox_) return sandbox_->exit_code();
    return exit_code_;
}

void AgentProcess::set_event_callback(AgentEventCallback callback) {
    event_callback_ = std::move(callback);
}

AgentMetrics AgentProcess::get_metrics() const {
    AgentMetrics m{};
    m.id = id_;
    m.name = config_.name;
    m.pid = pid();
    m.state = state_;
    m.llm_request_count = llm_request_count_;
    m.llm_tokens_used = llm_tokens_used_;
    m.parent_id = parent_id_;
    m.child_ids = child_ids_;
    m.created_at_ms = created_at_ms_;

    if (created_at_ms_ > 0) {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        m.uptime_seconds = static_cast<uint64_t>((now_ms - created_at_ms_) / 1000);
    }
    return m;
}

void AgentProcess::record_llm_call(int tokens) {
    llm_request_count_++;
    llm_tokens_used_ += tokens;
}

IsolationStatus AgentProcess::get_isolation_status() const {
    if (sandbox_) return sandbox_->isolation_status();
    return {};
}

void AgentProcess::add_child(uint32_t child_id) {
    child_ids_.push_back(child_id);
}

void AgentProcess::set_state(AgentState new_state) {
    state_ = new_state;
    if (event_callback_) {
        event_callback_(this, new_state);
    }
}

std::vector<std::string> AgentProcess::build_args() const {
    std::vector<std::string> args;
    if (!config_.script_path.empty()) {
        args.push_back(config_.script_path);
    }
    return args;
}

} // namespace clove
