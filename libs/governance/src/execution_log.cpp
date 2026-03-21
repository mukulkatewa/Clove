#include <clove/execution_log.hpp>

namespace clove {

ExecutionLogger::ExecutionLogger() = default;

// ---------------------------------------------------------------------------
// Recording control
// ---------------------------------------------------------------------------

bool ExecutionLogger::start_recording(const RecordingConfig& config) {
    std::lock_guard lock(mutex_);

    if (recording_state_.load(std::memory_order_relaxed) == RecordingState::RECORDING) return false;

    config_ = config;
    entries_.clear();
    entries_.reserve(std::min(config_.max_entries, size_t{4096}));
    next_sequence_ = 1;
    recording_state_.store(RecordingState::RECORDING, std::memory_order_release);
    return true;
}

bool ExecutionLogger::stop_recording() {
    std::lock_guard lock(mutex_);

    if (recording_state_.load(std::memory_order_relaxed) == RecordingState::IDLE) return false;

    recording_state_.store(RecordingState::IDLE, std::memory_order_release);
    return true;
}

bool ExecutionLogger::pause_recording() {
    std::lock_guard lock(mutex_);

    if (recording_state_.load(std::memory_order_relaxed) != RecordingState::RECORDING) return false;

    recording_state_.store(RecordingState::PAUSED, std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------------------
// Opcode filtering
// ---------------------------------------------------------------------------

bool ExecutionLogger::should_record(SyscallOp opcode) const {
    // Caller must hold mutex_
    switch (opcode) {
    case SyscallOp::SYS_THINK:
        return config_.include_think;

    case SyscallOp::SYS_HTTP:
        return config_.include_http;

    case SyscallOp::SYS_EXEC:
        return config_.include_exec;

    // Meta / bookkeeping ops are never recorded
    case SyscallOp::SYS_NOOP:
    case SyscallOp::SYS_HELLO:
    case SyscallOp::SYS_EXIT:
    case SyscallOp::SYS_ASYNC_POLL:
        return false;

    // Recording control ops are never recorded (avoid recursion)
    case SyscallOp::SYS_RECORD_START:
    case SyscallOp::SYS_RECORD_STOP:
    case SyscallOp::SYS_RECORD_STATUS:
    case SyscallOp::SYS_REPLAY_START:
    case SyscallOp::SYS_REPLAY_STATUS:
        return false;

    default:
        return true;
    }
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void ExecutionLogger::record(uint32_t agent_id, SyscallOp opcode,
                             const std::string& payload,
                             const std::string& response,
                             uint64_t duration_us, bool success) {
    // Fast path: skip mutex entirely when not recording.
    // This is the common case — recording is opt-in.
    if (recording_state_.load(std::memory_order_relaxed) != RecordingState::RECORDING) return;

    std::lock_guard lock(mutex_);

    // Re-check under lock (state may have changed).
    if (recording_state_.load(std::memory_order_relaxed) != RecordingState::RECORDING) return;
    if (!should_record(opcode)) return;
    if (entries_.size() >= config_.max_entries) return; // buffer full

    entries_.push_back({
        .sequence_id = next_sequence_++,
        .timestamp   = std::chrono::system_clock::now(),
        .agent_id    = agent_id,
        .opcode      = opcode,
        .payload     = payload,
        .response    = response,
        .duration_us = duration_us,
        .success     = success,
    });
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::vector<ExecutionLogEntry> ExecutionLogger::get_entries(size_t limit) const {
    std::lock_guard lock(mutex_);

    if (limit == 0 || limit >= entries_.size()) {
        return entries_;
    }

    return {entries_.begin(), entries_.begin() + static_cast<ptrdiff_t>(limit)};
}

size_t ExecutionLogger::entry_count() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

void ExecutionLogger::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    next_sequence_ = 1;
}

} // namespace clove
