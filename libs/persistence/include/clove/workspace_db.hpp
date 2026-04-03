#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <vector>

namespace clove {

class Database;

// ── Workspace ──────────────────────────────────────────────────────────────
struct WorkspaceRow {
    std::string id;
    std::string name;
    std::string status;        // "active" | "archived"
    nlohmann::json metadata;
    std::string created_at;
    std::string updated_at;
};

class WorkspaceDb {
public:
    explicit WorkspaceDb(Database& db);

    bool upsert(const WorkspaceRow& row);
    std::optional<WorkspaceRow> get(const std::string& id) const;
    std::optional<WorkspaceRow> get_by_name(const std::string& name) const;
    std::vector<WorkspaceRow> list(const std::string& status = "") const;
    bool remove(const std::string& id);
    size_t count() const;

private:
    Database& db_;
    WorkspaceRow row_from_stmt(void* stmt) const;
};

// ── AgentDefinition ────────────────────────────────────────────────────────
struct AgentDefRow {
    std::string name;
    std::string workspace_id;   // empty = global
    std::string description;
    bool enabled = true;
    std::string goal;
    nlohmann::json triggers;    // array
    nlohmann::json tools;       // array
    nlohmann::json connections; // array
    nlohmann::json permissions; // object
    double budget_per_run = 1.0;
    double budget_daily_max = 10.0;
    double budget_daily_spent = 0.0;
    std::string budget_last_reset;
    std::string model;
    int max_steps = 20;
    std::string created_at;
    std::string updated_at;
};

class AgentDefDb {
public:
    explicit AgentDefDb(Database& db);

    bool upsert(const AgentDefRow& row);
    std::optional<AgentDefRow> get(const std::string& name) const;
    std::vector<AgentDefRow> list(const std::string& workspace_id = "") const;
    bool remove(const std::string& name);
    bool set_enabled(const std::string& name, bool enabled);
    bool update_budget_spent(const std::string& name, double spent);
    size_t count() const;

private:
    Database& db_;
    AgentDefRow row_from_stmt(void* stmt) const;
};

// ── WorkspaceData ──────────────────────────────────────────────────────────
struct WorkspaceDataRow {
    std::string id;
    std::string workspace_id;
    std::string key;
    std::string content_type;
    std::string content;
    std::string file_path;
    std::string created_at;
};

class WorkspaceDataDb {
public:
    explicit WorkspaceDataDb(Database& db);

    bool upsert(const WorkspaceDataRow& row);
    std::optional<WorkspaceDataRow> get(const std::string& workspace_id,
                                        const std::string& key) const;
    std::vector<WorkspaceDataRow> list(const std::string& workspace_id) const;
    bool remove(const std::string& workspace_id, const std::string& key);
    bool remove_workspace(const std::string& workspace_id);

private:
    Database& db_;
};

// ── WorkspaceOutput ────────────────────────────────────────────────────────
struct WorkspaceOutputRow {
    std::string id;
    std::string workspace_id;
    std::string agent_name;
    std::string run_id;
    std::string type;    // "text" | "json" | "file"
    std::string title;
    std::string content;
    double cost_usd = 0.0;
    std::string created_at;
};

class WorkspaceOutputDb {
public:
    explicit WorkspaceOutputDb(Database& db);

    bool insert(const WorkspaceOutputRow& row);
    std::vector<WorkspaceOutputRow> list(const std::string& workspace_id,
                                         int limit = 50) const;
    std::vector<WorkspaceOutputRow> list_by_run(const std::string& run_id) const;
    bool remove_workspace(const std::string& workspace_id);

private:
    Database& db_;
};

// ── AgentRun ───────────────────────────────────────────────────────────────
struct AgentRunRow {
    std::string id;
    std::string agent_name;
    std::string workspace_id;  // empty = global
    std::string goal;
    std::string status;        // "running" | "completed" | "failed"
    std::string result;
    int steps = 0;
    double cost_usd = 0.0;
    std::string model;
    std::string started_at;
    std::string completed_at;  // empty if still running
};

class AgentRunDb {
public:
    explicit AgentRunDb(Database& db);

    bool insert(const AgentRunRow& row);
    bool complete(const std::string& id, const std::string& status,
                  const std::string& result, int steps, double cost_usd);
    std::optional<AgentRunRow> get(const std::string& id) const;
    std::vector<AgentRunRow> list(const std::string& workspace_id = "",
                                   int limit = 50) const;
    std::vector<AgentRunRow> list_by_agent(const std::string& agent_name,
                                            int limit = 20) const;
    size_t count() const;

private:
    Database& db_;
    AgentRunRow row_from_stmt(void* stmt) const;
};

// ── Swarm ──────────────────────────────────────────────────────────────────
struct SwarmRow {
    std::string name;
    std::string workspace_id;  // empty = global
    nlohmann::json agents;     // array of agent names
    std::string goal;
    double budget = 2.0;
    std::string status;        // "idle" | "running" | "completed"
    std::string created_at;
};

class SwarmDb {
public:
    explicit SwarmDb(Database& db);

    bool upsert(const SwarmRow& row);
    std::optional<SwarmRow> get(const std::string& name) const;
    std::vector<SwarmRow> list(const std::string& workspace_id = "") const;
    bool remove(const std::string& name);
    bool set_status(const std::string& name, const std::string& status);
    size_t count() const;

private:
    Database& db_;
    SwarmRow row_from_stmt(void* stmt) const;
};

} // namespace clove
