#include "mod.hpp"
#include <clove/world_engine.hpp>

namespace clove {

void WorldSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_WORLD_CREATE — create a new isolated world
    router.register_handler(SyscallOp::SYS_WORLD_CREATE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_CREATE, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                std::string name = req.value("name", "unnamed");
                json metadata = req.value("metadata", json::object());
                uint32_t world_id = engine_->create(name, metadata);
                response["success"] = true;
                response["world_id"] = world_id;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_CREATE, response.dump());
        });

    // SYS_WORLD_DESTROY — destroy a world
    router.register_handler(SyscallOp::SYS_WORLD_DESTROY,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_DESTROY, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                uint32_t world_id = req.at("world_id").get<uint32_t>();
                bool ok = engine_->destroy(world_id);
                response["success"] = ok;
                if (!ok) response["error"] = "world not found";
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_DESTROY, response.dump());
        });

    // SYS_WORLD_LIST — list all worlds
    router.register_handler(SyscallOp::SYS_WORLD_LIST,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_LIST, response.dump());
                }
                auto worlds = engine_->list();
                json arr = json::array();
                for (const auto& w : worlds) {
                    arr.push_back({
                        {"world_id", w.id},
                        {"name", w.name},
                        {"member_count", w.member_count},
                        {"metadata", w.metadata}
                    });
                }
                response["success"] = true;
                response["worlds"] = arr;
                response["count"] = worlds.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_LIST, response.dump());
        });

    // SYS_WORLD_JOIN — join an agent to a world
    router.register_handler(SyscallOp::SYS_WORLD_JOIN,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_JOIN, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                uint32_t world_id = req.at("world_id").get<uint32_t>();
                bool ok = engine_->join(world_id, msg.agent_id());
                response["success"] = ok;
                if (!ok) response["error"] = "world not found";
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_JOIN, response.dump());
        });

    // SYS_WORLD_LEAVE — remove agent from a world
    router.register_handler(SyscallOp::SYS_WORLD_LEAVE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_LEAVE, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                uint32_t world_id = req.at("world_id").get<uint32_t>();
                bool ok = engine_->leave(world_id, msg.agent_id());
                response["success"] = ok;
                if (!ok) response["error"] = "world not found or agent not a member";
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_LEAVE, response.dump());
        });

    // SYS_WORLD_EVENT — emit a world-scoped event
    router.register_handler(SyscallOp::SYS_WORLD_EVENT,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_EVENT, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                uint32_t world_id = req.at("world_id").get<uint32_t>();
                json data = req.value("data", json::object());
                engine_->emit_event(world_id, KernelEventType::CUSTOM, data, msg.agent_id());
                response["success"] = true;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_EVENT, response.dump());
        });

    // SYS_WORLD_STATE — get or set world-scoped state
    router.register_handler(SyscallOp::SYS_WORLD_STATE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_STATE, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                uint32_t world_id = req.at("world_id").get<uint32_t>();
                std::string action = req.value("action", "get");
                std::string key = req.at("key").get<std::string>();

                if (action == "set") {
                    auto value = req.at("value");
                    bool ok = engine_->set_state(world_id, key, value, msg.agent_id());
                    response["success"] = ok;
                    if (!ok) response["error"] = "world not found";
                } else {
                    auto val = engine_->get_state(world_id, key, msg.agent_id());
                    response["success"] = true;
                    if (val) {
                        response["found"] = true;
                        response["value"] = *val;
                    } else {
                        response["found"] = false;
                    }
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_STATE, response.dump());
        });

    // SYS_WORLD_SNAPSHOT — serialize entire world to JSON
    router.register_handler(SyscallOp::SYS_WORLD_SNAPSHOT,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_SNAPSHOT, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                uint32_t world_id = req.at("world_id").get<uint32_t>();
                if (!engine_->exists(world_id)) {
                    response["success"] = false;
                    response["error"] = "world not found";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_SNAPSHOT, response.dump());
                }
                json snap = engine_->snapshot(world_id);
                response["success"] = true;
                response["snapshot"] = snap;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_SNAPSHOT, response.dump());
        });

    // SYS_WORLD_RESTORE — restore a world from snapshot
    router.register_handler(SyscallOp::SYS_WORLD_RESTORE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!engine_) {
                    response["success"] = false;
                    response["error"] = "worlds not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_RESTORE, response.dump());
                }
                auto req = json::parse(msg.payload_str());
                json snapshot = req.at("snapshot");
                uint32_t new_id = engine_->restore(snapshot);
                response["success"] = (new_id > 0);
                response["world_id"] = new_id;
                if (new_id == 0) response["error"] = "restore failed";
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WORLD_RESTORE, response.dump());
        });
}

} // namespace clove
