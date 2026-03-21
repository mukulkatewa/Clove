#include "mod.hpp"
#include <clove/agent_manager.hpp>
#include <clove/permissions_store.hpp>
#include <clove/event_bus.hpp>

namespace clove {

void AgentSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_LIST — list all agents
    router.register_handler(SyscallOp::SYS_LIST,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto agents = ctx_.agent_manager.list_agents();
                json agents_arr = json::array();
                for (const auto& a : agents) {
                    agents_arr.push_back({
                        {"id", a->id()},
                        {"name", a->name()},
                        {"pid", a->pid()},
                        {"state", static_cast<uint8_t>(a->state())}
                    });
                }
                response["success"] = true;
                response["agents"] = agents_arr;
                response["count"] = agents.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_LIST, response.dump());
        });

    // SYS_SPAWN — spawn a new agent
    router.register_handler(SyscallOp::SYS_SPAWN,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                AgentConfig config;
                config.name = req.at("name").get<std::string>();
                config.script_path = req.at("script").get<std::string>();
                config.socket_path = ctx_.config.socket_path;
                config.sandboxed = req.value("sandboxed", true);

                auto agent = ctx_.agent_manager.spawn_agent(config);
                if (agent) {
                    response["success"] = true;
                    response["agent_id"] = agent->id();
                    response["name"] = agent->name();
                    response["pid"] = agent->pid();
                } else {
                    response["success"] = false;
                    response["error"] = "spawn failed";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_SPAWN, response.dump());
        });

    // SYS_KILL — kill an agent
    router.register_handler(SyscallOp::SYS_KILL,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                bool ok = false;
                if (req.contains("agent_id")) {
                    ok = ctx_.agent_manager.kill_agent(req["agent_id"].get<uint32_t>());
                } else if (req.contains("name")) {
                    ok = ctx_.agent_manager.kill_agent(req["name"].get<std::string>());
                }
                response["success"] = ok;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_KILL, response.dump());
        });

    // SYS_PAUSE — pause an agent
    router.register_handler(SyscallOp::SYS_PAUSE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                bool ok = false;
                if (req.contains("agent_id")) {
                    ok = ctx_.agent_manager.pause_agent(req["agent_id"].get<uint32_t>());
                } else if (req.contains("name")) {
                    ok = ctx_.agent_manager.pause_agent(req["name"].get<std::string>());
                }
                response["success"] = ok;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_PAUSE, response.dump());
        });

    // SYS_RESUME — resume a paused agent
    router.register_handler(SyscallOp::SYS_RESUME,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                bool ok = false;
                if (req.contains("agent_id")) {
                    ok = ctx_.agent_manager.resume_agent(req["agent_id"].get<uint32_t>());
                } else if (req.contains("name")) {
                    ok = ctx_.agent_manager.resume_agent(req["name"].get<std::string>());
                }
                response["success"] = ok;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_RESUME, response.dump());
        });
}

} // namespace clove
