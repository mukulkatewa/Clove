#include <clove/permissions.hpp>

#include <algorithm>
#include <cctype>

namespace clove {

// ---------------------------------------------------------------------------
// PermissionChecker
// ---------------------------------------------------------------------------

bool PermissionChecker::path_matches(const std::string& path,
                                     const std::string& pattern) {
    // Simple glob: supports * (single segment) and ** (any depth)
    // We use a recursive approach for correctness.
    size_t pi = 0, gi = 0;
    size_t plen = path.size(), glen = pattern.size();
    size_t star_g = std::string::npos, star_p = 0;

    // Handle ** as a special case first: replace "**" with a marker
    // For simplicity, we do recursive matching.
    auto match = [&](auto&& self, size_t p, size_t g) -> bool {
        if (g == glen) return p == plen;

        // "**" matches zero or more path segments (including separators)
        if (g + 1 < glen && pattern[g] == '*' && pattern[g + 1] == '*') {
            size_t next_g = g + 2;
            // Skip trailing '/' after **
            if (next_g < glen && pattern[next_g] == '/') ++next_g;

            // Try matching ** against zero or more characters
            for (size_t i = p; i <= plen; ++i) {
                if (self(self, i, next_g)) return true;
            }
            return false;
        }

        // Single '*' matches anything except '/'
        if (pattern[g] == '*') {
            size_t next_g = g + 1;
            for (size_t i = p; i <= plen; ++i) {
                if (i > p && path[i - 1] == '/') break;
                if (self(self, i, next_g)) return true;
            }
            return false;
        }

        // '?' matches a single non-slash character
        if (pattern[g] == '?') {
            if (p < plen && path[p] != '/') {
                return self(self, p + 1, g + 1);
            }
            return false;
        }

        // Literal match
        if (p < plen && path[p] == pattern[g]) {
            return self(self, p + 1, g + 1);
        }

        return false;
    };
    return [&](size_t p, size_t g) { return match(match, p, g); }(pi, gi);
}

bool PermissionChecker::command_matches(const std::string& command,
                                        const std::string& prefix) {
    if (prefix.empty()) return true;
    if (prefix == command) return true;
    // Check prefix match (e.g., "python" matches "python3 script.py")
    if (command.size() > prefix.size() &&
        command.compare(0, prefix.size(), prefix) == 0 &&
        (command[prefix.size()] == ' ' || command[prefix.size()] == '/')) {
        return true;
    }
    return false;
}

std::string PermissionChecker::extract_domain(const std::string& url) {
    // Strip scheme
    std::string work = url;
    auto scheme_end = work.find("://");
    if (scheme_end != std::string::npos) {
        work = work.substr(scheme_end + 3);
    }

    // Strip path
    auto slash = work.find('/');
    if (slash != std::string::npos) {
        work = work.substr(0, slash);
    }

    // Strip port
    auto colon = work.rfind(':');
    if (colon != std::string::npos) {
        // Make sure it's a port, not part of IPv6
        bool all_digits = true;
        for (size_t i = colon + 1; i < work.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(work[i]))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits && colon + 1 < work.size()) {
            work = work.substr(0, colon);
        }
    }

    // Strip userinfo
    auto at = work.find('@');
    if (at != std::string::npos) {
        work = work.substr(at + 1);
    }

    return work;
}

