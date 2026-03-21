#include <clove/audit_store.hpp>
#include <clove/database.hpp>
#include <sqlite3.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace clove {

AuditStore::AuditStore(Database& db) : db_(db) {}

bool AuditStore::store(const AuditLogEntry& entry) {
    std::lock_guard lock(db_.mutex());

    const char* sql = R"(
        INSERT INTO audit_log (category, event_type, agent_id, agent_name, details, success)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    std::string cat = audit_category_to_string(entry.category);
    std::string details = entry.details.dump();

    sqlite3_bind_text(stmt, 1, cat.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.event_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(entry.agent_id));
    sqlite3_bind_text(stmt, 4, entry.agent_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, details.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, entry.success ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AuditStore::store_batch(const std::vector<AuditLogEntry>& entries) {
    std::lock_guard lock(db_.mutex());

    db_.exec("BEGIN TRANSACTION");

    const char* sql = R"(
        INSERT INTO audit_log (category, event_type, agent_id, agent_name, details, success)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        db_.exec("ROLLBACK");
        return false;
    }

    for (const auto& entry : entries) {
        std::string cat = audit_category_to_string(entry.category);
        std::string details = entry.details.dump();

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, cat.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, entry.event_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, static_cast<int>(entry.agent_id));
        sqlite3_bind_text(stmt, 4, entry.agent_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, details.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, entry.success ? 1 : 0);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    db_.exec("COMMIT");
    return true;
}

std::vector<AuditLogEntry> AuditStore::query(
    AuditCategory* category, uint32_t* agent_id,
    uint64_t since_id, size_t limit) const {

    std::lock_guard lock(db_.mutex());

    std::string sql = "SELECT id, timestamp, category, event_type, agent_id, agent_name, details, success FROM audit_log WHERE 1=1";
    if (category) sql += " AND category = ?";
    if (agent_id) sql += " AND agent_id = ?";
    if (since_id > 0) sql += " AND id > " + std::to_string(since_id);
    sql += " ORDER BY id DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(), sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return {};

    int bind_idx = 1;
    if (category) {
        std::string cat = audit_category_to_string(*category);
        sqlite3_bind_text(stmt, bind_idx++, cat.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (agent_id) {
        sqlite3_bind_int(stmt, bind_idx++, static_cast<int>(*agent_id));
    }

    std::vector<AuditLogEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AuditLogEntry entry;
        entry.id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        // timestamp is text, skip parsing for now
        std::string cat_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.category = audit_category_from_string(cat_str);
        entry.event_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        entry.agent_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 4));
        entry.agent_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        const char* details_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        entry.details = nlohmann::json::parse(details_str ? details_str : "{}");
        entry.success = sqlite3_column_int(stmt, 7) != 0;
        results.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    return results;
}

size_t AuditStore::count() const {
    std::lock_guard lock(db_.mutex());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(), "SELECT COUNT(*) FROM audit_log", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace clove
