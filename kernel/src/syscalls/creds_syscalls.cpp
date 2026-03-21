#include "mod.hpp"
#include <clove/audit_log.hpp>
#include <clove/state_store.hpp>

namespace clove {

void CredsSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_CREDS_GET — retrieve a credential/secret
    // Credentials are stored in the state store under a "creds:" prefix with agent scope.
    // This provides a permission-gated interface to retrieve them.
    router.register_handler(SyscallOp::SYS_CREDS_GET,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string key = req.at("key").get<std::string>();

                // Look up in state store under creds namespace
                std::string store_key = "creds:" + key;
                auto val = ctx_.state_store.fetch(store_key, msg.agent_id());

                if (val) {
                    response["success"] = true;
                    response["key"] = key;
                    response["value"] = *val;

                    ctx_.audit_logger.log(AuditCategory::SECURITY, "CREDS_ACCESS",
                        msg.agent_id(), "", {{"key", key}});
                } else {
                    response["success"] = false;
                    response["error"] = "credential not found: " + key;
                }

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_CREDS_GET, response.dump());
        });
}

} // namespace clove
