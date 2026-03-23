#include <clove/memory_block_db.hpp>
#include <clove/database.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace clove {

MemoryBlockDb::MemoryBlockDb(Database& db) : db_(db) {}

bool MemoryBlockDb::store(const MemoryBlock& b) {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return false;

    const char* sql = R"SQL(
        INSERT OR REPLACE INTO memory_blocks
            (id, name, owner_agent_id, type, access, content,
             shared_with, max_tokens, created_at_ms, updated_at_ms)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    nlohmann::json shared_arr = nlohmann::json::array();
    for (auto id : b.shared_with) shared_arr.push_back(id);
    std::string shared_json = shared_arr.dump();

    sqlite3_bind_text(stmt, 1, b.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, b.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(b.owner_agent_id));
    sqlite3_bind_int(stmt, 4, static_cast<int>(b.type));
    sqlite3_bind_int(stmt, 5, static_cast<int>(b.access));
    sqlite3_bind_text(stmt, 6, b.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, shared_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, static_cast<int64_t>(b.max_tokens));
    sqlite3_bind_int64(stmt, 9, static_cast<int64_t>(b.created_at_ms));
    sqlite3_bind_int64(stmt, 10, static_cast<int64_t>(b.updated_at_ms));

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool MemoryBlockDb::erase(const std::string& id) {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return false;

    const char* sql = "DELETE FROM memory_blocks WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<MemoryBlock> MemoryBlockDb::load_all() const {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    std::vector<MemoryBlock> result;
    if (!db) return result;

    const char* sql = "SELECT * FROM memory_blocks ORDER BY created_at_ms";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryBlock b;
        b.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        b.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        b.owner_agent_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 2));
        b.type = static_cast<MemoryBlockType>(sqlite3_column_int(stmt, 3));
        b.access = static_cast<MemoryAccess>(sqlite3_column_int(stmt, 4));
        b.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

        auto shared_json = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
        for (const auto& v : shared_json) {
            b.shared_with.insert(v.get<uint32_t>());
        }

        b.max_tokens = static_cast<size_t>(sqlite3_column_int64(stmt, 7));
        b.created_at_ms = static_cast<uint64_t>(sqlite3_column_int64(stmt, 8));
        b.updated_at_ms = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9));
        result.push_back(std::move(b));
    }

    sqlite3_finalize(stmt);
    return result;
}

size_t MemoryBlockDb::count() const {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return 0;

    const char* sql = "SELECT COUNT(*) FROM memory_blocks";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    size_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return n;
}

} // namespace clove
