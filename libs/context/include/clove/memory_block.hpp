#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>
#include <clove/context_utils.hpp>

namespace clove {

enum class MemoryBlockType : uint8_t {
    SYSTEM = 0,   // Pinned at top of context, agent cannot edit
    CORE   = 1,   // Always included in assembly, agent can edit
    RECALL = 2,   // Included only when relevant, agent can read/write
};

enum class MemoryAccess : uint8_t {
    PRIVATE          = 0,  // Only owner can read/write
    SHARED_READ      = 1,  // Others can read, only owner writes
    SHARED_READWRITE = 2,  // Multiple agents can read and write
};

struct MemoryBlock {
    std::string id;                     // "mem_" + 12-char hex
    std::string name;                   // human-readable ("persona", "task_state", etc.)
    uint32_t owner_agent_id = 0;
    MemoryBlockType type = MemoryBlockType::CORE;
    MemoryAccess access = MemoryAccess::PRIVATE;
    std::string content;                // markdown/text
    std::set<uint32_t> shared_with;     // agent IDs with access
    size_t max_tokens = 0;              // 0 = unlimited
    uint64_t created_at_ms = 0;
    uint64_t updated_at_ms = 0;
};

// --- String conversion ---

inline const char* memory_block_type_to_string(MemoryBlockType t) {
    switch (t) {
        case MemoryBlockType::SYSTEM: return "system";
        case MemoryBlockType::CORE:   return "core";
        case MemoryBlockType::RECALL: return "recall";
    }
    return "unknown";
}

inline MemoryBlockType memory_block_type_from_string(const std::string& s) {
    if (s == "system") return MemoryBlockType::SYSTEM;
    if (s == "core")   return MemoryBlockType::CORE;
    if (s == "recall") return MemoryBlockType::RECALL;
    return MemoryBlockType::CORE;
}

inline const char* memory_access_to_string(MemoryAccess a) {
    switch (a) {
        case MemoryAccess::PRIVATE:          return "private";
        case MemoryAccess::SHARED_READ:      return "shared_read";
        case MemoryAccess::SHARED_READWRITE: return "shared_readwrite";
    }
    return "unknown";
}

inline MemoryAccess memory_access_from_string(const std::string& s) {
    if (s == "private")          return MemoryAccess::PRIVATE;
    if (s == "shared_read")      return MemoryAccess::SHARED_READ;
    if (s == "shared_readwrite") return MemoryAccess::SHARED_READWRITE;
    return MemoryAccess::PRIVATE;
}

// --- JSON serialization ---

inline nlohmann::json memory_block_to_json(const MemoryBlock& b) {
    nlohmann::json shared_arr = nlohmann::json::array();
    for (auto id : b.shared_with) shared_arr.push_back(id);

    return {
        {"id", b.id},
        {"name", b.name},
        {"owner_agent_id", b.owner_agent_id},
        {"type", memory_block_type_to_string(b.type)},
        {"access", memory_access_to_string(b.access)},
        {"content", b.content},
        {"shared_with", shared_arr},
        {"max_tokens", b.max_tokens},
        {"content_tokens", b.content.size() / 4},
        {"created_at_ms", b.created_at_ms},
        {"updated_at_ms", b.updated_at_ms},
    };
}

} // namespace clove
