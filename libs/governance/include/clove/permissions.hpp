#pragma once

#include <clove/types.hpp>

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clove {

// ---------------------------------------------------------------------------
// AgentBudget — four hard limits enforced at kernel level
// ---------------------------------------------------------------------------
struct AgentBudget {
    // Limits (0 = unlimited)
    uint64_t max_tokens    = 0;
    uint32_t max_steps     = 0;
    uint64_t max_time_ms   = 0;
    double   max_cost_usd  = 0.0;
    bool     kill_on_exceeded = false;

    // Tracked usage
    uint64_t tokens_used   = 0;
    uint32_t steps_taken   = 0;
    uint64_t started_at_ms = 0;  // 0 = timer not started
    double   cost_usd      = 0.0;

    // --- Check methods (return false if budget exceeded) ---
    [[nodiscard]] bool check_tokens(uint64_t estimate = 0) const {
        if (max_tokens == 0) return true;
        return (tokens_used + estimate) <= max_tokens;
    }
    [[nodiscard]] bool check_steps() const {
        if (max_steps == 0) return true;
        return steps_taken <= max_steps;
    }
    [[nodiscard]] bool check_time(uint64_t now_ms) const {
        if (max_time_ms == 0 || started_at_ms == 0) return true;
        return (now_ms - started_at_ms) <= max_time_ms;
    }
    [[nodiscard]] bool check_cost(double estimate = 0.0) const {
        if (max_cost_usd <= 0.0) return true;
        return (cost_usd + estimate) <= max_cost_usd;
    }
    [[nodiscard]] bool any_exceeded(uint64_t now_ms) const {
        return !check_tokens() || !check_steps() || !check_time(now_ms) || !check_cost();
    }

    // --- Which budget is exceeded (for error messages) ---
    [[nodiscard]] std::string exceeded_type(uint64_t now_ms) const {
        if (!check_steps()) return "step";
        if (!check_tokens()) return "token";
        if (!check_time(now_ms)) return "time";
        if (!check_cost()) return "cost";
        return "";
    }

    // --- Record methods ---
    void record_tokens(uint64_t tokens) { tokens_used += tokens; }
    void record_step() { steps_taken += 1; }
    void record_cost(double usd) { cost_usd += usd; }
    void start_timer(uint64_t now_ms) {
        if (started_at_ms == 0) started_at_ms = now_ms;
    }
    void reset() {
        tokens_used = 0; steps_taken = 0; started_at_ms = 0; cost_usd = 0.0;
    }

    // --- Serialization ---
    [[nodiscard]] nlohmann::json to_json() const {
        uint64_t elapsed = 0;
        if (started_at_ms > 0) {
            auto now = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            elapsed = now - started_at_ms;
        }
        return {
            {"max_tokens", max_tokens}, {"tokens_used", tokens_used},
            {"max_steps", max_steps}, {"steps_taken", steps_taken},
            {"max_time_ms", max_time_ms}, {"elapsed_ms", elapsed},
            {"max_cost_usd", max_cost_usd}, {"cost_usd", cost_usd},
            {"kill_on_exceeded", kill_on_exceeded},
        };
    }
    static AgentBudget from_json(const nlohmann::json& j) {
        AgentBudget b;
        if (j.contains("max_tokens")) b.max_tokens = j["max_tokens"].get<uint64_t>();
        if (j.contains("max_steps")) b.max_steps = j["max_steps"].get<uint32_t>();
        if (j.contains("max_time_ms")) b.max_time_ms = j["max_time_ms"].get<uint64_t>();
        if (j.contains("max_cost_usd")) b.max_cost_usd = j["max_cost_usd"].get<double>();
        if (j.contains("kill_on_exceeded")) b.kill_on_exceeded = j["kill_on_exceeded"].get<bool>();
        if (j.contains("tokens_used")) b.tokens_used = j["tokens_used"].get<uint64_t>();
        if (j.contains("steps_taken")) b.steps_taken = j["steps_taken"].get<uint32_t>();
        if (j.contains("cost_usd")) b.cost_usd = j["cost_usd"].get<double>();
        return b;
    }
};

// ---------------------------------------------------------------------------
// AgentPermissions — capability flags + ACLs + LLM quotas
// ---------------------------------------------------------------------------
struct AgentPermissions {
    bool can_exec  = false;
    bool can_read  = true;
    bool can_write = true;
    bool can_think = true;
    bool can_spawn = false;
    bool can_http  = false;

    std::vector<std::string> allowed_read_paths;
    std::vector<std::string> allowed_write_paths;
    std::vector<std::string> blocked_paths;
    std::vector<std::string> allowed_commands;
    std::vector<std::string> blocked_commands;
    std::vector<std::string> allowed_domains;
    std::vector<std::string> allowed_http_methods;

    uint64_t max_exec_time_ms = 30000;

    // NOTE: LLM token/cost tracking is handled by AgentBudget (above),
    // NOT by AgentPermissions. AgentPermissions only controls capabilities.

    static AgentPermissions from_json(const nlohmann::json& j);
    [[nodiscard]] nlohmann::json to_json() const;
    static AgentPermissions from_level(PermissionLevel level);

    [[nodiscard]] bool can_read_path(const std::string& path) const;
    [[nodiscard]] bool can_write_path(const std::string& path) const;
    [[nodiscard]] bool can_execute_command(const std::string& command) const;
    [[nodiscard]] bool can_access_domain(const std::string& domain) const;
    [[nodiscard]] bool can_http_method(const std::string& method) const;
};

class PermissionChecker {
public:
    static bool path_matches(const std::string& path, const std::string& pattern);
    static bool command_matches(const std::string& command, const std::string& prefix);
    static std::string extract_domain(const std::string& url);
    static bool domain_matches(const std::string& domain, const std::string& pattern);
};

} // namespace clove
