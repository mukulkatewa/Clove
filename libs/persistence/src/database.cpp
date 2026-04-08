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

        -- Workspaces (persistent world/team environments)
        CREATE TABLE IF NOT EXISTS workspaces (
            id          TEXT    PRIMARY KEY,
            name        TEXT    NOT NULL UNIQUE,
            status      TEXT    NOT NULL DEFAULT 'active',
            metadata    TEXT    NOT NULL DEFAULT '{}',
            created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
            updated_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
        );

        CREATE INDEX IF NOT EXISTS idx_workspace_name ON workspaces(name);
        CREATE INDEX IF NOT EXISTS idx_workspace_status ON workspaces(status);

        -- Agent definitions (persistent agent configurations)
        CREATE TABLE IF NOT EXISTS agent_definitions (
            name            TEXT    PRIMARY KEY,
            workspace_id    TEXT    REFERENCES workspaces(id) ON DELETE SET NULL,
            description     TEXT    NOT NULL DEFAULT '',
            enabled         INTEGER NOT NULL DEFAULT 1,
            goal            TEXT    NOT NULL DEFAULT '',
            triggers        TEXT    NOT NULL DEFAULT '[]',
            tools           TEXT    NOT NULL DEFAULT '[]',
            connections     TEXT    NOT NULL DEFAULT '[]',
            permissions     TEXT    NOT NULL DEFAULT '{}',
            budget_per_run  REAL    NOT NULL DEFAULT 1.0,
            budget_daily_max REAL   NOT NULL DEFAULT 10.0,
            budget_daily_spent REAL NOT NULL DEFAULT 0.0,
            budget_last_reset TEXT,
            model           TEXT    NOT NULL DEFAULT '',
            max_steps       INTEGER NOT NULL DEFAULT 20,
            created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
            updated_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
        );

        CREATE INDEX IF NOT EXISTS idx_agentdef_workspace ON agent_definitions(workspace_id);
        CREATE INDEX IF NOT EXISTS idx_agentdef_enabled ON agent_definitions(enabled);

        -- Workspace-scoped data (inputs, context files, shared KV)
        CREATE TABLE IF NOT EXISTS workspace_data (
            id              TEXT    PRIMARY KEY,
            workspace_id    TEXT    NOT NULL REFERENCES workspaces(id) ON DELETE CASCADE,
            key             TEXT    NOT NULL,
            content_type    TEXT    NOT NULL DEFAULT 'text/plain',
            content         TEXT    NOT NULL DEFAULT '',
            file_path       TEXT    NOT NULL DEFAULT '',
            created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
            UNIQUE(workspace_id, key)
        );

        CREATE INDEX IF NOT EXISTS idx_wsdata_workspace ON workspace_data(workspace_id);

        -- Workspace outputs (agent results scoped to a workspace)
        CREATE TABLE IF NOT EXISTS workspace_outputs (
            id              TEXT    PRIMARY KEY,
            workspace_id    TEXT    NOT NULL REFERENCES workspaces(id) ON DELETE CASCADE,
            agent_name      TEXT    NOT NULL DEFAULT '',
            run_id          TEXT    NOT NULL DEFAULT '',
            type            TEXT    NOT NULL DEFAULT 'text',
            title           TEXT    NOT NULL DEFAULT '',
            content         TEXT    NOT NULL DEFAULT '',
            cost_usd        REAL    NOT NULL DEFAULT 0.0,
            created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
        );

        CREATE INDEX IF NOT EXISTS idx_wsoutput_workspace ON workspace_outputs(workspace_id);
        CREATE INDEX IF NOT EXISTS idx_wsoutput_run ON workspace_outputs(run_id);

        -- Agent run history
        CREATE TABLE IF NOT EXISTS agent_runs (
            id              TEXT    PRIMARY KEY,
            agent_name      TEXT    NOT NULL DEFAULT '',
            workspace_id    TEXT    REFERENCES workspaces(id) ON DELETE SET NULL,
            goal            TEXT    NOT NULL DEFAULT '',
            status          TEXT    NOT NULL DEFAULT 'running',
            result          TEXT    NOT NULL DEFAULT '',
            steps           INTEGER NOT NULL DEFAULT 0,
            cost_usd        REAL    NOT NULL DEFAULT 0.0,
            model           TEXT    NOT NULL DEFAULT '',
            started_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
            completed_at    TEXT
        );

        CREATE INDEX IF NOT EXISTS idx_agentrun_workspace ON agent_runs(workspace_id);
        CREATE INDEX IF NOT EXISTS idx_agentrun_agent ON agent_runs(agent_name);
        CREATE INDEX IF NOT EXISTS idx_agentrun_status ON agent_runs(status);

        -- Swarm definitions
        CREATE TABLE IF NOT EXISTS swarms (
            name            TEXT    PRIMARY KEY,
            workspace_id    TEXT    REFERENCES workspaces(id) ON DELETE SET NULL,
            agents          TEXT    NOT NULL DEFAULT '[]',
            goal            TEXT    NOT NULL DEFAULT '',
            budget          REAL    NOT NULL DEFAULT 2.0,
            status          TEXT    NOT NULL DEFAULT 'idle',
            created_at      TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
        );

        CREATE INDEX IF NOT EXISTS idx_swarm_workspace ON swarms(workspace_id);
        CREATE INDEX IF NOT EXISTS idx_swarm_status ON swarms(status);

        INSERT OR IGNORE INTO schema_version (version) VALUES (2);

        -- Three-tier agent memory (ENGRAM-inspired: episodic/semantic/procedural)
        -- Scored retrieval: 0.40*recency_decay + 0.35*importance + 0.25*keyword_overlap
        CREATE TABLE IF NOT EXISTS agent_memory (
            id              TEXT    PRIMARY KEY,
            agent_name      TEXT    NOT NULL,
            workspace_id    TEXT    NOT NULL DEFAULT '',
            tier            INTEGER NOT NULL DEFAULT 0,  -- 0=EPISODIC 1=SEMANTIC 2=PROCEDURAL
            content         TEXT    NOT NULL DEFAULT '',
            importance      REAL    NOT NULL DEFAULT 5.0, -- 1.0–10.0, scored at write time
            recency_step    INTEGER NOT NULL DEFAULT 0,   -- step index when written
            causal_parent   TEXT    NOT NULL DEFAULT '',  -- id of causal predecessor
            source_run_id   TEXT    NOT NULL DEFAULT '',
            created_at_ms   INTEGER NOT NULL DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_agentmem_agent    ON agent_memory(agent_name);
        CREATE INDEX IF NOT EXISTS idx_agentmem_tier     ON agent_memory(tier);
        CREATE INDEX IF NOT EXISTS idx_agentmem_run      ON agent_memory(source_run_id);
        CREATE INDEX IF NOT EXISTS idx_agentmem_created  ON agent_memory(created_at_ms DESC);

        INSERT OR IGNORE INTO schema_version (version) VALUES (3);
    )SQL";

    return exec(schema);
}

} // namespace clove