bool PermissionChecker::domain_matches(const std::string& domain,
                                       const std::string& pattern) {
    if (pattern == domain) return true;
    if (pattern == "*") return true;

    // Wildcard: *.example.com matches sub.example.com, a.b.example.com
    if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
        std::string suffix = pattern.substr(1); // ".example.com"
        if (domain.size() > suffix.size() &&
            domain.compare(domain.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// AgentPermissions — factory methods
// ---------------------------------------------------------------------------

AgentPermissions AgentPermissions::from_level(PermissionLevel level) {
    AgentPermissions p;

    switch (level) {
    case PermissionLevel::UNRESTRICTED:
        p.can_exec  = true;
        p.can_read  = true;
        p.can_write = true;
        p.can_think = true;
        p.can_spawn = true;
        p.can_http  = true;
        break;

    case PermissionLevel::STANDARD:
        p.can_exec  = false;
        p.can_read  = true;
        p.can_write = true;
        p.can_think = true;
        p.can_spawn = false;
        p.can_http  = false;
        break;

    case PermissionLevel::SANDBOXED:
        p.can_exec  = false;
        p.can_read  = true;
        p.can_write = false;
        p.can_think = true;
        p.can_spawn = false;
        p.can_http  = false;
        break;

    case PermissionLevel::READONLY:
        p.can_exec  = false;
        p.can_read  = true;
        p.can_write = false;
        p.can_think = false;
        p.can_spawn = false;
        p.can_http  = false;
        break;

    case PermissionLevel::MINIMAL:
        p.can_exec  = false;
        p.can_read  = false;
        p.can_write = false;
        p.can_think = true;
        p.can_spawn = false;
        p.can_http  = false;
        break;
    }

    return p;
}

AgentPermissions AgentPermissions::from_json(const nlohmann::json& j) {
    AgentPermissions p;

    auto get_bool = [&](const char* key, bool& out) {
        if (j.contains(key) && j[key].is_boolean()) out = j[key].get<bool>();
    };
    auto get_u32 = [&](const char* key, uint32_t& out) {
        if (j.contains(key) && j[key].is_number_unsigned())
            out = j[key].get<uint32_t>();
    };
    auto get_u64 = [&](const char* key, uint64_t& out) {
        if (j.contains(key) && j[key].is_number_unsigned())
            out = j[key].get<uint64_t>();
    };
    auto get_strings = [&](const char* key, std::vector<std::string>& out) {
        if (j.contains(key) && j[key].is_array()) {
            for (const auto& v : j[key]) {
                if (v.is_string()) out.push_back(v.get<std::string>());
            }
        }
    };

    get_bool("can_exec", p.can_exec);
    get_bool("can_read", p.can_read);
    get_bool("can_write", p.can_write);
    get_bool("can_think", p.can_think);
    get_bool("can_spawn", p.can_spawn);
    get_bool("can_http", p.can_http);

    get_strings("allowed_read_paths", p.allowed_read_paths);
    get_strings("allowed_write_paths", p.allowed_write_paths);
    get_strings("blocked_paths", p.blocked_paths);
    get_strings("allowed_commands", p.allowed_commands);
    get_strings("blocked_commands", p.blocked_commands);
    get_strings("allowed_domains", p.allowed_domains);
    get_strings("allowed_http_methods", p.allowed_http_methods);

    get_u64("max_exec_time_ms", p.max_exec_time_ms);

    // Support "level" shorthand
    if (j.contains("level") && j["level"].is_string()) {
        auto level_str = j["level"].get<std::string>();
        PermissionLevel level = PermissionLevel::STANDARD;
        if (level_str == "unrestricted") level = PermissionLevel::UNRESTRICTED;
        else if (level_str == "standard") level = PermissionLevel::STANDARD;
        else if (level_str == "sandboxed") level = PermissionLevel::SANDBOXED;
        else if (level_str == "readonly") level = PermissionLevel::READONLY;
        else if (level_str == "minimal") level = PermissionLevel::MINIMAL;

        auto base = from_level(level);
        // Merge: only override the boolean flags from level if not explicitly set
        if (!j.contains("can_exec")) p.can_exec = base.can_exec;
        if (!j.contains("can_read")) p.can_read = base.can_read;
        if (!j.contains("can_write")) p.can_write = base.can_write;
        if (!j.contains("can_think")) p.can_think = base.can_think;
        if (!j.contains("can_spawn")) p.can_spawn = base.can_spawn;
        if (!j.contains("can_http")) p.can_http = base.can_http;
    }

    return p;
}

nlohmann::json AgentPermissions::to_json() const {
    return nlohmann::json{
        {"can_exec", can_exec},
        {"can_read", can_read},
        {"can_write", can_write},
        {"can_think", can_think},
        {"can_spawn", can_spawn},
        {"can_http", can_http},
        {"allowed_read_paths", allowed_read_paths},
        {"allowed_write_paths", allowed_write_paths},
        {"blocked_paths", blocked_paths},
        {"allowed_commands", allowed_commands},
        {"blocked_commands", blocked_commands},
        {"allowed_domains", allowed_domains},
        {"allowed_http_methods", allowed_http_methods},
        {"max_exec_time_ms", max_exec_time_ms},
    };
}

// ---------------------------------------------------------------------------
// AgentPermissions — access checks
// ---------------------------------------------------------------------------

bool AgentPermissions::can_read_path(const std::string& path) const {
    if (!can_read) return false;

    // Check blocked paths first (deny list takes priority)
    for (const auto& blocked : blocked_paths) {
        if (PermissionChecker::path_matches(path, blocked)) {
            return false;
        }
    }

    // If no allow-list is configured, all paths are allowed
    if (allowed_read_paths.empty()) return true;

    // Must match at least one allowed pattern
    for (const auto& allowed : allowed_read_paths) {
        if (PermissionChecker::path_matches(path, allowed)) {
            return true;
        }
    }

    return false;
}

bool AgentPermissions::can_write_path(const std::string& path) const {
    if (!can_write) return false;

    // Check blocked paths first
    for (const auto& blocked : blocked_paths) {
        if (PermissionChecker::path_matches(path, blocked)) {
            return false;
        }
    }

    // If no allow-list is configured, all paths are allowed
    if (allowed_write_paths.empty()) return true;

    // Must match at least one allowed pattern
    for (const auto& allowed : allowed_write_paths) {
        if (PermissionChecker::path_matches(path, allowed)) {
            return true;
        }
    }

    return false;
}

bool AgentPermissions::can_execute_command(const std::string& command) const {
    if (!can_exec) return false;

    // Check blocked commands first
    for (const auto& blocked : blocked_commands) {
        if (PermissionChecker::command_matches(command, blocked)) {
            return false;
        }
    }

    // If no allow-list, all commands are allowed (when can_exec is true)
    if (allowed_commands.empty()) return true;

    for (const auto& allowed : allowed_commands) {
        if (PermissionChecker::command_matches(command, allowed)) {
            return true;
        }
    }

    return false;
}

bool AgentPermissions::can_access_domain(const std::string& domain) const {
    if (!can_http) return false;

    // If no allow-list, all domains are allowed
    if (allowed_domains.empty()) return true;

    for (const auto& allowed : allowed_domains) {
        if (PermissionChecker::domain_matches(domain, allowed)) {
            return true;
        }
    }

    return false;
}

bool AgentPermissions::can_http_method(const std::string& method) const {
    if (!can_http) return false;

    // If no allow-list, all methods are allowed
    if (allowed_http_methods.empty()) return true;

    // Case-insensitive comparison
    auto upper = [](const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        return r;
    };

    std::string m = upper(method);
    for (const auto& allowed : allowed_http_methods) {
        if (upper(allowed) == m) return true;
    }

    return false;
}

} // namespace clove
