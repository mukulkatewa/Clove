#include <clove/workspace_db.hpp>
#include <clove/database.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>

namespace clove {

// ── Helpers ────────────────────────────────────────────────────────────────

static std::string gen_id() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << dist(rng)
        << std::setw(16) << dist(rng);
    return oss.str();
}

static std::string col_text(sqlite3_stmt* stmt, int col) {
    const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    return v ? v : "";
}

// ── WorkspaceDb ────────────────────────────────────────────────────────────

WorkspaceDb::WorkspaceDb(Database& db) : db_(db) {}

WorkspaceRow WorkspaceDb::row_from_stmt(void* raw) const {
    auto* stmt = static_cast<sqlite3_stmt*>(raw);
    WorkspaceRow r;
    r.id         = col_text(stmt, 0);
    r.name       = col_text(stmt, 1);
    r.status     = col_text(stmt, 2);
    const char* meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    r.metadata   = nlohmann::json::parse(meta ? meta : "{}", nullptr, false);
    r.created_at = col_text(stmt, 4);
    r.updated_at = col_text(stmt, 5);
    return r;
}

bool WorkspaceDb::upsert(const WorkspaceRow& row) {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        INSERT INTO workspaces (id, name, status, metadata, updated_at)
        VALUES (?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
        ON CONFLICT(id) DO UPDATE SET
            name=excluded.name, status=excluded.status,
            metadata=excluded.metadata,
            updated_at=strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string id = row.id.empty() ? gen_id() : row.id;
    std::string meta = row.metadata.dump();
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, row.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, meta.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<WorkspaceRow> WorkspaceDb::get(const std::string& id) const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "SELECT id,name,status,metadata,created_at,updated_at FROM workspaces WHERE id=?",
            -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<WorkspaceRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = row_from_stmt(stmt);
    sqlite3_finalize(stmt);
    return result;
}

std::optional<WorkspaceRow> WorkspaceDb::get_by_name(const std::string& name) const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "SELECT id,name,status,metadata,created_at,updated_at FROM workspaces WHERE name=?",
            -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<WorkspaceRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = row_from_stmt(stmt);
    sqlite3_finalize(stmt);
    return result;
}

std::vector<WorkspaceRow> WorkspaceDb::list(const std::string& status) const {
    std::lock_guard lock(db_.mutex());
    std::string sql = "SELECT id,name,status,metadata,created_at,updated_at FROM workspaces";
    if (!status.empty()) sql += " WHERE status=?";
    sql += " ORDER BY created_at";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
    if (!status.empty()) sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<WorkspaceRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) rows.push_back(row_from_stmt(stmt));
    sqlite3_finalize(stmt);
    return rows;
}

bool WorkspaceDb::remove(const std::string& id) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "DELETE FROM workspaces WHERE id=?", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

size_t WorkspaceDb::count() const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "SELECT COUNT(*) FROM workspaces", -1, &stmt, nullptr) != SQLITE_OK) return 0;
    size_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return n;
}

// ── AgentDefDb ─────────────────────────────────────────────────────────────

AgentDefDb::AgentDefDb(Database& db) : db_(db) {}

AgentDefRow AgentDefDb::row_from_stmt(void* raw) const {
    auto* stmt = static_cast<sqlite3_stmt*>(raw);
    AgentDefRow r;
    r.name               = col_text(stmt, 0);
    r.workspace_id       = col_text(stmt, 1);
    r.description        = col_text(stmt, 2);
    r.enabled            = sqlite3_column_int(stmt, 3) != 0;
    r.goal               = col_text(stmt, 4);
    const char* trg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    r.triggers           = nlohmann::json::parse(trg ? trg : "[]", nullptr, false);
    const char* tls = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    r.tools              = nlohmann::json::parse(tls ? tls : "[]", nullptr, false);
    const char* con = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    r.connections        = nlohmann::json::parse(con ? con : "[]", nullptr, false);
    const char* perm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    r.permissions        = nlohmann::json::parse(perm ? perm : "{}", nullptr, false);
    r.budget_per_run     = sqlite3_column_double(stmt, 9);
    r.budget_daily_max   = sqlite3_column_double(stmt, 10);
    r.budget_daily_spent = sqlite3_column_double(stmt, 11);
    r.budget_last_reset  = col_text(stmt, 12);
    r.model              = col_text(stmt, 13);
    r.max_steps          = sqlite3_column_int(stmt, 14);
    r.created_at         = col_text(stmt, 15);
    r.updated_at         = col_text(stmt, 16);
    return r;
}

