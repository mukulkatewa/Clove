#pragma once
#include <clove/memory_entry.hpp>
#include <string>
#include <vector>
#include <optional>

namespace clove {

class Database;

/// SQLite-backed store for the three-tier agent memory system.
/// Handles write, scored retrieval, episodic consolidation, and pattern abstraction.
class MemoryStore {
public:
    explicit MemoryStore(Database& db);

    /// Persist a memory entry. Replaces on id collision.
    bool write(const MemoryEntry& entry);

    /// 3-axis scored retrieval within a token budget.
    /// Score = 0.40 × recency_decay(0.995^steps_ago)
    ///       + 0.35 × (importance / 10.0)
    ///       + 0.25 × keyword_overlap(query, content)
    /// Returns entries in order: PROCEDURAL → SEMANTIC → EPISODIC
    /// (Lost in the Middle positioning: high-value at context boundaries)
    ///
    /// Workspace sharing: if workspace_id is non-empty, also includes SEMANTIC
    /// and PROCEDURAL entries written by other agents in the same workspace.
    /// EPISODIC is always private (per-agent, volatile).
    std::vector<MemoryEntry> retrieve(
        const std::string& agent_name,
        const std::string& workspace_id,
        const std::string& query,
        int current_step,
        size_t token_budget = 2000,
        std::optional<MemoryTier> tier_filter = std::nullopt) const;

    /// List all entries for a tier — used by consolidation and abstraction passes.
    std::vector<MemoryEntry> list_tier(
        const std::string& agent_name, MemoryTier tier, int limit = 200) const;

    /// Post-run: promote high-importance EPISODIC entries from this run to SEMANTIC.
    /// Returns number of entries promoted.
    int consolidate(const std::string& agent_name, const std::string& run_id);

    /// Daemon pass: detect recurring SEMANTIC patterns → write as PROCEDURAL.
    /// Returns number of new procedural entries written.
    int abstract_to_procedural(const std::string& agent_name);

    size_t count(const std::string& agent_name) const;

private:
    Database& db_;

    float score(const MemoryEntry& e, const std::string& query, int current_step) const;
    float keyword_overlap(const std::string& a, const std::string& b) const;
    static size_t estimate_tokens(const std::string& text) { return text.size() / 4 + 1; }
};

} // namespace clove
