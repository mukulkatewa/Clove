// CLOVE CLI — Command-line interface for the CLOVE kernel REST API
// Single-file implementation using cpp-httplib as HTTP client.

#include <clove/version.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

// ── Terminal colors ─────────────────────────────────────────────────────────

namespace term {
    constexpr const char* RESET  = "\033[0m";
    constexpr const char* BOLD   = "\033[1m";
    constexpr const char* DIM    = "\033[2m";
    constexpr const char* RED    = "\033[31m";
    constexpr const char* GREEN  = "\033[32m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* BLUE   = "\033[34m";
    constexpr const char* CYAN   = "\033[36m";
    constexpr const char* WHITE  = "\033[37m";
} // namespace term

// ── Helpers ─────────────────────────────────────────────────────────────────

static void print_error(const std::string& msg) {
    std::cerr << term::RED << term::BOLD << "error" << term::RESET
              << ": " << msg << "\n";
}

static void print_ok(const std::string& msg) {
    std::cout << term::GREEN << "✓" << term::RESET << "  " << msg << "\n";
}

/// Format seconds into a human-readable duration string.
static std::string format_duration(int64_t total_seconds) {
    if (total_seconds < 0) return "—";
    int64_t days    = total_seconds / 86400;
    int64_t hours   = (total_seconds % 86400) / 3600;
    int64_t minutes = (total_seconds % 3600) / 60;
    std::ostringstream os;
    if (days > 0)    os << days << "d ";
    if (hours > 0)   os << hours << "h ";
    os << minutes << "m";
    return os.str();
}

/// Right-pad a string to at least `width` characters.
static std::string pad(const std::string& s, size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

/// Colorize an agent/service state string.
static std::string colorize_state(const std::string& state) {
    if (state == "running")
        return std::string(term::GREEN) + state + term::RESET;
    if (state == "paused" || state == "suspended")
        return std::string(term::YELLOW) + state + term::RESET;
    if (state == "stopped" || state == "dead" || state == "crashed")
        return std::string(term::RED) + state + term::RESET;
    return state;
}

/// Safely get a string from a JSON value, returning a fallback if missing or wrong type.
static std::string jstr(const json& j, const std::string& key, const std::string& fallback = "—") {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return fallback;
}

/// Safely get an integer from a JSON value.
static int64_t jint(const json& j, const std::string& key, int64_t fallback = 0) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<int64_t>();
    return fallback;
}

// ── HTTP client wrapper ─────────────────────────────────────────────────────

struct CliConfig {
    std::string host    = "localhost";
    uint16_t    port    = 8080;
    std::string api_key;
};

class ApiClient {
public:
    explicit ApiClient(const CliConfig& cfg)
        : client_(cfg.host, cfg.port)
        , api_key_(cfg.api_key)
    {
        client_.set_connection_timeout(5);
        client_.set_read_timeout(30);
    }

    /// Perform a GET request. Returns {status_code, parsed JSON body}.
    std::pair<int, json> get(const std::string& path) {
        httplib::Headers headers;
        if (!api_key_.empty()) headers.emplace("Authorization", "Bearer " + api_key_);

        auto res = client_.Get(path, headers);
        if (!res) return {0, json{{"error", "connection refused — is the kernel running?"}}};
        return parse_response(res);
    }

    /// Perform a POST request with a JSON body.
    std::pair<int, json> post(const std::string& path, const json& body = json::object()) {
        httplib::Headers headers;
        if (!api_key_.empty()) headers.emplace("Authorization", "Bearer " + api_key_);

        auto res = client_.Post(path, headers, body.dump(), "application/json");
        if (!res) return {0, json{{"error", "connection refused — is the kernel running?"}}};
        return parse_response(res);
    }

    /// Perform a DELETE request.
    std::pair<int, json> del(const std::string& path) {
        httplib::Headers headers;
        if (!api_key_.empty()) headers.emplace("Authorization", "Bearer " + api_key_);

        auto res = client_.Delete(path, headers);
        if (!res) return {0, json{{"error", "connection refused — is the kernel running?"}}};
        return parse_response(res);
    }

private:
    static std::pair<int, json> parse_response(const httplib::Result& res) {
        json j;
        try {
            j = json::parse(res->body);
        } catch (...) {
            j = json{{"raw", res->body}};
        }
        return {res->status, j};
    }