bool AgentDefDb::upsert(const AgentDefRow& row) {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        INSERT INTO agent_definitions
            (name,workspace_id,description,enabled,goal,triggers,tools,connections,
             permissions,budget_per_run,budget_daily_max,budget_daily_spent,
             budget_last_reset,model,max_steps,updated_at)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,strftime('%Y-%m-%dT%H:%M:%fZ','now'))
        ON CONFLICT(name) DO UPDATE SET
            workspace_id=excluded.workspace_id,
            description=excluded.description, enabled=excluded.enabled,
            goal=excluded.goal, triggers=excluded.triggers, tools=excluded.tools,
            connections=excluded.connections, permissions=excluded.permissions,
            budget_per_run=excluded.budget_per_run,
            budget_daily_max=excluded.budget_daily_max,
            budget_daily_spent=excluded.budget_daily_spent,
            budget_last_reset=excluded.budget_last_reset,
            model=excluded.model, max_steps=excluded.max_steps,
            updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now')
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string trg  = row.triggers.dump();
    std::string tls  = row.tools.dump();
    std::string con  = row.connections.dump();
    std::string perm = row.permissions.dump();

    sqlite3_bind_text(stmt,  1, row.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  2, row.workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  3, row.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt,  4, row.enabled ? 1 : 0);
    sqlite3_bind_text(stmt,  5, row.goal.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  6, trg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  7, tls.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  8, con.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  9, perm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt,10, row.budget_per_run);
    sqlite3_bind_double(stmt,11, row.budget_daily_max);
    sqlite3_bind_double(stmt,12, row.budget_daily_spent);
    sqlite3_bind_text(stmt, 13, row.budget_last_reset.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, row.model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 15, row.max_steps);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<AgentDefRow> AgentDefDb::get(const std::string& name) const {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        SELECT name,workspace_id,description,enabled,goal,triggers,tools,connections,
               permissions,budget_per_run,budget_daily_max,budget_daily_spent,
               budget_last_reset,model,max_steps,created_at,updated_at
        FROM agent_definitions WHERE name=?
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<AgentDefRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = row_from_stmt(stmt);
    sqlite3_finalize(stmt);
    return result;
}

std::vector<AgentDefRow> AgentDefDb::list(const std::string& workspace_id) const {
    std::lock_guard lock(db_.mutex());
    std::string sql = R"(
        SELECT name,workspace_id,description,enabled,goal,triggers,tools,connections,
               permissions,budget_per_run,budget_daily_max,budget_daily_spent,
               budget_last_reset,model,max_steps,created_at,updated_at
        FROM agent_definitions
    )";
    if (!workspace_id.empty()) sql += " WHERE workspace_id=?";
    sql += " ORDER BY name";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
    if (!workspace_id.empty()) sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<AgentDefRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) rows.push_back(row_from_stmt(stmt));
    sqlite3_finalize(stmt);
    return rows;
}

bool AgentDefDb::remove(const std::string& name) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "DELETE FROM agent_definitions WHERE name=?", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AgentDefDb::set_enabled(const std::string& name, bool enabled) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "UPDATE agent_definitions SET enabled=?,updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE name=?",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int (stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AgentDefDb::update_budget_spent(const std::string& name, double spent) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "UPDATE agent_definitions SET budget_daily_spent=budget_daily_spent+?,updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE name=?",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_double(stmt, 1, spent);
    sqlite3_bind_text  (stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

size_t AgentDefDb::count() const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "SELECT COUNT(*) FROM agent_definitions", -1, &stmt, nullptr) != SQLITE_OK) return 0;
    size_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return n;
}

