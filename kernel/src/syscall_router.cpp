#include "syscall_router.hpp"
#include <clove/permissions_store.hpp>
#include <clove/event_bus.hpp>
#include <clove/audit_log.hpp>

namespace clove {

Message SyscallRouter::handle(const Message& msg) const {
    // Budget enforcement: step + time check before every syscall
    if (budget_store_) {
        auto& budget = budget_store_->get_or_create_budget(msg.agent_id());

        // Start timer on first syscall if time budget is set
        if (budget.max_time_ms > 0) {
            budget.start_timer(now_ms());
        }

        // Record this step
        budget.record_step();

        // Check step and time budgets
        uint64_t now = now_ms();
        if (budget.any_exceeded(now)) {
            std::string exceeded = budget.exceeded_type(now);

            // Emit event
            if (event_bus_) {
                event_bus_->emit(KernelEventType::BUDGET_EXCEEDED, {
                    {"agent_id", msg.agent_id()},
                    {"budget_type", exceeded},
                    {"budget", budget.to_json()}
                }, msg.agent_id());
            }

            // Audit log
            if (audit_) {
                audit_->log(AuditCategory::RESOURCE, "BUDGET_EXCEEDED",
                    msg.agent_id(), "", {
                        {"budget_type", exceeded},
                        {"steps_taken", budget.steps_taken},
                        {"tokens_used", budget.tokens_used},
                        {"cost_usd", budget.cost_usd}
                    });
            }

            // Return error
            nlohmann::json response;
            response["success"] = false;
            response["error"] = exceeded + " budget exceeded";
            response["budget"] = budget.to_json();
            return Message::create(msg.agent_id(), msg.opcode(), response.dump());
        }
    }

    // Dispatch to handler
    auto it = handlers_.find(static_cast<uint8_t>(msg.header.opcode));
    if (it != handlers_.end()) {
        return it->second(msg);
    }

    // Unknown opcode
    nlohmann::json response;
    response["success"] = false;
    response["error"] = "Unknown syscall opcode";
    return Message::create(msg.header.agent_id, msg.opcode(), response.dump());
}

} // namespace clove
