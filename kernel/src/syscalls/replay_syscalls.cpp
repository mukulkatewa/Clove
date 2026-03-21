#include "mod.hpp"
#include "kernel/src/syscall_router.hpp"
#include <clove/execution_log.hpp>

namespace clove {

void ReplaySyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_RECORD_START — start recording syscalls
    router.register_handler(SyscallOp::SYS_RECORD_START,
        [this](const Message& msg) -> Message {
            json response;
            try {
                RecordingConfig config;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    config.max_entries = req.value("max_entries", size_t(50000));
                    config.include_think = req.value("include_think", false);
                    config.include_http = req.value("include_http", false);
                    config.include_exec = req.value("include_exec", false);
                }

                bool ok = ctx_.execution_logger.start_recording(config);
                response["success"] = ok;
                if (!ok) response["error"] = "already recording";
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_RECORD_START, response.dump());
        });

    // SYS_RECORD_STOP — stop recording
    router.register_handler(SyscallOp::SYS_RECORD_STOP,
        [this](const Message& msg) -> Message {
            json response;
            try {
                bool ok = ctx_.execution_logger.stop_recording();
                response["success"] = ok;
                response["entries_recorded"] = ctx_.execution_logger.entry_count();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_RECORD_STOP, response.dump());
        });

    // SYS_RECORD_STATUS — get recording status
    router.register_handler(SyscallOp::SYS_RECORD_STATUS,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto state = ctx_.execution_logger.recording_state();
                std::string state_str;
                switch (state) {
                    case RecordingState::IDLE:      state_str = "idle"; break;
                    case RecordingState::RECORDING:  state_str = "recording"; break;
                    case RecordingState::PAUSED:     state_str = "paused"; break;
                }
                response["success"] = true;
                response["state"] = state_str;
                response["entry_count"] = ctx_.execution_logger.entry_count();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_RECORD_STATUS, response.dump());
        });

    // SYS_REPLAY_START — load recorded entries and begin replay
    router.register_handler(SyscallOp::SYS_REPLAY_START,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (replay_state_ == ReplayState::RUNNING) {
                    response["success"] = false;
                    response["error"] = "replay already running";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_REPLAY_START, response.dump());
                }

                // Load entries from execution logger
                replay_entries_ = ctx_.execution_logger.get_entries();
                if (replay_entries_.empty()) {
                    response["success"] = false;
                    response["error"] = "no recorded entries to replay";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_REPLAY_START, response.dump());
                }

                replay_index_ = 0;
                replay_state_ = ReplayState::RUNNING;
                response["success"] = true;
                response["total_entries"] = replay_entries_.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_REPLAY_START, response.dump());
        });

    // SYS_REPLAY_STATUS — report replay progress
    router.register_handler(SyscallOp::SYS_REPLAY_STATUS,
        [this](const Message& msg) -> Message {
            json response;
            try {
                std::string state_str;
                switch (replay_state_) {
                    case ReplayState::IDLE:      state_str = "idle"; break;
                    case ReplayState::RUNNING:   state_str = "running"; break;
                    case ReplayState::PAUSED:    state_str = "paused"; break;
                    case ReplayState::COMPLETED: state_str = "completed"; break;
                    case ReplayState::ERROR:     state_str = "error"; break;
                }
                response["success"] = true;
                response["state"] = state_str;
                response["total_entries"] = replay_entries_.size();
                response["entries_replayed"] = replay_index_;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_REPLAY_STATUS, response.dump());
        });
}

void ReplaySyscalls::on_tick() {
    if (replay_state_ != ReplayState::RUNNING || !router_) return;
    if (replay_index_ >= replay_entries_.size()) {
        replay_state_ = ReplayState::COMPLETED;
        return;
    }

    // Replay one entry per tick
    const auto& entry = replay_entries_[replay_index_];
    auto msg = Message::create(entry.agent_id, entry.opcode, entry.payload);
    router_->handle(msg);
    replay_index_++;

    if (replay_index_ >= replay_entries_.size()) {
        replay_state_ = ReplayState::COMPLETED;
    }
}

} // namespace clove
