#include "mod.hpp"
#include <clove/inference_gateway.hpp>
#include <clove/llm_queue.hpp>
#include <clove/privacy_filter.hpp>
#include <clove/audit_log.hpp>
#include <clove/policy_recommender.hpp>

namespace clove {

void LlmSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_THINK — submit LLM inference request (async via LlmQueue)
    router.register_handler(SyscallOp::SYS_THINK,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string prompt = req.at("prompt").get<std::string>();
                std::string model = req.value("model", ctx_.config.llm_model);

                // Inference gateway checks
                if (ctx_.inference_gateway.is_enabled()) {
                    if (!ctx_.inference_gateway.is_model_allowed(model)) {
                        ctx_.policy_recommender.record_denial({
                            msg.agent_id(), "llm", model,
                            "model not in allowlist", 0
                        });
                        response["success"] = false;
                        response["error"] = "model not allowed: " + model;
                        return Message::create(msg.agent_id(), SyscallOp::SYS_THINK, response.dump());
                    }
                    if (!ctx_.inference_gateway.is_within_cost_limit()) {
                        response["success"] = false;
                        response["error"] = "cost limit exceeded";
                        return Message::create(msg.agent_id(), SyscallOp::SYS_THINK, response.dump());
                    }
                }

                // Submit to LLM queue (synchronous wait on future for now)
                auto future = ctx_.llm_queue.submit(msg.agent_id(), prompt);
                std::string result = future.get();
                response = json::parse(result);
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_THINK, response.dump());
        });

    // SYS_LLM_CONFIG — get/set inference gateway configuration
    router.register_handler(SyscallOp::SYS_LLM_CONFIG,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (msg.payload.empty()) {
                    // GET config
                    response["success"] = true;
                    response["config"] = ctx_.inference_gateway.to_json();
                } else {
                    // SET config
                    auto req = json::parse(msg.payload_str());
                    ctx_.inference_gateway.update_config(req);
                    response["success"] = true;
                    response["config"] = ctx_.inference_gateway.to_json();
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_LLM_CONFIG, response.dump());
        });

    // SYS_LLM_REPORT — get inference usage report
    router.register_handler(SyscallOp::SYS_LLM_REPORT,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto config = ctx_.inference_gateway.get_config();
                response["success"] = true;
                response["total_requests"] = ctx_.llm_queue.total_requests();
                response["total_completed"] = ctx_.llm_queue.total_completed();
                response["current_cost_usd"] = config.current_cost_usd;
                response["max_cost_usd"] = config.max_cost_usd;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_LLM_REPORT, response.dump());
        });

    // SYS_POLICY_RECOMMEND — get policy recommendations
    router.register_handler(SyscallOp::SYS_POLICY_RECOMMEND,
        [this](const Message& msg) -> Message {
            json response;
            try {
                size_t max = 20;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    max = req.value("max", size_t(20));
                }

                auto recs = ctx_.policy_recommender.get_recommendations(max);
                json recs_arr = json::array();
                for (const auto& r : recs) {
                    recs_arr.push_back({
                        {"category", r.category},
                        {"action", r.action},
                        {"resource", r.resource},
                        {"occurrence_count", r.occurrence_count},
                        {"suggested_change", r.suggested_change}
                    });
                }
                response["success"] = true;
                response["recommendations"] = recs_arr;
                response["denial_count"] = ctx_.policy_recommender.denial_count();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_POLICY_RECOMMEND, response.dump());
        });
}

} // namespace clove
