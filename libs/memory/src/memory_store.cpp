#include <clove/memory_store.hpp>
#include <clove/database.hpp>
#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>
#include <set>
#include <sstream>
#include <functional>

namespace clove {

MemoryStore::MemoryStore(Database& db) : db_(db) {}

// ── Write ─────────────────────────────────────────────────────────────────────

bool MemoryStore::write(const MemoryEntry& e) {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return false;

    const char* sql = R"SQL(
        INSERT OR REPLACE INTO agent_memory
            (id, agent_name, workspace_id, tier, content, importance,
             recency_step, causal_parent, source_run_id, created_at_ms)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1,  e.id.c_str(),            -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2,  e.agent_name.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3,  e.workspace_id.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 4,  static_cast<int>(e.tier));
    sqlite3_bind_text(stmt, 5,  e.content.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt,6, static_cast<double>(e.importance));
    sqlite3_bind_int (stmt, 7,  e.recency_step);
    sqlite3_bind_text(stmt, 8,  e.causal_parent.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9,  e.source_run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,10, e.created_at_ms);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

// ── Internal loader ───────────────────────────────────────────────────────────

static std::vector<MemoryEntry> load_stmt(sqlite3_stmt* stmt) {
    std::vector<MemoryEntry> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MemoryEntry e;
        auto col_str = [&](int c) -> std::string {
            auto p = sqlite3_column_text(stmt, c);
            return p ? reinterpret_cast<const char*>(p) : "";
        };
        e.id            = col_str(0);
        e.agent_name    = col_str(1);
        e.workspace_id  = col_str(2);
        e.tier          = static_cast<MemoryTier>(sqlite3_column_int(stmt, 3));
        e.content       = col_str(4);
        e.importance    = static_cast<float>(sqlite3_column_double(stmt, 5));
        e.recency_step  = sqlite3_column_int(stmt, 6);
        e.causal_parent = col_str(7);
        e.source_run_id = col_str(8);
        e.created_at_ms = sqlite3_column_int64(stmt, 9);
        result.push_back(std::move(e));
    }
    return result;
}

// ── Retrieve ──────────────────────────────────────────────────────────────────

