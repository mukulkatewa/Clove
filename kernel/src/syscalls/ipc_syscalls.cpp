#include "mod.hpp"
#include <clove/mailbox.hpp>

namespace clove {

void IpcSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_REGISTER — register agent name for mailbox addressing
    router.register_handler(SyscallOp::SYS_REGISTER,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string name = req.at("name").get<std::string>();

                ctx_.mailbox_registry.register_agent(msg.agent_id(), name);
                response["success"] = true;
                response["agent_id"] = msg.agent_id();
                response["name"] = name;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_REGISTER, response.dump());
        });

    // SYS_SEND — send a message to another agent
    router.register_handler(SyscallOp::SYS_SEND,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string content = req.at("content").get<std::string>();

                bool ok = false;
                if (req.contains("to_id")) {
                    uint32_t to_id = req["to_id"].get<uint32_t>();
                    ok = ctx_.mailbox_registry.send(msg.agent_id(), to_id, content);
                } else if (req.contains("to_name")) {
                    std::string to_name = req["to_name"].get<std::string>();
                    ok = ctx_.mailbox_registry.send_by_name(msg.agent_id(), to_name, content);
                } else {
                    response["success"] = false;
                    response["error"] = "must specify to_id or to_name";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_SEND, response.dump());
                }

                response["success"] = ok;
                if (!ok) response["error"] = "delivery failed";
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_SEND, response.dump());
        });

    // SYS_RECV — receive pending messages
    router.register_handler(SyscallOp::SYS_RECV,
        [this](const Message& msg) -> Message {
            json response;
            try {
                size_t max_count = 100;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    max_count = req.value("max_count", 100);
                }

                auto messages = ctx_.mailbox_registry.receive(msg.agent_id(), max_count);
                json msg_array = json::array();
                for (const auto& m : messages) {
                    msg_array.push_back({
                        {"from_agent_id", m.from_agent_id},
                        {"to_agent_id", m.to_agent_id},
                        {"content", m.content},
                        {"timestamp_ms", m.timestamp_ms}
                    });
                }
                response["success"] = true;
                response["messages"] = msg_array;
                response["count"] = messages.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_RECV, response.dump());
        });

    // SYS_BROADCAST — broadcast to all agents
    router.register_handler(SyscallOp::SYS_BROADCAST,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string content = req.at("content").get<std::string>();

                ctx_.mailbox_registry.broadcast(msg.agent_id(), content);
                response["success"] = true;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_BROADCAST, response.dump());
        });
}

} // namespace clove
