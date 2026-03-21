#include <clove/mcp_bridge.hpp>

#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <chrono>

namespace clove {

McpBridge::McpBridge() = default;

McpBridge::~McpBridge() {
    stop_all();
}

// ── Server management ──────────────────────────────────────────

bool McpBridge::add_server(const McpServerConfig& config) {
    std::lock_guard lock(mutex_);
    if (sessions_.count(config.name)) return false;

    McpSession session;
    session.config = config;
    sessions_[config.name] = std::move(session);
    return true;
}

bool McpBridge::remove_server(const std::string& name) {
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(name);
    if (it == sessions_.end()) return false;

    kill_process(it->second);
    sessions_.erase(it);
    return true;
}

bool McpBridge::start_server(const std::string& name) {
    std::lock_guard lock(mutex_);
    auto it = sessions_.find(name);
    if (it == sessions_.end()) return false;

    auto& session = it->second;
    if (session.connected) return true;

    if (!spawn_process(session)) return false;
    if (!initialize_session(session)) {
        kill_process(session);
        return false;
    }
    if (!discover_tools(session)) {
        kill_process(session);
        return false;
    }

    session.connected = true;
    return true;
}

void McpBridge::start_all() {
    // Collect names first to avoid holding lock during start
    std::vector<std::string> names;
    {
        std::lock_guard lock(mutex_);
        for (auto& [name, _] : sessions_) names.push_back(name);
    }
    for (const auto& name : names) {
        start_server(name);
    }
}

void McpBridge::stop_all() {
    std::lock_guard lock(mutex_);
    for (auto& [_, session] : sessions_) {
        kill_process(session);
    }
}

bool McpBridge::has_server(const std::string& name) const {
    std::lock_guard lock(mutex_);
    return sessions_.count(name) > 0;
}

// ── Tool discovery ─────────────────────────────────────────────

std::vector<McpTool> McpBridge::list_tools(uint32_t /*agent_id*/,
                                            const std::string& agent_name) const {
    std::lock_guard lock(mutex_);
    std::vector<McpTool> result;

    for (const auto& [_, session] : sessions_) {
        if (!session.connected) continue;
        if (!agent_allowed(session, agent_name)) continue;

        for (const auto& tool : session.tools) {
            result.push_back(tool);
        }
    }
    return result;
}

// ── Tool invocation ────────────────────────────────────────────

McpCallResult McpBridge::call_tool(uint32_t /*agent_id*/,
                                    const std::string& server_name,
                                    const std::string& tool_name,
                                    const nlohmann::json& arguments) {
    std::lock_guard lock(mutex_);

    auto it = sessions_.find(server_name);
    if (it == sessions_.end()) {
        return {false, {}, "server not found: " + server_name};
    }

    auto& session = it->second;
    if (!session.connected) {
        return {false, {}, "server not connected: " + server_name};
    }

    auto t0 = std::chrono::steady_clock::now();

    nlohmann::json params;
    params["name"] = tool_name;
    params["arguments"] = arguments;

    auto response = send_request(session, "tools/call", params);
    session.total_calls++;

    auto t1 = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double, std::milli>(t1 - t0).count();

    McpCallResult result;
    result.duration_ms = duration;

    if (response.contains("error")) {
        result.success = false;
        if (response["error"].is_object()) {
            result.error = response["error"].value("message", "unknown error");
        } else {
            result.error = response["error"].dump();
        }
        return result;
    }

    if (response.contains("result")) {
        result.success = true;
        result.content = response["result"].value("content", nlohmann::json::array());
    } else {
        result.success = false;
        result.error = "unexpected response format";
    }

    return result;
}

// ── Status ─────────────────────────────────────────────────────

std::vector<McpServerStatus> McpBridge::status() const {
    std::lock_guard lock(mutex_);
    std::vector<McpServerStatus> result;

    for (const auto& [name, session] : sessions_) {
        McpServerStatus s;
        s.name = name;
        s.connected = session.connected;
        s.pid = session.pid;
        s.tool_count = session.tools.size();
        s.total_calls = session.total_calls;
        result.push_back(s);
    }
    return result;
}

// ── Process management ─────────────────────────────────────────

bool McpBridge::spawn_process(McpSession& session) {
    if (session.config.transport != "stdio") {
        return false; // SSE not implemented yet
    }

    int stdin_pipe[2];   // Parent writes → child reads
    int stdout_pipe[2];  // Child writes → parent reads

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return false;
    }

    if (pid == 0) {
        // Child: set up stdio redirection
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);

        // Build argv
        std::vector<const char*> argv;
        argv.push_back(session.config.command.c_str());
        for (const auto& arg : session.config.args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127); // exec failed
    }

    // Parent
    close(stdin_pipe[0]);   // Close read end of stdin pipe
    close(stdout_pipe[1]);  // Close write end of stdout pipe

    session.pid = pid;
    session.stdin_fd = stdin_pipe[1];
    session.stdout_fd = stdout_pipe[0];

    // Set stdout to non-blocking for read timeouts
    fcntl(session.stdout_fd, F_SETFL,
          fcntl(session.stdout_fd, F_GETFL) | O_NONBLOCK);

    return true;
}

