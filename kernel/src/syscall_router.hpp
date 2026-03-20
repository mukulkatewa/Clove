#pragma once

#include <clove/protocol.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <unordered_map>

namespace clove {

using SyscallHandler = std::function<Message(const Message&)>;

class SyscallRouter {
public:
    void register_handler(SyscallOp opcode, SyscallHandler handler) {
        handlers_[static_cast<uint8_t>(opcode)] = std::move(handler);
    }

    bool has_handler(SyscallOp opcode) const {
        return handlers_.count(static_cast<uint8_t>(opcode)) > 0;
    }

    Message handle(const Message& msg) const {
        auto it = handlers_.find(static_cast<uint8_t>(msg.header.opcode));
        if (it != handlers_.end()) {
            return it->second(msg);
        }
        // Unknown opcode — return error
        nlohmann::json response;
        response["success"] = false;
        response["error"] = "Unknown syscall opcode";
        return Message::create(msg.header.agent_id, msg.opcode(), response.dump());
    }

private:
    std::unordered_map<uint8_t, SyscallHandler> handlers_;
};

} // namespace clove