// ── WorkspaceDataDb ────────────────────────────────────────────────────────

WorkspaceDataDb::WorkspaceDataDb(Database& db) : db_(db) {}

bool WorkspaceDataDb::upsert(const WorkspaceDataRow& row) {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        INSERT INTO workspace_data (id,workspace_id,key,content_type,content,file_path)
        VALUES (?,?,?,?,?,?)
        ON CONFLICT(workspace_id,key) DO UPDATE SET
            content_type=excluded.content_type,
            content=excluded.content,
            file_path=excluded.file_path
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string id = row.id.empty() ? gen_id() : row.id;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, row.workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, row.key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, row.content_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, row.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, row.file_path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<WorkspaceDataRow> WorkspaceDataDb::get(const std::string& workspace_id,
                                                      const std::string& key) const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "SELECT id,workspace_id,key,content_type,content,file_path,created_at FROM workspace_data WHERE workspace_id=? AND key=?",
            -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<WorkspaceDataRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        WorkspaceDataRow r;
        r.id           = col_text(stmt, 0);
        r.workspace_id = col_text(stmt, 1);
        r.key          = col_text(stmt, 2);
        r.content_type = col_text(stmt, 3);
        r.content      = col_text(stmt, 4);
        r.file_path    = col_text(stmt, 5);
        r.created_at   = col_text(stmt, 6);
        result = r;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<WorkspaceDataRow> WorkspaceDataDb::list(const std::string& workspace_id) const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "SELECT id,workspace_id,key,content_type,content,file_path,created_at FROM workspace_data WHERE workspace_id=? ORDER BY key",
            -1, &stmt, nullptr) != SQLITE_OK) return {};
    sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<WorkspaceDataRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WorkspaceDataRow r;
        r.id           = col_text(stmt, 0);
        r.workspace_id = col_text(stmt, 1);
        r.key          = col_text(stmt, 2);
        r.content_type = col_text(stmt, 3);
        r.content      = col_text(stmt, 4);
        r.file_path    = col_text(stmt, 5);
        r.created_at   = col_text(stmt, 6);
        rows.push_back(r);
    }
    sqlite3_finalize(stmt);
    return rows;
}

bool WorkspaceDataDb::remove(const std::string& workspace_id, const std::string& key) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "DELETE FROM workspace_data WHERE workspace_id=? AND key=?",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool WorkspaceDataDb::remove_workspace(const std::string& workspace_id) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "DELETE FROM workspace_data WHERE workspace_id=?",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// ── WorkspaceOutputDb ──────────────────────────────────────────────────────

WorkspaceOutputDb::WorkspaceOutputDb(Database& db) : db_(db) {}

bool WorkspaceOutputDb::insert(const WorkspaceOutputRow& row) {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        INSERT INTO workspace_outputs (id,workspace_id,agent_name,run_id,type,title,content,cost_usd)
        VALUES (?,?,?,?,?,?,?,?)
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string id = row.id.empty() ? gen_id() : row.id;
    sqlite3_bind_text  (stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 2, row.workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 3, row.agent_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 4, row.run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 5, row.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 6, row.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 7, row.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, row.cost_usd);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<WorkspaceOutputRow> WorkspaceOutputDb::list(const std::string& workspace_id,
                                                          int limit) const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id,workspace_id,agent_name,run_id,type,title,content,cost_usd,created_at
        FROM workspace_outputs WHERE workspace_id=?
        ORDER BY created_at DESC LIMIT ?
    )";
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return {};
    sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, limit);
    std::vector<WorkspaceOutputRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WorkspaceOutputRow r;
        r.id           = col_text(stmt, 0);
        r.workspace_id = col_text(stmt, 1);
        r.agent_name   = col_text(stmt, 2);
        r.run_id       = col_text(stmt, 3);
        r.type         = col_text(stmt, 4);
        r.title        = col_text(stmt, 5);
        r.content      = col_text(stmt, 6);
        r.cost_usd     = sqlite3_column_double(stmt, 7);
        r.created_at   = col_text(stmt, 8);
        rows.push_back(r);
    }
    sqlite3_finalize(stmt);
    return rows;
}

