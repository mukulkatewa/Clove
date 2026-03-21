#include "mod.hpp"
#include <clove/async_tasks.hpp>

namespace clove {

void AsyncSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_ASYNC_POLL — poll completed async task results
    router.register_handler(SyscallOp::SYS_ASYNC_POLL,
        [this](const Message& msg) -> Message {
            json response;
            try {
                size_t max_results = 10;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    max_results = req.value("max_results", size_t(10));
                }

                auto results = ctx_.async_tasks.poll(msg.agent_id(), max_results);
                json results_arr = json::array();
                for (const auto& r : results) {
                    results_arr.push_back({
                        {"request_id", r.request_id},
                        {"opcode", static_cast<uint8_t>(r.opcode)},
                        {"payload", r.payload}
                    });
                }
                response["success"] = true;
                response["results"] = results_arr;
                response["count"] = results.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_ASYNC_POLL, response.dump());
        });
}

} // namespace clove
