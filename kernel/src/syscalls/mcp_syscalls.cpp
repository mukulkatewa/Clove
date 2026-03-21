#include "mod.hpp"
#include <clove/mcp_bridge.hpp>
#include <clove/audit_log.hpp>

namespace clove {

void McpSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_MCP_LIST — list available MCP tools for this agent
    router.register_handler(SyscallOp::SYS_MCP_LIST,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "MCP not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_MCP_LIST, response.dump());
                }

                std::string agent_name;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    agent_name = req.value("agent_name", "");
                }

                auto tools = bridge_->list_tools(msg.agent_id(), agent_name);
                json tools_arr = json::array();
                for (const auto& t : tools) {
                    tools_arr.push_back({
                        {"server", t.server_name},
                        {"name", t.name},
                        {"description", t.description},
                        {"input_schema", t.input_schema}
                    });
                }
                response["success"] = true;
                response["tools"] = tools_arr;
                response["count"] = tools.size();

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MCP_LIST, response.dump());
        });

    // SYS_MCP_CALL — call an MCP tool
    router.register_handler(SyscallOp::SYS_MCP_CALL,
        [this](const Message& msg) -> Message {
            json response;
            try {
                if (!bridge_) {
                    response["success"] = false;
                    response["error"] = "MCP not enabled";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_MCP_CALL, response.dump());
                }

                auto req = json::parse(msg.payload_str());
                std::string server = req.at("server").get<std::string>();
                std::string tool = req.at("tool").get<std::string>();
                json arguments = req.value("arguments", json::object());

                ctx_.audit_logger.log(AuditCategory::SYSCALL, "MCP_CALL",
                    msg.agent_id(), "", {
                        {"server", server}, {"tool", tool},
                        {"arguments", arguments}
                    });

                auto result = bridge_->call_tool(msg.agent_id(), server, tool, arguments);

                response["success"] = result.success;
                response["content"] = result.content;
                response["duration_ms"] = result.duration_ms;
                if (!result.error.empty()) {
                    response["error"] = result.error;
                }

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MCP_CALL, response.dump());
        });
}

} // namespace clove
