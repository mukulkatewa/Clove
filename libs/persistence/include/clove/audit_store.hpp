#pragma once

#include <clove/audit_log.hpp>
#include <memory>
#include <string>
#include <vector>

namespace clove {

class Database;

class AuditStore {
public:
    explicit AuditStore(Database& db);

    // Persist an audit entry
    bool store(const AuditLogEntry& entry);

    // Batch persist
    bool store_batch(const std::vector<AuditLogEntry>& entries);

    // Query persisted entries
    std::vector<AuditLogEntry> query(
        AuditCategory* category = nullptr,
        uint32_t* agent_id = nullptr,
        uint64_t since_id = 0,
        size_t limit = 100) const;

    // Count total entries
    size_t count() const;

private:
    Database& db_;
};

} // namespace clove