std::vector<MemoryEntry> MemoryStore::retrieve(
    const std::string& agent_name,
    const std::string& workspace_id,
    const std::string& query,
    int current_step,
    size_t token_budget,
    std::optional<MemoryTier> tier_filter) const
{
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return {};

    // Private memories: all tiers for this agent
    // Shared memories: SEMANTIC (tier=1) + PROCEDURAL (tier=2) from any agent
    //                  in the same workspace (workspace_id must be non-empty)
    std::string sql;
    if (!workspace_id.empty()) {
        sql = "SELECT * FROM agent_memory WHERE "
              "(agent_name = ?)";
        if (tier_filter)
            sql += " AND tier = " + std::to_string(static_cast<int>(*tier_filter));
        sql += " UNION SELECT * FROM agent_memory WHERE "
               "(workspace_id = ? AND workspace_id != '' AND agent_name != ? AND tier >= 1)";
        if (tier_filter)
            sql += " AND tier = " + std::to_string(static_cast<int>(*tier_filter));
        sql += " ORDER BY created_at_ms DESC LIMIT 500";
    } else {
        sql = "SELECT * FROM agent_memory WHERE agent_name = ?";
        if (tier_filter)
            sql += " AND tier = " + std::to_string(static_cast<int>(*tier_filter));
        sql += " ORDER BY created_at_ms DESC LIMIT 500";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
    sqlite3_bind_text(stmt, 1, agent_name.c_str(), -1, SQLITE_TRANSIENT);
    if (!workspace_id.empty()) {
        sqlite3_bind_text(stmt, 2, workspace_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, agent_name.c_str(),   -1, SQLITE_TRANSIENT);
    }
    auto candidates = load_stmt(stmt);
    sqlite3_finalize(stmt);

    // Score all candidates
    for (auto& e : candidates)
        e.score = score(e, query, current_step);

    // Sort by score descending
    std::sort(candidates.begin(), candidates.end(),
        [](const MemoryEntry& a, const MemoryEntry& b) { return a.score > b.score; });

    // Bucket by tier for position-aware injection:
    // PROCEDURAL first (anchors context), SEMANTIC middle, EPISODIC last
    // This respects "Lost in the Middle" (Liu et al. 2023) — critical info at boundaries
    std::vector<MemoryEntry> proc, sem, epis;
    for (auto& e : candidates) {
        switch (e.tier) {
            case MemoryTier::PROCEDURAL: proc.push_back(e); break;
            case MemoryTier::SEMANTIC:   sem.push_back(e);  break;
            case MemoryTier::EPISODIC:   epis.push_back(e); break;
        }
    }

    std::vector<MemoryEntry> result;
    size_t tokens_used = 0;
    auto add = [&](const MemoryEntry& e) {
        size_t t = estimate_tokens(e.content);
        if (tokens_used + t <= token_budget) {
            result.push_back(e);
            tokens_used += t;
        }
    };

    for (const auto& e : proc) add(e);
    for (const auto& e : sem)  add(e);
    for (const auto& e : epis) add(e);
    return result;
}

// ── List tier ─────────────────────────────────────────────────────────────────

std::vector<MemoryEntry> MemoryStore::list_tier(
    const std::string& agent_name, MemoryTier tier, int limit) const
{
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return {};

    const char* sql =
        "SELECT * FROM agent_memory WHERE agent_name = ? AND tier = ? "
        "ORDER BY created_at_ms DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};
    sqlite3_bind_text(stmt, 1, agent_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, static_cast<int>(tier));
    sqlite3_bind_int (stmt, 3, limit);
    auto result = load_stmt(stmt);
    sqlite3_finalize(stmt);
    return result;
}

// ── Consolidate: EPISODIC → SEMANTIC ─────────────────────────────────────────

int MemoryStore::consolidate(const std::string& agent_name, const std::string& run_id) {
    // Load all EPISODIC entries for this run
    std::vector<MemoryEntry> episodic;
    {
        std::lock_guard lock(db_.mutex());
        sqlite3* db = db_.handle();
        if (!db) return 0;

        const char* sql =
            "SELECT * FROM agent_memory WHERE agent_name = ? AND source_run_id = ? AND tier = 0";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
        sqlite3_bind_text(stmt, 1, agent_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, run_id.c_str(),     -1, SQLITE_TRANSIENT);
        episodic = load_stmt(stmt);
        sqlite3_finalize(stmt);
    }

    if (episodic.empty()) return 0;

    // Promote high-importance entries to SEMANTIC
    int promoted = 0;
    for (auto& e : episodic) {
        if (e.importance >= 6.5f) {
            MemoryEntry semantic = e;
            semantic.tier = MemoryTier::SEMANTIC;
            write(semantic);
            promoted++;
        }
    }

    // Delete all EPISODIC for this run
    {
        std::lock_guard lock(db_.mutex());
        sqlite3* db = db_.handle();
        if (!db) return promoted;

        const char* sql =
            "DELETE FROM agent_memory WHERE agent_name = ? AND source_run_id = ? AND tier = 0";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return promoted;
        sqlite3_bind_text(stmt, 1, agent_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, run_id.c_str(),     -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return promoted;
}

// ── Abstract: SEMANTIC → PROCEDURAL ─────────────────────────────────────────

int MemoryStore::abstract_to_procedural(const std::string& agent_name) {
    auto semantic = list_tier(agent_name, MemoryTier::SEMANTIC, 500);
    if (static_cast<int>(semantic.size()) < 3) return 0;

    // Group entries by content prefix (first 40 chars, lowercased) as topic key
    std::map<std::string, std::vector<const MemoryEntry*>> groups;
    for (const auto& e : semantic) {
        std::string key = e.content.substr(0, std::min(e.content.size(), size_t(40)));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        // Normalize whitespace
        key.erase(std::remove_if(key.begin(), key.end(),
            [](char c){ return c == '\n' || c == '\r'; }), key.end());
        groups[key].push_back(&e);
    }

    int new_proc = 0;
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (const auto& [key, entries] : groups) {
        if (static_cast<int>(entries.size()) < 3) continue;

        std::string pattern =
            "Recurring pattern [×" + std::to_string(entries.size()) + "]: " +
            entries[0]->content.substr(0, 300);

        MemoryEntry proc;
        // Deterministic id based on agent+key to avoid duplicates on re-runs
        proc.id = "proc-" + std::to_string(std::hash<std::string>{}(agent_name + key));
        proc.agent_name    = agent_name;
        proc.workspace_id  = entries[0]->workspace_id;
        proc.tier          = MemoryTier::PROCEDURAL;
        proc.content       = pattern;
        proc.importance    = 8.5f;
        proc.recency_step  = entries[0]->recency_step;
        proc.created_at_ms = now_ms;

        write(proc);
        new_proc++;
    }

    return new_proc;
}

// ── Count ─────────────────────────────────────────────────────────────────────

size_t MemoryStore::count(const std::string& agent_name) const {
    std::lock_guard lock(db_.mutex());
    sqlite3* db = db_.handle();
    if (!db) return 0;

    const char* sql = "SELECT COUNT(*) FROM agent_memory WHERE agent_name = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, agent_name.c_str(), -1, SQLITE_TRANSIENT);
    size_t n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        n = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return n;
}

// ── Scoring ───────────────────────────────────────────────────────────────────

float MemoryStore::score(const MemoryEntry& e, const std::string& query, int current_step) const {
    int steps_ago = std::max(0, current_step - e.recency_step);
    float recency   = static_cast<float>(std::pow(0.995, steps_ago));
    float importance = e.importance / 10.0f;
    float relevance  = keyword_overlap(query, e.content);
    return 0.40f * recency + 0.35f * importance + 0.25f * relevance;
}

float MemoryStore::keyword_overlap(const std::string& a, const std::string& b) const {
    if (a.empty() || b.empty()) return 0.0f;

    auto tokenize = [](const std::string& s) {
        std::set<std::string> tokens;
        std::string tok;
        for (unsigned char c : s) {
            if (std::isalnum(c)) {
                tok += static_cast<char>(std::tolower(c));
            } else if (!tok.empty()) {
                if (tok.size() >= 3) tokens.insert(tok);
                tok.clear();
            }
        }
        if (tok.size() >= 3) tokens.insert(tok);
        return tokens;
    };

    auto ta = tokenize(a);
    auto tb = tokenize(b);
    if (ta.empty() || tb.empty()) return 0.0f;

    size_t intersection = 0;
    for (const auto& t : ta) {
        if (tb.count(t)) intersection++;
    }

    return static_cast<float>(intersection) /
           static_cast<float>(std::max(ta.size(), tb.size()));
}

} // namespace clove
