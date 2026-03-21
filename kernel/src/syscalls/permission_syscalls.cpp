#include "mod.hpp"
#include <clove/permissions_store.hpp>
#include <clove/agent_manager.hpp>
#include <clove/inference_gateway.hpp>
#include <clove/privacy_filter.hpp>
#include <clove/audit_log.hpp>

namespace clove {

void PermissionSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_GET_PERMS — get permissions for an agent
    router.register_handler(SyscallOp::SYS_GET_PERMS,
        [this](const Message& msg) -> Message {
            json response;
            try {
                uint32_t target_id = msg.agent_id();
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    target_id = req.value("agent_id", msg.agent_id());
                }

                auto& perms = ctx_.permissions_store.get_or_create(target_id);
                response["success"] = true;
                response["agent_id"] = target_id;
                response["permissions"] = perms.to_json();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_GET_PERMS, response.dump());
        });

    // SYS_SET_PERMS — set permissions for an agent
    router.register_handler(SyscallOp::SYS_SET_PERMS,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                uint32_t target_id = req.value("agent_id", msg.agent_id());

                if (req.contains("level")) {
                    std::string level_str = req["level"].get<std::string>();
                    PermissionLevel level = PermissionLevel::STANDARD;
                    if (level_str == "unrestricted") level = PermissionLevel::UNRESTRICTED;
                    else if (level_str == "sandboxed") level = PermissionLevel::SANDBOXED;
                    else if (level_str == "readonly") level = PermissionLevel::READONLY;
                    else if (level_str == "minimal") level = PermissionLevel::MINIMAL;
                    ctx_.permissions_store.set_level(target_id, level);
                } else if (req.contains("permissions")) {
                    auto perms = AgentPermissions::from_json(req["permissions"]);
                    ctx_.permissions_store.set_permissions(target_id, perms);
                }

                response["success"] = true;
                response["agent_id"] = target_id;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_SET_PERMS, response.dump());
        });

    // SYS_AUTH — check if agent exists and return its permissions
    router.register_handler(SyscallOp::SYS_AUTH,
        [this](const Message& msg) -> Message {
            json response;
            try {
                uint32_t target_id = msg.agent_id();
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    target_id = req.value("agent_id", msg.agent_id());
                }

                auto agent = ctx_.agent_manager.get_agent(target_id);
                if (!agent) {
                    response["success"] = false;
                    response["error"] = "agent not found";
                    response["authenticated"] = false;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_AUTH, response.dump());
                }

                auto& perms = ctx_.permissions_store.get_or_create(target_id);
                response["success"] = true;
                response["authenticated"] = true;
                response["agent_id"] = target_id;
                response["agent_name"] = agent->name();
                response["permissions"] = perms.to_json();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_AUTH, response.dump());
        });

    // SYS_POLICY_UPDATE — hot-update LLM/privacy/agent policy via JSON payload
    router.register_handler(SyscallOp::SYS_POLICY_UPDATE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto policy = json::parse(msg.payload_str());
                int changes = 0;

                if (policy.contains("llm")) {
                    ctx_.inference_gateway.update_config(policy["llm"]);
                    changes++;
                }

                if (policy.contains("privacy")) {
                    auto& pconf = policy["privacy"];
                    PrivacyFilterConfig pf_config;
                    pf_config.enabled = pconf.value("enabled", true);
                    if (pconf.contains("mode")) {
                        pf_config.mode = PrivacyFilter::mode_from_string(
                            pconf["mode"].get<std::string>());
                    }
                    if (pconf.contains("patterns")) {
                        pf_config.enabled_patterns.clear();
                        for (const auto& p : pconf["patterns"]) {
                            pf_config.enabled_patterns.push_back(p.get<std::string>());
                        }
                    }
                    ctx_.privacy_filter.configure(pf_config);
                    changes++;
                }

                if (policy.contains("agents") && policy["agents"].is_object()) {
                    for (auto& [name, agent_policy] : policy["agents"].items()) {
                        auto agent = ctx_.agent_manager.get_agent(name);
                        if (!agent) continue;
                        if (agent_policy.contains("permissions")) {
                            auto current = ctx_.permissions_store.get_or_create(agent->id());
                            auto delta = agent_policy["permissions"];
                            if (delta.contains("can_exec")) current.can_exec = delta["can_exec"].get<bool>();
                            if (delta.contains("can_read")) current.can_read = delta["can_read"].get<bool>();
                            if (delta.contains("can_write")) current.can_write = delta["can_write"].get<bool>();
                            if (delta.contains("can_think")) current.can_think = delta["can_think"].get<bool>();
                            if (delta.contains("can_http")) current.can_http = delta["can_http"].get<bool>();
                            if (delta.contains("allowed_domains")) {
                                current.allowed_domains.clear();
                                for (const auto& d : delta["allowed_domains"])
                                    current.allowed_domains.push_back(d.get<std::string>());
                            }
                            ctx_.permissions_store.set_permissions(agent->id(), current);
                            changes++;
                        }
                    }
                }

                ctx_.audit_logger.log(AuditCategory::SECURITY, "POLICY_UPDATE",
                    msg.agent_id(), "", {{"source", "syscall"}, {"changes", changes}});

                response["success"] = true;
                response["changes_applied"] = changes;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_POLICY_UPDATE, response.dump());
        });
}

} // namespace clove
