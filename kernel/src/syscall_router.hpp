#pragma once

#include <clove/protocol.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <functional>
#include <unordered_map>

namespace clove {

class PermissionsStore;
class EventBus;
class AuditLogger;

using SyscallHandler = std::function<Message(const Message&)>;

class SyscallRouter {
public:
    void register_handler(SyscallOp opcode, SyscallHandler handler) {
        handlers_[static_cast<uint8_t>(opcode)] = std::move(handler);
    }

    bool has_handler(SyscallOp opcode) const {
        return handlers_.count(static_cast<uint8_t>(opcode)) > 0;
    }

    // Wire budget enforcement subsystems (called from Kernel constructor)
    void set_budget_enforcement(PermissionsStore* store, EventBus* bus, AuditLogger* audit) {
        budget_store_ = store;
        event_bus_ = bus;
        audit_ = audit;
    }

    Message handle(const Message& msg) const;

private:
    std::unordered_map<uint8_t, SyscallHandler> handlers_;

    // Budget enforcement (optional, null if disabled)
    PermissionsStore* budget_store_ = nullptr;
    EventBus* event_bus_ = nullptr;
    AuditLogger* audit_ = nullptr;

    static uint64_t now_ms() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }
};

} // namespace clove
