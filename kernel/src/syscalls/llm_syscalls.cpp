#include "mod.hpp"
#include <clove/inference_gateway.hpp>
#include <clove/llm_queue.hpp>
#include <clove/privacy_filter.hpp>
#include <clove/audit_log.hpp>
#include <clove/policy_recommender.hpp>
#include <clove/context_assembler.hpp>
#include <clove/permissions_store.hpp>
#include <clove/event_bus.hpp>
#include <clove/agent_scheduler.hpp>

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

                // Context-aware mode: prepend assembled context from chain
                std::string chain_id = req.value("chain_id", "");
                bool auto_context = req.value("auto_context", false);
                if (auto_context && !chain_id.empty() && assembler_) {
                    AssemblyConfig ac;
                    ac.max_tokens = req.value("context_max_tokens", size_t(128000));
                    auto assembled = assembler_->assemble(
                        msg.agent_id(), chain_id, ac);
                    if (!assembled.context.empty()) {
                        prompt = assembled.context + "\n\n---\n\n" + prompt;
                    }
                }

                // Model allowlist check — always enforced if allowlist is non-empty,
                // regardless of whether the full inference gateway is enabled.
                if (!ctx_.inference_gateway.is_model_allowed(model)) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "llm", model,
                        "model not in allowlist", 0
                    });
                    response["success"] = false;
                    response["error"] = "model not allowed: " + model;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_THINK, response.dump());
                }

                // System-wide cost limit (only if gateway enabled)
                if (ctx_.inference_gateway.is_enabled()) {
                    if (!ctx_.inference_gateway.is_within_cost_limit()) {
                        response["success"] = false;
                        response["error"] = "system cost limit exceeded";
                        return Message::create(msg.agent_id(), SyscallOp::SYS_THINK, response.dump());
                    }
                }

                // PII filtering — applied before ANY LLM backend, not just OpenRouter.
                // Runs in the syscall handler so it works regardless of backend.
                std::string filtered_prompt = prompt;
                size_t pii_redacted = 0;
                if (ctx_.privacy_filter.is_enabled()) {
                    auto pii_result = ctx_.privacy_filter.redact(prompt);
                    if (ctx_.privacy_filter.mode() == PrivacyMode::BLOCK &&
                        !pii_result.matches.empty()) {
                        response["success"] = false;
                        response["error"] = "PII detected in prompt, blocked by privacy policy";
                        nlohmann::json pii_types = nlohmann::json::array();
                        for (const auto& m : pii_result.matches) {
                            pii_types.push_back(m.type_name);
                        }
                        response["pii_types"] = pii_types;
                        return Message::create(msg.agent_id(), SyscallOp::SYS_THINK, response.dump());
                    }
                    filtered_prompt = pii_result.cleaned_text;
                    pii_redacted = pii_result.matches.size();
                }

                // Mark agent as waiting for LLM
                if (scheduler_) scheduler_->mark_waiting_llm(msg.agent_id());

                // Submit to LLM queue (synchronous wait on future for now)
                auto future = ctx_.llm_queue.submit(msg.agent_id(), filtered_prompt);
                std::string result = future.get();
                response = json::parse(result);

                // Mark agent as ready again
                if (scheduler_) scheduler_->mark_ready(msg.agent_id());

                // Attach PII redaction info to response
                if (pii_redacted > 0) {
                    response["pii_redacted"] = pii_redacted;
                }

                // Track token and cost at PER-AGENT level via AgentBudget.
                // System-wide cost is tracked separately in InferenceGateway
                // (inside the OpenRouter backend lambda in kernel.cpp).
                if (response.value("success", false)) {
                    uint64_t tokens = response.value("tokens", uint64_t(0));
                    double cost = response.value("cost_usd", 0.0);
                    auto& budget = ctx_.permissions_store.get_or_create_budget(msg.agent_id());
                    if (tokens > 0) budget.record_tokens(tokens);
                    if (cost > 0.0) budget.record_cost(cost);

                    // Check post-call budget limits
                    auto now = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    if (budget.any_exceeded(now)) {
                        std::string exceeded = budget.exceeded_type(now);
                        ctx_.event_bus.emit(KernelEventType::BUDGET_EXCEEDED, {
                            {"agent_id", msg.agent_id()},
                            {"budget_type", exceeded},
                            {"budget", budget.to_json()}
                        }, msg.agent_id());
                        ctx_.audit_logger.log(AuditCategory::RESOURCE, "BUDGET_EXCEEDED",
                            msg.agent_id(), "", {
                                {"budget_type", exceeded},
                                {"tokens_used", budget.tokens_used},
                                {"cost_usd", budget.cost_usd}
                            });
                    }
                }
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
