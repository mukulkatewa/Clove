#include "mod.hpp"
#include <clove/audit_log.hpp>
#include <chrono>

namespace clove {

void OtelSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_OTEL_SPAN — record an OpenTelemetry span
    // For now, stores spans in-memory and audit log. Full OTel export (gRPC/HTTP
    // to collector) will be added when otel-cpp SDK is integrated.
    router.register_handler(SyscallOp::SYS_OTEL_SPAN,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string name = req.at("name").get<std::string>();
                std::string trace_id = req.value("trace_id", "");
                std::string parent_id = req.value("parent_id", "");
                uint64_t start_us = req.value("start_us", uint64_t(0));
                uint64_t end_us = req.value("end_us", uint64_t(0));
                json attributes = req.value("attributes", json::object());
                std::string status = req.value("status", "OK");

                // Generate span_id if not provided
                std::string span_id = req.value("span_id", "");
                if (span_id.empty()) {
                    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
                    span_id = std::to_string(now ^ msg.agent_id());
                }

                // Record to audit log for persistence
                ctx_.audit_logger.log(AuditCategory::SYSCALL, "OTEL_SPAN",
                    msg.agent_id(), "", {
                        {"span_name", name},
                        {"trace_id", trace_id},
                        {"span_id", span_id},
                        {"parent_id", parent_id},
                        {"start_us", start_us},
                        {"end_us", end_us},
                        {"duration_us", end_us > start_us ? end_us - start_us : 0},
                        {"attributes", attributes},
                        {"status", status}
                    });

                // Store in spans buffer for export
                {
                    std::lock_guard lock(spans_mutex_);
                    spans_.push_back({
                        {"name", name},
                        {"trace_id", trace_id},
                        {"span_id", span_id},
                        {"parent_id", parent_id},
                        {"start_us", start_us},
                        {"end_us", end_us},
                        {"agent_id", msg.agent_id()},
                        {"attributes", attributes},
                        {"status", status}
                    });
                    // Cap at 10K spans
                    if (spans_.size() > 10000) {
                        spans_.erase(spans_.begin());
                    }
                }

                response["success"] = true;
                response["span_id"] = span_id;

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_OTEL_SPAN, response.dump());
        });
}

} // namespace clove
