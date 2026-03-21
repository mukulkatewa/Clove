#include "mod.hpp"
#include <clove/event_bus.hpp>

namespace clove {

void EventSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_SUBSCRIBE — subscribe to event types
    router.register_handler(SyscallOp::SYS_SUBSCRIBE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());

                if (req.contains("event_type")) {
                    auto type = static_cast<KernelEventType>(req["event_type"].get<uint32_t>());
                    ctx_.event_bus.subscribe(msg.agent_id(), type);
                } else if (req.value("all", false)) {
                    ctx_.event_bus.subscribe_all(msg.agent_id());
                }
                response["success"] = true;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_SUBSCRIBE, response.dump());
        });

    // SYS_UNSUBSCRIBE — unsubscribe from event types
    router.register_handler(SyscallOp::SYS_UNSUBSCRIBE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());

                if (req.contains("event_type")) {
                    auto type = static_cast<KernelEventType>(req["event_type"].get<uint32_t>());
                    ctx_.event_bus.unsubscribe(msg.agent_id(), type);
                } else if (req.value("all", false)) {
                    ctx_.event_bus.unsubscribe_all(msg.agent_id());
                }
                response["success"] = true;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_UNSUBSCRIBE, response.dump());
        });

    // SYS_POLL_EVENTS — poll for pending events
    router.register_handler(SyscallOp::SYS_POLL_EVENTS,
        [this](const Message& msg) -> Message {
            json response;
            try {
                size_t max_events = 50;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    max_events = req.value("max_events", 50);
                }

                auto events = ctx_.event_bus.poll(msg.agent_id(), max_events);
                json ev_array = json::array();
                for (const auto& e : events) {
                    ev_array.push_back({
                        {"type", static_cast<uint32_t>(e.type)},
                        {"data", e.data},
                        {"source_agent_id", e.source_agent_id},
                        {"timestamp_ms", e.timestamp_ms}
                    });
                }
                response["success"] = true;
                response["events"] = ev_array;
                response["count"] = events.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_POLL_EVENTS, response.dump());
        });

    // SYS_EMIT — emit a custom event
    router.register_handler(SyscallOp::SYS_EMIT,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                json data = req.value("data", json::object());

                auto type = KernelEventType::CUSTOM;
                if (req.contains("event_type")) {
                    type = static_cast<KernelEventType>(req["event_type"].get<uint32_t>());
                }

                ctx_.event_bus.emit(type, data, msg.agent_id());
                response["success"] = true;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_EMIT, response.dump());
        });
}

} // namespace clove
