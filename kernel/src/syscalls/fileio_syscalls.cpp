#include "mod.hpp"
#include <clove/permissions_store.hpp>
#include <clove/audit_log.hpp>
#include <clove/policy_recommender.hpp>
#include <fstream>
#include <array>
#include <cstdio>
#include <sys/wait.h>

namespace clove {

void FileIoSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_READ — permission-gated file read
    router.register_handler(SyscallOp::SYS_READ,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string path = req.at("path").get<std::string>();
                size_t max_bytes = req.value("max_bytes", size_t(1024 * 1024)); // 1MB default

                auto& perms = ctx_.permissions_store.get_or_create(msg.agent_id());
                if (!perms.can_read) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "read", path, "read not allowed", 0
                    });
                    response["success"] = false;
                    response["error"] = "read permission denied";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_READ, response.dump());
                }

                if (!perms.can_read_path(path)) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "read", path, "path not in allowed list", 0
                    });
                    ctx_.audit_logger.log(AuditCategory::SECURITY, "READ_DENIED",
                        msg.agent_id(), "", {{"path", path}}, false);
                    response["success"] = false;
                    response["error"] = "path not allowed: " + path;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_READ, response.dump());
                }

                std::ifstream file(path, std::ios::binary);
                if (!file.is_open()) {
                    response["success"] = false;
                    response["error"] = "file not found: " + path;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_READ, response.dump());
                }

                // Read up to max_bytes
                std::string content(max_bytes, '\0');
                file.read(content.data(), static_cast<std::streamsize>(max_bytes));
                content.resize(static_cast<size_t>(file.gcount()));

                response["success"] = true;
                response["path"] = path;
                response["content"] = content;
                response["bytes"] = content.size();
                response["truncated"] = file.peek() != EOF;

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_READ, response.dump());
        });

    // SYS_WRITE — permission-gated file write
    router.register_handler(SyscallOp::SYS_WRITE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string path = req.at("path").get<std::string>();
                std::string content = req.at("content").get<std::string>();
                bool append = req.value("append", false);

                auto& perms = ctx_.permissions_store.get_or_create(msg.agent_id());
                if (!perms.can_write) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "write", path, "write not allowed", 0
                    });
                    response["success"] = false;
                    response["error"] = "write permission denied";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WRITE, response.dump());
                }

                if (!perms.can_write_path(path)) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "write", path, "path not in allowed list", 0
                    });
                    ctx_.audit_logger.log(AuditCategory::SECURITY, "WRITE_DENIED",
                        msg.agent_id(), "", {{"path", path}}, false);
                    response["success"] = false;
                    response["error"] = "path not allowed: " + path;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WRITE, response.dump());
                }

                auto mode = append ? (std::ios::binary | std::ios::app)
                                   : (std::ios::binary | std::ios::trunc);
                std::ofstream file(path, mode);
                if (!file.is_open()) {
                    response["success"] = false;
                    response["error"] = "cannot open for writing: " + path;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_WRITE, response.dump());
                }

                file.write(content.data(), static_cast<std::streamsize>(content.size()));
                file.close();

                ctx_.audit_logger.log(AuditCategory::SYSCALL, "FILE_WRITE",
                    msg.agent_id(), "", {{"path", path}, {"bytes", content.size()}, {"append", append}});

                response["success"] = true;
                response["path"] = path;
                response["bytes_written"] = content.size();

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_WRITE, response.dump());
        });

    // SYS_EXEC — permission-gated command execution
    router.register_handler(SyscallOp::SYS_EXEC,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string command = req.at("command").get<std::string>();
                int timeout_ms = req.value("timeout_ms", 30000);

                auto& perms = ctx_.permissions_store.get_or_create(msg.agent_id());
                if (!perms.can_exec) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "exec", command, "exec not allowed", 0
                    });
                    response["success"] = false;
                    response["error"] = "exec permission denied";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_EXEC, response.dump());
                }

                if (!perms.can_execute_command(command)) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "exec", command, "command not in allowed list", 0
                    });
                    ctx_.audit_logger.log(AuditCategory::SECURITY, "EXEC_DENIED",
                        msg.agent_id(), "", {{"command", command}}, false);
                    response["success"] = false;
                    response["error"] = "command not allowed";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_EXEC, response.dump());
                }

                ctx_.audit_logger.log(AuditCategory::SYSCALL, "EXEC_START",
                    msg.agent_id(), "", {{"command", command}});

                // Execute with popen — capture stdout
                std::string output;
                std::string cmd_with_redirect = command + " 2>&1";
                FILE* pipe = popen(cmd_with_redirect.c_str(), "r");
                if (!pipe) {
                    response["success"] = false;
                    response["error"] = "failed to execute command";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_EXEC, response.dump());
                }

                std::array<char, 4096> buf;
                while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
                    output += buf.data();
                    // Cap output at 1MB
                    if (output.size() > 1024 * 1024) break;
                }
                int exit_code = pclose(pipe);
                exit_code = WEXITSTATUS(exit_code);

                response["success"] = (exit_code == 0);
                response["exit_code"] = exit_code;
                response["output"] = output;
                response["truncated"] = output.size() > 1024 * 1024;

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_EXEC, response.dump());
        });
}

} // namespace clove
