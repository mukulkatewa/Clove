#include "mod.hpp"
#include <clove/permissions_store.hpp>
#include <clove/agent_scheduler.hpp>
#include <clove/audit_log.hpp>
#include <chrono>

namespace clove {

static uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

void BudgetSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_SET_BUDGET — set budget limits for an agent
    router.register_handler(SyscallOp::SYS_SET_BUDGET,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());

                // Target agent (default: calling agent)
                uint32_t target_id = req.value("agent_id", msg.agent_id());

                auto& budget = ctx_.permissions_store.get_or_create_budget(target_id);

                // Update limits (only fields that are present)
                if (req.contains("max_tokens"))
                    budget.max_tokens = req["max_tokens"].get<uint64_t>();
                if (req.contains("max_steps"))
                    budget.max_steps = req["max_steps"].get<uint32_t>();
                if (req.contains("max_time_ms"))
                    budget.max_time_ms = req["max_time_ms"].get<uint64_t>();
                if (req.contains("max_cost_usd"))
                    budget.max_cost_usd = req["max_cost_usd"].get<double>();
                if (req.contains("kill_on_exceeded"))
                    budget.kill_on_exceeded = req["kill_on_exceeded"].get<bool>();

                // Start timer if time budget is set
                if (budget.max_time_ms > 0 && budget.started_at_ms == 0) {
                    budget.start_timer(now_ms());
                }

                // If "reset" flag is set, reset usage counters
                if (req.value("reset", false)) {
                    budget.reset();
                    if (budget.max_time_ms > 0) {
                        budget.start_timer(now_ms());
                    }
                }

                ctx_.audit_logger.log(AuditCategory::RESOURCE, "BUDGET_SET",
                    target_id, "", budget.to_json());

                response["success"] = true;
                response["budget"] = budget.to_json();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_SET_BUDGET, response.dump());
        });

    // SYS_GET_BUDGET — get budget status for an agent
    router.register_handler(SyscallOp::SYS_GET_BUDGET,
        [this](const Message& msg) -> Message {
            json response;
            try {
                uint32_t target_id = msg.agent_id();
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    target_id = req.value("agent_id", msg.agent_id());
                }

                auto& budget = ctx_.permissions_store.get_or_create_budget(target_id);

                uint64_t now = now_ms();
                response["success"] = true;
                response["agent_id"] = target_id;
                response["budget"] = budget.to_json();
                response["exceeded"] = budget.any_exceeded(now);

                std::string exceeded_type = budget.exceeded_type(now);
                if (!exceeded_type.empty()) {
                    response["exceeded_type"] = exceeded_type;
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_GET_BUDGET, response.dump());
        });

    // SYS_SET_PRIORITY — set scheduling priority for an agent
    router.register_handler(SyscallOp::SYS_SET_PRIORITY,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                uint32_t target_id = req.value("agent_id", msg.agent_id());
                std::string priority_str = req.value("priority", "normal");

                if (!scheduler_) {
                    response["success"] = false;
                    response["error"] = "scheduler not available";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_SET_PRIORITY, response.dump());
                }

                AgentPriority priority = agent_priority_from_string(priority_str);
                scheduler_->set_priority(target_id, priority);

                ctx_.audit_logger.log(AuditCategory::RESOURCE, "PRIORITY_SET",
                    target_id, "", {{"priority", priority_str}});

                response["success"] = true;
                response["agent_id"] = target_id;
                response["priority"] = agent_priority_to_string(priority);
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_SET_PRIORITY, response.dump());
        });

    // SYS_GET_PRIORITY — get scheduling priority and state for an agent
    router.register_handler(SyscallOp::SYS_GET_PRIORITY,
        [this](const Message& msg) -> Message {
            json response;
            try {
                uint32_t target_id = msg.agent_id();
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    target_id = req.value("agent_id", msg.agent_id());
                }

                if (!scheduler_) {
                    response["success"] = false;
                    response["error"] = "scheduler not available";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_GET_PRIORITY, response.dump());
                }

                auto entry = scheduler_->get_entry(target_id);
                response["success"] = true;
                response["agent_id"] = target_id;

                if (entry) {
                    response["priority"] = agent_priority_to_string(entry->priority);
                    response["schedule_state"] = schedule_state_to_string(entry->state);
                    response["llm_calls"] = entry->llm_calls;
                    response["tool_calls"] = entry->tool_calls;
                    response["total_llm_wait_ms"] = entry->total_llm_wait_ms;
                    response["total_tool_wait_ms"] = entry->total_tool_wait_ms;
                } else {
                    response["priority"] = "normal";
                    response["schedule_state"] = "idle";
                }

                // Include scheduler stats
                response["scheduler"] = scheduler_->stats_json();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_GET_PRIORITY, response.dump());
        });
}

} // namespace clove
