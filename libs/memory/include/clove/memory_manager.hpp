#pragma once
#include <clove/memory_store.hpp>
#include <string>

namespace clove {

class Database;

/// Orchestrates the three-tier memory lifecycle for a CLOVE agent.
///
///   Tier 0 EPISODIC    — raw compressed steps (per-run, volatile)
///   Tier 1 SEMANTIC    — atomic facts (post-run, durable)
///   Tier 2 PROCEDURAL  — cross-run patterns (daemon, long-lived)
///
/// Wire into RunEngine: remember() / recall() replace the old MemoryBlockStore path.
/// Call consolidate() post-run. Call abstract() from the memory-consolidator daemon.
class MemoryManager {
public:
    explicit MemoryManager(Database& db);

    /// Called by `remember` tool: store a fact directly as SEMANTIC (high importance).
    std::string remember(
        const std::string& agent_name,
        const std::string& workspace_id,
        const std::string& run_id,
        int current_step,
        const std::string& fact);

    /// Called by `recall` tool: 3-axis scored retrieval within token budget.
    std::string recall(
        const std::string& agent_name,
        const std::string& workspace_id,
        const std::string& query,
        int current_step,
        size_t token_budget = 2000);

    /// Called post-run by RunEngine: EPISODIC → SEMANTIC consolidation.
    void consolidate(const std::string& agent_name, const std::string& run_id);

    /// Called by memory-consolidator daemon: SEMANTIC → PROCEDURAL abstraction.
    void abstract(const std::string& agent_name);

    MemoryStore& store() { return store_; }

private:
    MemoryStore store_;

    std::string generate_id() const;
    float       score_importance(const std::string& content) const;
};

} // namespace clove
