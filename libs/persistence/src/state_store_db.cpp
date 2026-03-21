#include <clove/state_store_db.hpp>
#include <clove/database.hpp>
#include <sqlite3.h>

namespace clove {

StateStoreDb::StateStoreDb(Database& db) : db_(db) {}

bool StateStoreDb::store(const std::string& key, const nlohmann::json& value,
                          uint32_t agent_id, const std::string& scope) {
    std::lock_guard lock(db_.mutex());

    const char* sql = R"(
        INSERT OR REPLACE INTO state_store (key, value, agent_id, scope, updated_at)
        VALUES (?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
    )";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    std::string val_str = value.dump();
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, val_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(agent_id));
    sqlite3_bind_text(stmt, 4, scope.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<nlohmann::json> StateStoreDb::fetch(const std::string& key) const {
    std::lock_guard lock(db_.mutex());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(),
        "SELECT value FROM state_store WHERE key = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return std::nullopt;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<nlohmann::json> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (val) result = nlohmann::json::parse(val);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool StateStoreDb::erase(const std::string& key) {
    std::lock_guard lock(db_.mutex());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(),
        "DELETE FROM state_store WHERE key = ?", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<std::string> StateStoreDb::keys(const std::string& prefix) const {
    std::lock_guard lock(db_.mutex());

    std::string sql = "SELECT key FROM state_store";
    if (!prefix.empty()) sql += " WHERE key LIKE ? || '%'";
    sql += " ORDER BY key";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(), sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return {};

    if (!prefix.empty()) {
        sqlite3_bind_text(stmt, 1, prefix.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::vector<std::string> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (key) result.emplace_back(key);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<StateStoreDb::Entry> StateStoreDb::load_all() const {
    std::lock_guard lock(db_.mutex());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(),
        "SELECT key, value, agent_id, scope FROM state_store", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return {};

    std::vector<Entry> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Entry e;
        e.key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        e.value = nlohmann::json::parse(val ? val : "null");
        e.agent_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 2));
        e.scope = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        result.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return result;
}

size_t StateStoreDb::count() const {
    std::lock_guard lock(db_.mutex());

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.handle(),
        "SELECT COUNT(*) FROM state_store", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace clove
