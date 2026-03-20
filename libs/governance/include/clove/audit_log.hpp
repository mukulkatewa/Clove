#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clove {

enum class AuditCategory {
    SECURITY,
    AGENT_LIFECYCLE,
    IPC,
    STATE_STORE,
    RESOURCE,
    SYSCALL,
    NETWORK,
    WORLD,
};

[[nodiscard]] const char*    audit_category_to_string(AuditCategory cat);
[[nodiscard]] AuditCategory  audit_category_from_string(const std::string& str);

struct AuditLogEntry {
    uint64_t                                id;
    std::chrono::system_clock::time_point   timestamp;
    AuditCategory                           category;
    std::string                             event_type;
    uint32_t                                agent_id;
    std::string                             agent_name;
    nlohmann::json                          details;
    bool                                    success;

    [[nodiscard]] nlohmann::json to_json() const;
    [[nodiscard]] std::string    to_jsonl() const;
};

struct AuditConfig {
    size_t max_entries    = 10000;
    bool   log_syscalls  = false;
    bool   log_security  = true;
    bool   log_lifecycle = true;
    bool   log_ipc       = false;
    bool   log_state     = false;
    bool   log_resource  = true;
    bool   log_network   = false;
    bool   log_world     = false;

    [[nodiscard]] bool is_enabled(AuditCategory cat) const;
};

class AuditLogger {
public:
    AuditLogger();
    explicit AuditLogger(const AuditConfig& config);

    void log(AuditCategory category, const std::string& event_type,
             uint32_t agent_id, const std::string& agent_name,
             const nlohmann::json& details, bool success = true);

    void log_security(const std::string& event_type, uint32_t agent_id,
                      const std::string& agent_name,
                      const nlohmann::json& details);

    void log_lifecycle(const std::string& event_type, uint32_t agent_id,
                       const std::string& agent_name,
                       const nlohmann::json& details);

    [[nodiscard]] std::vector<AuditLogEntry> get_entries(
        AuditCategory* category = nullptr,
        uint32_t*      agent_id = nullptr,
        uint64_t       since_id = 0,
        size_t         limit    = 100) const;

    [[nodiscard]] std::string export_jsonl(size_t limit = 0) const;

    void set_config(const AuditConfig& config);
    void clear();
    [[nodiscard]] size_t entry_count() const;

private:
    AuditConfig             config_;
    std::deque<AuditLogEntry> entries_;
    mutable std::mutex      mutex_;
    uint64_t                next_id_ = 1;

    void trim_entries();
};

} // namespace clove
