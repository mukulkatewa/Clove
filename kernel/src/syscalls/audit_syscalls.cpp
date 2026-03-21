#include "mod.hpp"
#include <clove/audit_log.hpp>

namespace clove {

void AuditSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_GET_AUDIT_LOG — query audit log entries
    router.register_handler(SyscallOp::SYS_GET_AUDIT_LOG,
        [this](const Message& msg) -> Message {
            json response;
            try {
                AuditCategory* cat_filter = nullptr;
                uint32_t* agent_filter = nullptr;
                uint64_t since_id = 0;
                size_t limit = 100;

                AuditCategory cat_val;
                uint32_t agent_val;

                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    if (req.contains("category")) {
                        cat_val = audit_category_from_string(req["category"].get<std::string>());
                        cat_filter = &cat_val;
                    }
                    if (req.contains("agent_id")) {
                        agent_val = req["agent_id"].get<uint32_t>();
                        agent_filter = &agent_val;
                    }
                    since_id = req.value("since_id", uint64_t(0));
                    limit = req.value("limit", size_t(100));
                }

                auto entries = ctx_.audit_logger.get_entries(cat_filter, agent_filter, since_id, limit);
                json entries_arr = json::array();
                for (const auto& e : entries) {
                    entries_arr.push_back(e.to_json());
                }
                response["success"] = true;
                response["entries"] = entries_arr;
                response["count"] = entries.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_GET_AUDIT_LOG, response.dump());
        });

    // SYS_SET_AUDIT_CONFIG — enable/disable audit categories, set max entries
    router.register_handler(SyscallOp::SYS_SET_AUDIT_CONFIG,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                AuditConfig config;
                config.max_entries    = req.value("max_entries", size_t(10000));
                config.log_syscalls   = req.value("log_syscalls", false);
                config.log_security   = req.value("log_security", true);
                config.log_lifecycle  = req.value("log_lifecycle", true);
                config.log_ipc        = req.value("log_ipc", false);
                config.log_state      = req.value("log_state", false);
                config.log_resource   = req.value("log_resource", true);
                config.log_network    = req.value("log_network", false);
                config.log_world      = req.value("log_world", false);
                ctx_.audit_logger.set_config(config);
                response["success"] = true;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_SET_AUDIT_CONFIG, response.dump());
        });
}

} // namespace clove
