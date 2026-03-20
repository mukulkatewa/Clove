#pragma once

#include <clove/types.hpp>

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clove {

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

    uint64_t max_llm_tokens   = 0; // 0 = unlimited
    uint32_t max_llm_calls    = 0; // 0 = unlimited
    uint64_t max_exec_time_ms = 30000;

    uint64_t llm_tokens_used = 0;
    uint32_t llm_calls_made  = 0;

    static AgentPermissions from_json(const nlohmann::json& j);
    [[nodiscard]] nlohmann::json to_json() const;
    static AgentPermissions from_level(PermissionLevel level);

    [[nodiscard]] bool can_read_path(const std::string& path) const;
    [[nodiscard]] bool can_write_path(const std::string& path) const;
    [[nodiscard]] bool can_execute_command(const std::string& command) const;
    [[nodiscard]] bool can_access_domain(const std::string& domain) const;
    [[nodiscard]] bool can_http_method(const std::string& method) const;
    [[nodiscard]] bool can_use_llm(uint32_t estimated_tokens = 0) const;
    void record_llm_usage(uint32_t tokens);
};

class PermissionChecker {
public:
    static bool path_matches(const std::string& path, const std::string& pattern);
    static bool command_matches(const std::string& command, const std::string& prefix);
    static std::string extract_domain(const std::string& url);
    static bool domain_matches(const std::string& domain, const std::string& pattern);
};

} // namespace clove
