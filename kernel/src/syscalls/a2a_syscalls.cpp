#include "mod.hpp"
#include <clove/a2a_bridge.hpp>
#include <clove/audit_log.hpp>

namespace clove {

void A2aSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_A2A_SEND — send message to external A2A agent
    router.register_handler(SyscallOp::SYS_A2A_SEND,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "A2A not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_A2A_SEND, response.dump());
                }

                auto req = json::parse(msg.payload_str());
                std::string target_url = req.at("url").get<std::string>();
                std::string from = req.value("from", "agent_" + std::to_string(msg.agent_id()));
                std::string content = req.at("content").get<std::string>();
                json metadata = req.value("metadata", json::object());

                ctx_.audit_logger.log(AuditCategory::NETWORK, "A2A_SEND",
                    msg.agent_id(), from, {{"target", target_url}});

                auto result = bridge_->send(target_url, from, content, metadata);

                response["success"] = result.success;
                response["status_code"] = result.status_code;
                response["response"] = result.response_content;
                response["duration_ms"] = result.duration_ms;
                if (!result.error.empty()) response["error"] = result.error;

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_A2A_SEND, response.dump());
        });

    // SYS_A2A_RECV — receive inbound A2A messages
    router.register_handler(SyscallOp::SYS_A2A_RECV,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "A2A not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_A2A_RECV, response.dump());
                }

                auto req = json::parse(msg.payload_str());
                std::string agent_name = req.at("agent_name").get<std::string>();
                size_t max = req.value("max", size_t(100));

                auto messages = bridge_->receive(agent_name, max);
                json msgs_arr = json::array();
                for (const auto& m : messages) {
                    msgs_arr.push_back({
                        {"from", m.from},
                        {"to", m.to},
                        {"content", m.content},
                        {"metadata", m.metadata},
                        {"timestamp_ms", m.timestamp_ms}
                    });
                }

                response["success"] = true;
                response["messages"] = msgs_arr;
                response["count"] = messages.size();

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_A2A_RECV, response.dump());
        });
}

} // namespace clove
