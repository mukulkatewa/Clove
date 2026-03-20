#include <clove/audit_log.hpp>

#include <iomanip>
#include <sstream>

namespace clove {

// ---------------------------------------------------------------------------
// Category string conversion
// ---------------------------------------------------------------------------

const char* audit_category_to_string(AuditCategory cat) {
    switch (cat) {
    case AuditCategory::SECURITY:        return "security";
    case AuditCategory::AGENT_LIFECYCLE: return "agent_lifecycle";
    case AuditCategory::IPC:             return "ipc";
    case AuditCategory::STATE_STORE:     return "state_store";
    case AuditCategory::RESOURCE:        return "resource";
    case AuditCategory::SYSCALL:         return "syscall";
    case AuditCategory::NETWORK:         return "network";
    case AuditCategory::WORLD:           return "world";
    }
    return "unknown";
}

AuditCategory audit_category_from_string(const std::string& str) {
    if (str == "security")        return AuditCategory::SECURITY;
    if (str == "agent_lifecycle") return AuditCategory::AGENT_LIFECYCLE;
    if (str == "ipc")             return AuditCategory::IPC;
    if (str == "state_store")     return AuditCategory::STATE_STORE;
    if (str == "resource")        return AuditCategory::RESOURCE;
    if (str == "syscall")         return AuditCategory::SYSCALL;
    if (str == "network")         return AuditCategory::NETWORK;
    if (str == "world")           return AuditCategory::WORLD;
    return AuditCategory::SECURITY; // safe default
}

// ---------------------------------------------------------------------------
// AuditConfig
// ---------------------------------------------------------------------------

bool AuditConfig::is_enabled(AuditCategory cat) const {
    switch (cat) {
    case AuditCategory::SECURITY:        return log_security;
    case AuditCategory::AGENT_LIFECYCLE: return log_lifecycle;
    case AuditCategory::IPC:             return log_ipc;
    case AuditCategory::STATE_STORE:     return log_state;
    case AuditCategory::RESOURCE:        return log_resource;
    case AuditCategory::SYSCALL:         return log_syscalls;
    case AuditCategory::NETWORK:         return log_network;
    case AuditCategory::WORLD:           return log_world;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Timestamp formatting (ISO 8601)
// ---------------------------------------------------------------------------

static std::string format_timestamp(
    const std::chrono::system_clock::time_point& tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  tp.time_since_epoch())
                  .count() %
              1'000'000;

    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(6) << us << 'Z';
    return oss.str();
}

// ---------------------------------------------------------------------------
// AuditLogEntry
// ---------------------------------------------------------------------------

nlohmann::json AuditLogEntry::to_json() const {
    return nlohmann::json{
        {"id",         id},
        {"timestamp",  format_timestamp(timestamp)},
        {"category",   audit_category_to_string(category)},
        {"event_type", event_type},
        {"agent_id",   agent_id},
        {"agent_name", agent_name},
        {"details",    details},
        {"success",    success},
    };
}

std::string AuditLogEntry::to_jsonl() const {
    return to_json().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// ---------------------------------------------------------------------------
// AuditLogger
// ---------------------------------------------------------------------------

AuditLogger::AuditLogger() = default;

AuditLogger::AuditLogger(const AuditConfig& config) : config_(config) {}

void AuditLogger::set_config(const AuditConfig& config) {
    std::lock_guard lock(mutex_);
    config_ = config;
}

void AuditLogger::log(AuditCategory category, const std::string& event_type,
                      uint32_t agent_id, const std::string& agent_name,
                      const nlohmann::json& details, bool success) {
    std::lock_guard lock(mutex_);

    if (!config_.is_enabled(category)) return;

    AuditLogEntry entry{
        .id         = next_id_++,
        .timestamp  = std::chrono::system_clock::now(),
        .category   = category,
        .event_type = event_type,
        .agent_id   = agent_id,
        .agent_name = agent_name,
        .details    = details,
        .success    = success,
    };

    entries_.push_back(std::move(entry));
    trim_entries();
}

void AuditLogger::log_security(const std::string& event_type,
                               uint32_t agent_id,
                               const std::string& agent_name,
                               const nlohmann::json& details) {
    log(AuditCategory::SECURITY, event_type, agent_id, agent_name, details, true);
}

void AuditLogger::log_lifecycle(const std::string& event_type,
                                uint32_t agent_id,
                                const std::string& agent_name,
                                const nlohmann::json& details) {
    log(AuditCategory::AGENT_LIFECYCLE, event_type, agent_id, agent_name,
        details, true);
}

std::vector<AuditLogEntry> AuditLogger::get_entries(
    AuditCategory* category, uint32_t* agent_id, uint64_t since_id,
    size_t limit) const {
    std::lock_guard lock(mutex_);

    std::vector<AuditLogEntry> result;
    result.reserve(std::min(limit, entries_.size()));

    for (const auto& entry : entries_) {
        if (entry.id <= since_id) continue;
        if (category && entry.category != *category) continue;
        if (agent_id && entry.agent_id != *agent_id) continue;

        result.push_back(entry);
        if (result.size() >= limit) break;
    }

    return result;
}

std::string AuditLogger::export_jsonl(size_t limit) const {
    std::lock_guard lock(mutex_);

    std::string output;
    size_t count = 0;

    for (const auto& entry : entries_) {
        output += entry.to_jsonl();
        output += '\n';
        ++count;
        if (limit > 0 && count >= limit) break;
    }

    return output;
}

void AuditLogger::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    next_id_ = 1;
}

size_t AuditLogger::entry_count() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

void AuditLogger::trim_entries() {
    // Caller must hold mutex_
    while (entries_.size() > config_.max_entries) {
        entries_.pop_front();
    }
}

} // namespace clove