std::vector<WorkspaceOutputRow> WorkspaceOutputDb::list_by_run(const std::string& run_id) const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT id,workspace_id,agent_name,run_id,type,title,content,cost_usd,created_at
        FROM workspace_outputs WHERE run_id=? ORDER BY created_at
    )";
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return {};
    sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<WorkspaceOutputRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WorkspaceOutputRow r;
        r.id           = col_text(stmt, 0);
        r.workspace_id = col_text(stmt, 1);
        r.agent_name   = col_text(stmt, 2);
        r.run_id       = col_text(stmt, 3);
        r.type         = col_text(stmt, 4);
        r.title        = col_text(stmt, 5);
        r.content      = col_text(stmt, 6);
        r.cost_usd     = sqlite3_column_double(stmt, 7);
        r.created_at   = col_text(stmt, 8);
        rows.push_back(r);
    }
    sqlite3_finalize(stmt);
    return rows;
}

bool WorkspaceOutputDb::remove_workspace(const std::string& workspace_id) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "DELETE FROM workspace_outputs WHERE workspace_id=?",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// ── AgentRunDb ─────────────────────────────────────────────────────────────

AgentRunDb::AgentRunDb(Database& db) : db_(db) {}

AgentRunRow AgentRunDb::row_from_stmt(void* raw) const {
    auto* stmt = static_cast<sqlite3_stmt*>(raw);
    AgentRunRow r;
    r.id           = col_text(stmt, 0);
    r.agent_name   = col_text(stmt, 1);
    r.workspace_id = col_text(stmt, 2);
    r.goal         = col_text(stmt, 3);
    r.status       = col_text(stmt, 4);
    r.result       = col_text(stmt, 5);
    r.steps        = sqlite3_column_int(stmt, 6);
    r.cost_usd     = sqlite3_column_double(stmt, 7);
    r.model        = col_text(stmt, 8);
    r.started_at   = col_text(stmt, 9);
    r.completed_at = col_text(stmt, 10);
    return r;
}

bool AgentRunDb::insert(const AgentRunRow& row) {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        INSERT OR IGNORE INTO agent_runs
            (id,agent_name,workspace_id,goal,status,result,steps,cost_usd,model)
        VALUES (?,?,?,?,?,?,?,?,?)
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string id = row.id.empty() ? gen_id() : row.id;
    sqlite3_bind_text  (stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 2, row.agent_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 3, row.workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 4, row.goal.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 5, row.status.empty() ? "running" : row.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 6, row.result.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (stmt, 7, row.steps);
    sqlite3_bind_double(stmt, 8, row.cost_usd);
    sqlite3_bind_text  (stmt, 9, row.model.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AgentRunDb::complete(const std::string& id, const std::string& status,
                           const std::string& result, int steps, double cost_usd) {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        UPDATE agent_runs SET
            status=?, result=?, steps=?, cost_usd=?,
            completed_at=strftime('%Y-%m-%dT%H:%M:%fZ','now')
        WHERE id=?
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text  (stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 2, result.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (stmt, 3, steps);
    sqlite3_bind_double(stmt, 4, cost_usd);
    sqlite3_bind_text  (stmt, 5, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<AgentRunRow> AgentRunDb::get(const std::string& id) const {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        SELECT id,agent_name,workspace_id,goal,status,result,steps,cost_usd,model,started_at,completed_at
        FROM agent_runs WHERE id=?
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<AgentRunRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = row_from_stmt(stmt);
    sqlite3_finalize(stmt);
    return result;
}

std::vector<AgentRunRow> AgentRunDb::list(const std::string& workspace_id, int limit) const {
    std::lock_guard lock(db_.mutex());
    std::string sql = R"(
        SELECT id,agent_name,workspace_id,goal,status,result,steps,cost_usd,model,started_at,completed_at
        FROM agent_runs
    )";
    if (!workspace_id.empty()) sql += " WHERE workspace_id=?";
    sql += " ORDER BY started_at DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
    int param = 1;
    if (!workspace_id.empty()) sqlite3_bind_text(stmt, param++, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, param, limit);
    std::vector<AgentRunRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) rows.push_back(row_from_stmt(stmt));
    sqlite3_finalize(stmt);
    return rows;
}

std::vector<AgentRunRow> AgentRunDb::list_by_agent(const std::string& agent_name, int limit) const {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        SELECT id,agent_name,workspace_id,goal,status,result,steps,cost_usd,model,started_at,completed_at
        FROM agent_runs WHERE agent_name=? ORDER BY started_at DESC LIMIT ?
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return {};
    sqlite3_bind_text(stmt, 1, agent_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, limit);
    std::vector<AgentRunRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) rows.push_back(row_from_stmt(stmt));
    sqlite3_finalize(stmt);
    return rows;
}

size_t AgentRunDb::count() const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "SELECT COUNT(*) FROM agent_runs", -1, &stmt, nullptr) != SQLITE_OK) return 0;
    size_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return n;
}

