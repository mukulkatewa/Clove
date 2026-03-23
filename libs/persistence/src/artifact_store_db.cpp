#include <clove/artifact_store_db.hpp>
#include <clove/database.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace clove {

ArtifactStoreDb::ArtifactStoreDb(Database& db) : db_(db) {}

bool ArtifactStoreDb::store(const Artifact& a) {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return false;

    const char* sql = R"SQL(
        INSERT OR REPLACE INTO artifacts
            (id, chain_id, author_agent_id, type, state, title, content,
             parent_ids, metadata, created_at_ms, updated_at_ms)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    std::string parent_ids_json = nlohmann::json(a.parent_ids).dump();
    std::string metadata_json = a.metadata.dump();

    sqlite3_bind_text(stmt, 1, a.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, a.chain_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(a.author_agent_id));
    sqlite3_bind_int(stmt, 4, static_cast<int>(a.type));
    sqlite3_bind_int(stmt, 5, static_cast<int>(a.state));
    sqlite3_bind_text(stmt, 6, a.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, a.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, parent_ids_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, metadata_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 10, static_cast<int64_t>(a.created_at_ms));
    sqlite3_bind_int64(stmt, 11, static_cast<int64_t>(a.updated_at_ms));

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<Artifact> ArtifactStoreDb::get(const std::string& id) const {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return std::nullopt;

    const char* sql = "SELECT * FROM artifacts WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return std::nullopt;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }

    Artifact a;
    a.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    a.chain_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    a.author_agent_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 2));
    a.type = static_cast<ArtifactType>(sqlite3_column_int(stmt, 3));
    a.state = static_cast<ArtifactState>(sqlite3_column_int(stmt, 4));
    a.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    a.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    a.parent_ids = nlohmann::json::parse(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7))
    ).get<std::vector<std::string>>();
    a.metadata = nlohmann::json::parse(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
    a.created_at_ms = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9));
    a.updated_at_ms = static_cast<uint64_t>(sqlite3_column_int64(stmt, 10));

    sqlite3_finalize(stmt);
    return a;
}

bool ArtifactStoreDb::erase(const std::string& id) {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return false;

    const char* sql = "DELETE FROM artifacts WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Artifact> ArtifactStoreDb::load_all() const {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    std::vector<Artifact> result;
    if (!db) return result;

    const char* sql = "SELECT * FROM artifacts ORDER BY created_at_ms";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Artifact a;
        a.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        a.chain_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        a.author_agent_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 2));
        a.type = static_cast<ArtifactType>(sqlite3_column_int(stmt, 3));
        a.state = static_cast<ArtifactState>(sqlite3_column_int(stmt, 4));
        a.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        a.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        a.parent_ids = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7))
        ).get<std::vector<std::string>>();
        a.metadata = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        a.created_at_ms = static_cast<uint64_t>(sqlite3_column_int64(stmt, 9));
        a.updated_at_ms = static_cast<uint64_t>(sqlite3_column_int64(stmt, 10));
        result.push_back(std::move(a));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool ArtifactStoreDb::store_chain(const Chain& c) {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return false;

    const char* sql = R"SQL(
        INSERT OR REPLACE INTO chains
            (id, name, description, creator_agent_id, artifact_ids, metadata, created_at_ms)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    std::string artifact_ids_json = nlohmann::json(c.artifact_ids).dump();
    std::string metadata_json = c.metadata.dump();

    sqlite3_bind_text(stmt, 1, c.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, c.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, c.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(c.creator_agent_id));
    sqlite3_bind_text(stmt, 5, artifact_ids_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, metadata_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, static_cast<int64_t>(c.created_at_ms));

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Chain> ArtifactStoreDb::load_all_chains() const {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    std::vector<Chain> result;
    if (!db) return result;

    const char* sql = "SELECT * FROM chains ORDER BY created_at_ms";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Chain c;
        c.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        c.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        c.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        c.creator_agent_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 3));
        c.artifact_ids = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))
        ).get<std::vector<std::string>>();
        c.metadata = nlohmann::json::parse(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        c.created_at_ms = static_cast<uint64_t>(sqlite3_column_int64(stmt, 6));
        result.push_back(std::move(c));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool ArtifactStoreDb::erase_chain(const std::string& id) {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return false;

    const char* sql = "DELETE FROM chains WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

size_t ArtifactStoreDb::count() const {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return 0;

    const char* sql = "SELECT COUNT(*) FROM artifacts";
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
