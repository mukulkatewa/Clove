#include "mod.hpp"
#include <clove/tunnel_bridge.hpp>

namespace clove {

void TunnelSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_TUNNEL_CONNECT — connect to relay
    router.register_handler(SyscallOp::SYS_TUNNEL_CONNECT,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "tunnel not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_CONNECT, response.dump());
                }

                auto req = json::parse(msg.payload_str());
                TunnelConfig config;
                config.relay_url = req.value("relay_url", std::string{});
                config.machine_id = req.value("machine_id", std::string{});
                config.machine_token = req.value("machine_token", std::string{});
                config.auto_reconnect = req.value("auto_reconnect", false);

                bool ok = bridge_->connect(config);
                response["success"] = ok;
                if (ok) {
                    response["session_id"] = bridge_->session_id();
                } else {
                    response["error"] = "connection failed — check relay_url and machine_id";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_CONNECT, response.dump());
        });

    // SYS_TUNNEL_DISCONNECT — disconnect from relay
    router.register_handler(SyscallOp::SYS_TUNNEL_DISCONNECT,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "tunnel not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_DISCONNECT, response.dump());
                }
                bridge_->disconnect();
                response["success"] = true;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_DISCONNECT, response.dump());
        });

    // SYS_TUNNEL_STATUS — get tunnel state
    router.register_handler(SyscallOp::SYS_TUNNEL_STATUS,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "tunnel not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_STATUS, response.dump());
                }
                response["success"] = true;
                response.merge_patch(bridge_->status_json());
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_STATUS, response.dump());
        });

    // SYS_TUNNEL_LIST_REMOTES — list remote machines
    router.register_handler(SyscallOp::SYS_TUNNEL_LIST_REMOTES,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "tunnel not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_LIST_REMOTES, response.dump());
                }
                auto remotes = bridge_->list_remotes();
                json arr = json::array();
                for (const auto& r : remotes) {
                    arr.push_back({
                        {"machine_id", r.machine_id},
                        {"name", r.name},
                        {"status", r.status},
                        {"last_seen_ms", r.last_seen_ms}
                    });
                }
                response["success"] = true;
                response["remotes"] = arr;
                response["count"] = remotes.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_LIST_REMOTES, response.dump());
        });

    // SYS_TUNNEL_CONFIG — update tunnel config
    router.register_handler(SyscallOp::SYS_TUNNEL_CONFIG,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "tunnel not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_CONFIG, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                TunnelConfig config;
                config.relay_url = req.value("relay_url", std::string{});
                config.machine_id = req.value("machine_id", std::string{});
                config.machine_token = req.value("machine_token", std::string{});
                config.auto_reconnect = req.value("auto_reconnect", false);
                bridge_->update_config(config);
                response["success"] = true;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_TUNNEL_CONFIG, response.dump());
        });
}

} // namespace clove