// ── SwarmDb ────────────────────────────────────────────────────────────────

SwarmDb::SwarmDb(Database& db) : db_(db) {}

SwarmRow SwarmDb::row_from_stmt(void* raw) const {
    auto* stmt = static_cast<sqlite3_stmt*>(raw);
    SwarmRow r;
    r.name         = col_text(stmt, 0);
    r.workspace_id = col_text(stmt, 1);
    const char* ag = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    r.agents       = nlohmann::json::parse(ag ? ag : "[]", nullptr, false);
    r.goal         = col_text(stmt, 3);
    r.budget       = sqlite3_column_double(stmt, 4);
    r.status       = col_text(stmt, 5);
    r.created_at   = col_text(stmt, 6);
    return r;
}

bool SwarmDb::upsert(const SwarmRow& row) {
    std::lock_guard lock(db_.mutex());
    const char* sql = R"(
        INSERT INTO swarms (name,workspace_id,agents,goal,budget,status)
        VALUES (?,?,?,?,?,?)
        ON CONFLICT(name) DO UPDATE SET
            workspace_id=excluded.workspace_id, agents=excluded.agents,
            goal=excluded.goal, budget=excluded.budget, status=excluded.status
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    std::string ag = row.agents.dump();
    sqlite3_bind_text  (stmt, 1, row.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 2, row.workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 3, ag.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, 4, row.goal.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, row.budget);
    sqlite3_bind_text  (stmt, 6, row.status.empty() ? "idle" : row.status.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::optional<SwarmRow> SwarmDb::get(const std::string& name) const {
    std::lock_guard lock(db_.mutex());
    const char* sql =
        "SELECT name,workspace_id,agents,goal,budget,status,created_at FROM swarms WHERE name=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<SwarmRow> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = row_from_stmt(stmt);
    sqlite3_finalize(stmt);
    return result;
}

std::vector<SwarmRow> SwarmDb::list(const std::string& workspace_id) const {
    std::lock_guard lock(db_.mutex());
    std::string sql = "SELECT name,workspace_id,agents,goal,budget,status,created_at FROM swarms";
    if (!workspace_id.empty()) sql += " WHERE workspace_id=?";
    sql += " ORDER BY created_at";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(), sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
    if (!workspace_id.empty()) sqlite3_bind_text(stmt, 1, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<SwarmRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) rows.push_back(row_from_stmt(stmt));
    sqlite3_finalize(stmt);
    return rows;
}

bool SwarmDb::remove(const std::string& name) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "DELETE FROM swarms WHERE name=?", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SwarmDb::set_status(const std::string& name, const std::string& status) {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "UPDATE swarms SET status=? WHERE name=?", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

size_t SwarmDb::count() const {
    std::lock_guard lock(db_.mutex());
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_.handle(),
            "SELECT COUNT(*) FROM swarms", -1, &stmt, nullptr) != SQLITE_OK) return 0;
    size_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return n;
}

} // namespace clove
