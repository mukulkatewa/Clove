#include "mod.hpp"
#include <clove/privacy_filter.hpp>

namespace clove {

void PiiSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_PII_SCAN — scan text for PII
    router.register_handler(SyscallOp::SYS_PII_SCAN,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string text = req.at("text").get<std::string>();

                auto matches = ctx_.privacy_filter.scan(text);
                json matches_arr = json::array();
                for (const auto& m : matches) {
                    matches_arr.push_back({
                        {"type", m.type_name},
                        {"matched_text", m.matched_text},
                        {"start_pos", m.start_pos},
                        {"end_pos", m.end_pos}
                    });
                }
                response["success"] = true;
                response["matches"] = matches_arr;
                response["count"] = matches.size();
                response["has_pii"] = !matches.empty();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_PII_SCAN, response.dump());
        });

    // SYS_PII_REDACT — redact PII from text
    router.register_handler(SyscallOp::SYS_PII_REDACT,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string text = req.at("text").get<std::string>();

                auto result = ctx_.privacy_filter.redact(text);
                json matches_arr = json::array();
                for (const auto& m : result.matches) {
                    matches_arr.push_back({
                        {"type", m.type_name},
                        {"matched_text", m.matched_text},
                        {"start_pos", m.start_pos},
                        {"end_pos", m.end_pos}
                    });
                }
                response["success"] = true;
                response["cleaned_text"] = result.cleaned_text;
                response["matches"] = matches_arr;
                response["redacted_count"] = result.matches.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_PII_REDACT, response.dump());
        });
}

} // namespace clove
