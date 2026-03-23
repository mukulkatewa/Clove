#include "mod.hpp"
#include <clove/state_store.hpp>
#include <clove/state_store_db.hpp>

namespace clove {

void StateSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_STORE — store a key-value pair
    router.register_handler(SyscallOp::SYS_STORE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string key = req.at("key").get<std::string>();
                json value = req.value("value", json{});
                std::string scope = req.value("scope", "global");
                uint64_t ttl_ms = req.value("ttl_ms", 0);

                bool ok = ctx_.state_store.store(key, value, msg.agent_id(), scope, ttl_ms);
                response["success"] = ok;
                if (ok) {
                    response["key"] = key;
                    // Write-through to SQLite (if persistence enabled)
                    if (db_) db_->store(key, value, msg.agent_id(), scope);
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_STORE, response.dump());
        });

    // SYS_FETCH — fetch a value by key
    router.register_handler(SyscallOp::SYS_FETCH,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string key = req.at("key").get<std::string>();

                auto val = ctx_.state_store.fetch(key, msg.agent_id());
                if (val) {
                    response["success"] = true;
                    response["key"] = key;
                    response["value"] = *val;
                } else {
                    response["success"] = false;
                    response["error"] = "key not found";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_FETCH, response.dump());
        });

    // SYS_DELETE — delete a key
    router.register_handler(SyscallOp::SYS_DELETE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string key = req.at("key").get<std::string>();

                bool ok = ctx_.state_store.erase(key, msg.agent_id());
                response["success"] = ok;
                if (ok && db_) db_->erase(key);
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_DELETE, response.dump());
        });

    // SYS_KEYS — list keys with optional prefix filter
    router.register_handler(SyscallOp::SYS_KEYS,
        [this](const Message& msg) -> Message {
            json response;
            try {
                std::string prefix;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    prefix = req.value("prefix", "");
                }

                auto keys = ctx_.state_store.keys(prefix, msg.agent_id());
                response["success"] = true;
                response["keys"] = keys;
                response["count"] = keys.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_KEYS, response.dump());
        });
}

} // namespace clove