void McpBridge::kill_process(McpSession& session) {
    if (session.stdin_fd >= 0) { close(session.stdin_fd); session.stdin_fd = -1; }
    if (session.stdout_fd >= 0) { close(session.stdout_fd); session.stdout_fd = -1; }

    if (session.pid > 0) {
        kill(session.pid, SIGTERM);
        int status;
        pid_t result = waitpid(session.pid, &status, WNOHANG);
        if (result == 0) {
            // Still running, wait briefly then force kill
            usleep(100000); // 100ms
            kill(session.pid, SIGKILL);
            waitpid(session.pid, &status, 0);
        }
        session.pid = -1;
    }

    session.connected = false;
    session.tools.clear();
    session.read_buf.clear();
}

// ── JSON-RPC ───────────────────────────────────────────────────

bool McpBridge::write_json(McpSession& session, const nlohmann::json& msg) {
    std::string line = msg.dump() + "\n";
    ssize_t written = write(session.stdin_fd, line.data(), line.size());
    return written == static_cast<ssize_t>(line.size());
}

nlohmann::json McpBridge::send_request(McpSession& session,
                                        const std::string& method,
                                        const nlohmann::json& params) {
    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["method"] = method;
    req["id"] = session.next_id++;
    if (!params.is_null() && !params.empty()) {
        req["params"] = params;
    }

    if (!write_json(session, req)) {
        return {{"error", {{"message", "failed to write to MCP server"}}}};
    }

    return read_response(session);
}

void McpBridge::send_notification(McpSession& session,
                                   const std::string& method,
                                   const nlohmann::json& params) {
    nlohmann::json notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = method;
    if (!params.is_null() && !params.empty()) {
        notif["params"] = params;
    }
    write_json(session, notif);
}

nlohmann::json McpBridge::read_response(McpSession& session, int timeout_ms) {
    // Poll for data with timeout
    struct pollfd pfd;
    pfd.fd = session.stdout_fd;
    pfd.events = POLLIN;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count());
        if (remaining <= 0) break;

        int ret = poll(&pfd, 1, std::min(remaining, 100));
        if (ret <= 0) continue;

        char buf[4096];
        ssize_t n = read(session.stdout_fd, buf, sizeof(buf));
        if (n <= 0) continue;

        session.read_buf.append(buf, static_cast<size_t>(n));

        // Look for complete JSON line
        size_t nl = session.read_buf.find('\n');
        while (nl != std::string::npos) {
            std::string line = session.read_buf.substr(0, nl);
            session.read_buf.erase(0, nl + 1);

            if (line.empty()) {
                nl = session.read_buf.find('\n');
                continue;
            }

            try {
                auto j = nlohmann::json::parse(line);
                // Skip notifications (no "id" field)
                if (j.contains("id")) {
                    return j;
                }
            } catch (...) {
                // Not valid JSON, skip
            }

            nl = session.read_buf.find('\n');
        }
    }

    return {{"error", {{"message", "timeout waiting for MCP response"}}}};
}

// ── MCP Protocol ───────────────────────────────────────────────

bool McpBridge::initialize_session(McpSession& session) {
    nlohmann::json params;
    params["protocolVersion"] = "2025-11";
    params["capabilities"] = nlohmann::json::object();
    params["clientInfo"] = {
        {"name", "clove"},
        {"version", "2.0.0"}
    };

    auto resp = send_request(session, "initialize", params);

    if (resp.contains("error")) return false;
    if (!resp.contains("result")) return false;

    // Send initialized notification
    send_notification(session, "notifications/initialized");
    return true;
}

bool McpBridge::discover_tools(McpSession& session) {
    auto resp = send_request(session, "tools/list");

    if (resp.contains("error")) return false;
    if (!resp.contains("result")) return false;

    session.tools.clear();
    auto& result = resp["result"];
    if (result.contains("tools") && result["tools"].is_array()) {
        for (const auto& t : result["tools"]) {
            McpTool tool;
            tool.server_name = session.config.name;
            tool.name = t.value("name", "");
            tool.description = t.value("description", "");
            tool.input_schema = t.value("inputSchema", nlohmann::json::object());
            session.tools.push_back(std::move(tool));
        }
    }

    return true;
}

// ── Permission check ───────────────────────────────────────────

bool McpBridge::agent_allowed(const McpSession& session,
                               const std::string& agent_name) const {
    // Empty allowed_agents = all agents allowed
    if (session.config.allowed_agents.empty()) return true;
    for (const auto& name : session.config.allowed_agents) {
        if (name == agent_name) return true;
    }
    return false;
}

} // namespace clove
