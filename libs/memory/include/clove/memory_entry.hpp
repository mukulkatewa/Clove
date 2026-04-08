#pragma once
#include <string>
#include <cstdint>

namespace clove {

/// Three-tier memory hierarchy.
/// EPISODIC  — volatile: compressed steps from the current/recent run
/// SEMANTIC  — durable: atomic facts promoted from episodic post-run
/// PROCEDURAL — persistent: cross-run patterns abstracted by the daemon
enum class MemoryTier : int {
    EPISODIC    = 0,
    SEMANTIC    = 1,
    PROCEDURAL  = 2,
};

struct MemoryEntry {
    std::string  id;
    std::string  agent_name;
    std::string  workspace_id;
    MemoryTier   tier          = MemoryTier::EPISODIC;
    std::string  content;
    float        importance    = 5.0f;   // 1.0–10.0, scored at write time
    int          recency_step  = 0;      // step index when written (for decay)
    std::string  causal_parent;          // id of the memory entry that caused this
    std::string  source_run_id;
    int64_t      created_at_ms = 0;

    // Computed at retrieval time — not persisted
    float        score         = 0.0f;
};

} // namespace clove
