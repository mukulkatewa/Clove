#include <clove/database.hpp>
#include <sqlite3.h>

namespace clove {

Database::Database(const std::string& path) : path_(path) {}

Database::~Database() {
    close();
}

bool Database::open() {
    std::lock_guard lock(mutex_);
    if (db_) return true;

    int rc = sqlite3_open(path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }

    // WAL mode for better concurrent read performance
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA foreign_keys=ON");

    return create_schema();
}

void Database::close() {
    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::exec(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (err_msg) sqlite3_free(err_msg);
    return rc == SQLITE_OK;
}

bool Database::migrate() {
    return create_schema();
}

bool Database::create_schema() {
    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS audit_log (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
            category    TEXT    NOT NULL,
            event_type  TEXT    NOT NULL,
            agent_id    INTEGER NOT NULL,
            agent_name  TEXT    NOT NULL DEFAULT '',
            details     TEXT    NOT NULL DEFAULT '{}',
            success     INTEGER NOT NULL DEFAULT 1
        );

        CREATE INDEX IF NOT EXISTS idx_audit_category ON audit_log(category);
        CREATE INDEX IF NOT EXISTS idx_audit_agent_id ON audit_log(agent_id);
        CREATE INDEX IF NOT EXISTS idx_audit_timestamp ON audit_log(timestamp);

        CREATE TABLE IF NOT EXISTS state_store (
            key         TEXT    PRIMARY KEY,
            value       TEXT    NOT NULL,
            agent_id    INTEGER NOT NULL,
            scope       TEXT    NOT NULL DEFAULT 'global',
            updated_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
        );

        CREATE INDEX IF NOT EXISTS idx_state_agent ON state_store(agent_id);
        CREATE INDEX IF NOT EXISTS idx_state_scope ON state_store(scope);

        CREATE TABLE IF NOT EXISTS schema_version (
            version     INTEGER PRIMARY KEY,
            applied_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
        );

        INSERT OR IGNORE INTO schema_version (version) VALUES (1);

        -- Context layer: artifacts and chains
        CREATE TABLE IF NOT EXISTS artifacts (
            id              TEXT PRIMARY KEY,
            chain_id        TEXT    NOT NULL,
            author_agent_id INTEGER NOT NULL,
            type            INTEGER NOT NULL,
            state           INTEGER NOT NULL DEFAULT 0,
            title           TEXT    NOT NULL,
            content         TEXT    NOT NULL DEFAULT '',
            parent_ids      TEXT    NOT NULL DEFAULT '[]',
            metadata        TEXT    NOT NULL DEFAULT '{}',
            created_at_ms   INTEGER NOT NULL,
            updated_at_ms   INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_artifact_chain ON artifacts(chain_id);
        CREATE INDEX IF NOT EXISTS idx_artifact_author ON artifacts(author_agent_id);
        CREATE INDEX IF NOT EXISTS idx_artifact_state ON artifacts(state);
        CREATE INDEX IF NOT EXISTS idx_artifact_type ON artifacts(type);

        CREATE TABLE IF NOT EXISTS chains (
            id                TEXT PRIMARY KEY,
            name              TEXT    NOT NULL,
            description       TEXT    NOT NULL DEFAULT '',
            creator_agent_id  INTEGER NOT NULL,
            artifact_ids      TEXT    NOT NULL DEFAULT '[]',
            metadata          TEXT    NOT NULL DEFAULT '{}',
            created_at_ms     INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_chain_creator ON chains(creator_agent_id);

        -- Memory blocks
        CREATE TABLE IF NOT EXISTS memory_blocks (
            id              TEXT PRIMARY KEY,
            name            TEXT    NOT NULL,
            owner_agent_id  INTEGER NOT NULL,
            type            INTEGER NOT NULL DEFAULT 1,
            access          INTEGER NOT NULL DEFAULT 0,
            content         TEXT    NOT NULL DEFAULT '',
            shared_with     TEXT    NOT NULL DEFAULT '[]',
            max_tokens      INTEGER NOT NULL DEFAULT 0,
            created_at_ms   INTEGER NOT NULL,
            updated_at_ms   INTEGER NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_memblock_owner ON memory_blocks(owner_agent_id);
        CREATE INDEX IF NOT EXISTS idx_memblock_type ON memory_blocks(type);
    )SQL";

    return exec(schema);
}

} // namespace clove
