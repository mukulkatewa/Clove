#pragma once

#include <clove/protocol.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace clove {

struct ExecutionLogEntry {
    uint64_t                                sequence_id;
    std::chrono::system_clock::time_point   timestamp;
    uint32_t                                agent_id;
    SyscallOp                               opcode;
    std::string                             payload;
    std::string                             response;
    uint64_t                                duration_us;
    bool                                    success;
};

enum class RecordingState { IDLE, RECORDING, PAUSED };
enum class ReplayState    { IDLE, RUNNING, PAUSED, COMPLETED, ERROR };

struct RecordingConfig {
    size_t max_entries   = 50000;
    bool   include_think = false;
    bool   include_http  = false;
    bool   include_exec  = false;
};

class ExecutionLogger {
public:
    ExecutionLogger();

    // Recording control
    bool start_recording(const RecordingConfig& config = {});
    bool stop_recording();
    bool pause_recording();
    [[nodiscard]] RecordingState recording_state() const { return recording_state_; }

    /// Record a syscall (called by kernel after each syscall).
    void record(uint32_t agent_id, SyscallOp opcode,
                const std::string& payload, const std::string& response,
                uint64_t duration_us, bool success);

    /// Get recorded entries.
    [[nodiscard]] std::vector<ExecutionLogEntry> get_entries(
        size_t limit = 0) const;

    /// Get total recorded entry count.
    [[nodiscard]] size_t entry_count() const;

    /// Clear all recordings.
    void clear();

private:
    mutable std::mutex              mutex_;
    std::atomic<RecordingState>     recording_state_{RecordingState::IDLE};
    RecordingConfig                 config_;
    std::vector<ExecutionLogEntry>  entries_;
    uint64_t                        next_sequence_ = 1;

    [[nodiscard]] bool should_record(SyscallOp opcode) const;
};

} // namespace clove