    httplib::Client client_;
    std::string api_key_;
};

// ── Command handlers ────────────────────────────────────────────────────────

static int cmd_status(ApiClient& api) {
    auto [code, body] = api.get("/api/health");
    if (code == 0) { print_error(jstr(body, "error")); return 1; }

    std::cout << "\n"
              << term::CYAN << term::BOLD << "CLOVE Kernel" << term::RESET
              << term::DIM << " v" << clove::VERSION << term::RESET << "\n"
              << "  Status:   " << (code == 200 ? std::string(term::GREEN) + "ok" + term::RESET
                                                : std::string(term::RED)   + "degraded" + term::RESET) << "\n"
              << "  Uptime:   " << format_duration(jint(body, "uptime_seconds", -1)) << "\n"
              << "  Syscalls: " << jint(body, "syscall_count", 0) << "\n"
              << "\n";
    return 0;
}

static int cmd_agents(ApiClient& api) {
    auto [code, body] = api.get("/api/agents");
    if (code == 0) { print_error(jstr(body, "error")); return 1; }
    if (code != 200) { print_error("HTTP " + std::to_string(code)); return 1; }

    auto agents = body.value("agents", json::array());
    if (agents.empty()) {
        std::cout << term::DIM << "  No agents running." << term::RESET << "\n";
        return 0;
    }

    // Header
    std::cout << "\n"
              << term::BOLD
              << "  " << pad("ID", 6) << pad("NAME", 14) << pad("STATE", 12)
              << pad("PID", 8) << "UPTIME" << term::RESET << "\n";

    for (auto& a : agents) {
        std::string id    = std::to_string(jint(a, "id"));
        std::string name  = jstr(a, "name");
        std::string state = jstr(a, "state", "unknown");
        std::string pid   = std::to_string(jint(a, "pid"));
        std::string up    = format_duration(jint(a, "uptime_seconds", -1));

        std::cout << "  " << pad(id, 6) << pad(name, 14)
                  << pad(colorize_state(state), 12 + 9)  // +9 for ANSI escape chars
                  << pad(pid, 8) << up << "\n";
    }
    std::cout << "\n";
    return 0;
}

static int cmd_spawn(ApiClient& api, const std::string& name, const std::string& command) {
    json body = {{"name", name}, {"command", command}};
    auto [code, resp] = api.post("/api/agents", body);
    if (code == 0) { print_error(jstr(resp, "error")); return 1; }
    if (code >= 400) { print_error(jstr(resp, "error", "spawn failed (HTTP " + std::to_string(code) + ")")); return 1; }

    print_ok(std::string("Spawned agent ") + term::BOLD + name + term::RESET
             + " (id=" + std::to_string(jint(resp, "id")) + ", pid=" + std::to_string(jint(resp, "pid")) + ")");
    return 0;
}

static int cmd_kill(ApiClient& api, const std::string& id) {
    auto [code, resp] = api.del("/api/agents/" + id);
    if (code == 0)    { print_error(jstr(resp, "error")); return 1; }
    if (code >= 400)  { print_error(jstr(resp, "error", "kill failed (HTTP " + std::to_string(code) + ")")); return 1; }
    print_ok("Killed agent " + id);
    return 0;
}

static int cmd_metrics(ApiClient& api, const std::string& agent_id) {
    std::string path = agent_id.empty() ? "/api/metrics" : "/api/agents/" + agent_id + "/metrics";
    auto [code, body] = api.get(path);
    if (code == 0)    { print_error(jstr(body, "error")); return 1; }
    if (code != 200)  { print_error("HTTP " + std::to_string(code)); return 1; }

    std::cout << "\n" << term::BOLD << "  Metrics" << term::RESET;
    if (!agent_id.empty()) std::cout << " (agent " << agent_id << ")";
    std::cout << "\n";

    // Print each key-value pair
    for (auto& [k, v] : body.items()) {
        std::cout << "  " << term::DIM << pad(k, 28) << term::RESET;
        if (v.is_number_float())
            std::cout << std::fixed << std::setprecision(2) << v.get<double>();
        else if (v.is_number())
            std::cout << v.get<int64_t>();
        else
            std::cout << v.dump();
        std::cout << "\n";
    }
    std::cout << "\n";
    return 0;
}

static int cmd_audit(ApiClient& api, const std::string& category, int limit) {
    std::string path = "/api/audit?limit=" + std::to_string(limit);
    if (!category.empty()) path += "&category=" + category;

    auto [code, body] = api.get(path);
    if (code == 0)    { print_error(jstr(body, "error")); return 1; }
    if (code != 200)  { print_error("HTTP " + std::to_string(code)); return 1; }

    auto entries = body.value("entries", json::array());
    if (entries.empty()) {
        std::cout << term::DIM << "  No audit entries." << term::RESET << "\n";
        return 0;
    }

    std::cout << "\n" << term::BOLD
              << "  " << pad("TIME", 22) << pad("CATEGORY", 14) << pad("AGENT", 8) << "ACTION"
              << term::RESET << "\n";

    for (auto& e : entries) {
        std::cout << "  " << pad(jstr(e, "timestamp"), 22)
                  << pad(jstr(e, "category"), 14)
                  << pad(std::to_string(jint(e, "agent_id")), 8)
                  << jstr(e, "action") << "\n";
    }
    std::cout << "\n";
    return 0;
}

static int cmd_record(ApiClient& api, const std::string& action) {
    if (action == "status") {
        auto [code, body] = api.get("/api/replay/status");
        if (code == 0)   { print_error(jstr(body, "error")); return 1; }
        if (code != 200) { print_error("HTTP " + std::to_string(code)); return 1; }
        std::cout << "  Recording: " << (body.value("recording", false)
                     ? std::string(term::GREEN) + "active" + term::RESET
                     : std::string(term::DIM)   + "inactive" + term::RESET) << "\n";
        return 0;
    }
    if (action == "start" || action == "stop") {
        auto [code, body] = api.post("/api/replay/" + action);
        if (code == 0)   { print_error(jstr(body, "error")); return 1; }
        if (code >= 400) { print_error(jstr(body, "error", "record " + action + " failed")); return 1; }
        print_ok("Recording " + action + (action == "start" ? "ed" : "ped"));
        return 0;
    }
    print_error("unknown record action: " + action + " (use start, stop, or status)");
    return 1;
}

static int cmd_worlds(ApiClient& api) {
    auto [code, body] = api.get("/api/worlds");
    if (code == 0)    { print_error(jstr(body, "error")); return 1; }
    if (code != 200)  { print_error("HTTP " + std::to_string(code)); return 1; }

    auto worlds = body.value("worlds", json::array());
    if (worlds.empty()) {
        std::cout << term::DIM << "  No worlds." << term::RESET << "\n";
        return 0;
    }

    std::cout << "\n" << term::BOLD
              << "  " << pad("ID", 6) << pad("NAME", 20) << "AGENTS"
              << term::RESET << "\n";

    for (auto& w : worlds) {
        std::cout << "  " << pad(std::to_string(jint(w, "id")), 6)
                  << pad(jstr(w, "name"), 20)
                  << jint(w, "agent_count") << "\n";
    }
    std::cout << "\n";
    return 0;
}

static int cmd_worlds_create(ApiClient& api, const std::string& name) {
    json body = {{"name", name}};
    auto [code, resp] = api.post("/api/worlds", body);
    if (code == 0)    { print_error(jstr(resp, "error")); return 1; }
    if (code >= 400)  { print_error(jstr(resp, "error", "create world failed")); return 1; }
    print_ok(std::string("Created world ") + term::BOLD + name + term::RESET
             + " (id=" + std::to_string(jint(resp, "id")) + ")");
    return 0;
}

static int cmd_worlds_destroy(ApiClient& api, const std::string& id) {
    auto [code, resp] = api.del("/api/worlds/" + id);
    if (code == 0)    { print_error(jstr(resp, "error")); return 1; }
    if (code >= 400)  { print_error(jstr(resp, "error", "destroy world failed")); return 1; }
    print_ok("Destroyed world " + id);
    return 0;
}

static int cmd_pii(ApiClient& api, const std::string& text) {
    json body = {{"text", text}};
    auto [code, resp] = api.post("/api/privacy/scan", body);
    if (code == 0)    { print_error(jstr(resp, "error")); return 1; }
    if (code >= 400)  { print_error(jstr(resp, "error", "PII scan failed")); return 1; }

    auto findings = resp.value("findings", json::array());
    if (findings.empty()) {
        print_ok("No PII detected.");
        return 0;
    }

    std::cout << "\n" << term::BOLD << "  PII Findings" << term::RESET << "\n";
    for (auto& f : findings) {
        std::cout << "  " << term::YELLOW << "!" << term::RESET
                  << "  " << jstr(f, "type") << ": " << jstr(f, "value") << "\n";
    }
    std::cout << "\n";

    if (resp.contains("redacted") && resp["redacted"].is_string()) {
        std::cout << "  " << term::DIM << "Redacted:" << term::RESET
                  << " " << resp["redacted"].get<std::string>() << "\n\n";
    }
    return 0;
}

static int cmd_recommendations(ApiClient& api) {
    auto [code, body] = api.get("/api/policy/recommendations");
    if (code == 0)    { print_error(jstr(body, "error")); return 1; }
    if (code != 200)  { print_error("HTTP " + std::to_string(code)); return 1; }

    auto recs = body.value("recommendations", json::array());
    if (recs.empty()) {
        print_ok("No recommendations — policy looks good.");
        return 0;
    }

    std::cout << "\n" << term::BOLD << "  Policy Recommendations" << term::RESET << "\n";
    for (auto& r : recs) {
        std::string severity = jstr(r, "severity", "info");
        const char* color = term::BLUE;
        if (severity == "warning") color = term::YELLOW;
        if (severity == "critical") color = term::RED;

        std::cout << "  " << color << severity << term::RESET
                  << "  " << jstr(r, "message") << "\n";
    }
    std::cout << "\n";
    return 0;
}

// ── Usage ───────────────────────────────────────────────────────────────────

static void print_usage() {
    std::cout << "\n"
    << term::CYAN << term::BOLD << "CLOVE CLI" << term::RESET
    << term::DIM << " v" << clove::VERSION << term::RESET << "\n"
    << "Command-line interface for the CLOVE agent fleet kernel.\n\n"

    << term::BOLD << "USAGE" << term::RESET << "\n"
    << "  clove [options] <command> [args...]\n\n"

    << term::BOLD << "COMMANDS" << term::RESET << "\n"
    << "  " << pad("status", 38) << "Show kernel health and uptime\n"
    << "  " << pad("agents, ps", 38) << "List running agents\n"
    << "  " << pad("spawn <name> <command>", 38) << "Spawn a new agent process\n"
    << "  " << pad("kill <id>", 38) << "Kill an agent by ID\n"
    << "  " << pad("metrics [agent_id]", 38) << "Show kernel or per-agent metrics\n"
    << "  " << pad("audit [--category X] [--limit N]", 38) << "Query audit log\n"
    << "  " << pad("record start|stop|status", 38) << "Control execution recording\n"
    << "  " << pad("worlds", 38) << "List sandbox worlds\n"
    << "  " << pad("worlds create <name>", 38) << "Create a new world\n"
    << "  " << pad("worlds destroy <id>", 38) << "Destroy a world\n"
    << "  " << pad("pii <text>", 38) << "Scan text for PII\n"
    << "  " << pad("recommendations", 38) << "Show policy recommendations\n"
    << "  " << pad("help", 38) << "Show this help message\n"
    << "\n"

    << term::BOLD << "OPTIONS" << term::RESET << "\n"
    << "  " << pad("--host <host>", 38) << "Kernel host (default: localhost)\n"
    << "  " << pad("--port <port>", 38) << "Kernel port (default: 8080)\n"
    << "  " << pad("--api-key <key>", 38) << "API key (or CLOVE_API_KEY env var)\n"
    << "\n"

    << term::BOLD << "EXAMPLES" << term::RESET << "\n"
    << term::DIM
    << "  clove status\n"
    << "  clove spawn researcher \"python3 agent.py\"\n"
    << "  clove ps\n"
    << "  clove kill 3\n"
    << "  clove audit --category permission --limit 20\n"
    << "  clove --port 9090 metrics\n"
    << term::RESET << "\n";
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    CliConfig cfg;

    // Check env var for API key
    if (const char* env_key = std::getenv("CLOVE_API_KEY"))
        cfg.api_key = env_key;

    // Parse global options and collect positional args
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--host" && i + 1 < argc)      { cfg.host = argv[++i]; continue; }
        if (a == "--port" && i + 1 < argc)      { cfg.port = static_cast<uint16_t>(std::stoi(argv[++i])); continue; }
        if (a == "--api-key" && i + 1 < argc)   { cfg.api_key = argv[++i]; continue; }
        args.push_back(a);
    }

    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        print_usage();
        return 0;
    }

    ApiClient api(cfg);
    const std::string& cmd = args[0];

    // ── status ──────────────────────────────────────────────────────────
    if (cmd == "status") {
        return cmd_status(api);
    }

    // ── agents / ps ─────────────────────────────────────────────────────
    if (cmd == "agents" || cmd == "ps") {
        return cmd_agents(api);
    }

    // ── spawn <name> <command> ──────────────────────────────────────────
    if (cmd == "spawn") {
        if (args.size() < 3) {
            print_error("usage: clove spawn <name> <command>");
            return 1;
        }
        // Join remaining args as the command string
        std::string command;
        for (size_t i = 2; i < args.size(); i++) {
            if (!command.empty()) command += " ";
            command += args[i];
        }
        return cmd_spawn(api, args[1], command);
    }

    // ── kill <id> ───────────────────────────────────────────────────────
    if (cmd == "kill") {
        if (args.size() < 2) { print_error("usage: clove kill <id>"); return 1; }
        return cmd_kill(api, args[1]);
    }

    // ── metrics [agent_id] ──────────────────────────────────────────────
    if (cmd == "metrics") {
        std::string agent_id = args.size() > 1 ? args[1] : "";
        return cmd_metrics(api, agent_id);
    }

    // ── audit [--category X] [--limit N] ────────────────────────────────
    if (cmd == "audit") {
        std::string category;
        int limit = 50;
        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == "--category" && i + 1 < args.size()) { category = args[++i]; continue; }
            if (args[i] == "--limit"    && i + 1 < args.size()) { limit = std::stoi(args[++i]); continue; }
        }
        return cmd_audit(api, category, limit);
    }

    // ── record start|stop|status ────────────────────────────────────────
    if (cmd == "record") {
        if (args.size() < 2) { print_error("usage: clove record start|stop|status"); return 1; }
        return cmd_record(api, args[1]);
    }

    // ── worlds [create <name> | destroy <id>] ───────────────────────────
    if (cmd == "worlds") {
        if (args.size() == 1) return cmd_worlds(api);
        if (args[1] == "create") {
            if (args.size() < 3) { print_error("usage: clove worlds create <name>"); return 1; }
            return cmd_worlds_create(api, args[2]);
        }
        if (args[1] == "destroy") {
            if (args.size() < 3) { print_error("usage: clove worlds destroy <id>"); return 1; }
            return cmd_worlds_destroy(api, args[2]);
        }
        print_error("unknown worlds subcommand: " + args[1]);
        return 1;
    }

    // ── pii <text> ──────────────────────────────────────────────────────
    if (cmd == "pii") {
        if (args.size() < 2) { print_error("usage: clove pii <text>"); return 1; }
        // Join remaining args as the text to scan
        std::string text;
        for (size_t i = 1; i < args.size(); i++) {
            if (!text.empty()) text += " ";
            text += args[i];
        }
        return cmd_pii(api, text);
    }

    // ── recommendations ─────────────────────────────────────────────────
    if (cmd == "recommendations") {
        return cmd_recommendations(api);
    }

    // ── unknown command ─────────────────────────────────────────────────
    print_error("unknown command: " + cmd);
    std::cerr << "Run " << term::BOLD << "clove help" << term::RESET << " for usage.\n";
    return 1;
}
