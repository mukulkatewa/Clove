#pragma once

#include <clove/memory_block.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace clove {

class MemoryBlockStore {
public:
    // Create a new memory block. Returns the created block.
    MemoryBlock create(uint32_t owner_agent_id, const std::string& name,
                       MemoryBlockType type, MemoryAccess access,
                       const std::string& content = "",
                       size_t max_tokens = 0);

    // Get a block by ID (checks access for requesting agent).
    std::optional<MemoryBlock> get(const std::string& id, uint32_t agent_id) const;

    // Get a block by name for a specific owner.
    std::optional<MemoryBlock> get_by_name(const std::string& name, uint32_t owner_id) const;

    // Overwrite block content. Returns false if not found or no write access.
    bool write(const std::string& id, const std::string& content, uint32_t agent_id);

    // Append to block content. Returns false if not found or no write access.
    bool append(const std::string& id, const std::string& content, uint32_t agent_id);

    // Delete a block. Only owner can delete.
    bool remove(const std::string& id, uint32_t agent_id);

    // List blocks visible to an agent (owned + shared).
    std::vector<MemoryBlock> list(uint32_t agent_id, size_t limit = 100) const;

    // Share a block with another agent.
    bool share(const std::string& id, uint32_t target_agent_id, uint32_t requesting_agent_id);

    // Get all SYSTEM+CORE blocks for an agent (for context assembly).
    std::vector<MemoryBlock> get_assembly_blocks(uint32_t agent_id) const;

    // Insert a pre-built block (for persistence loading).
    void insert(const MemoryBlock& block);

    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, MemoryBlock> blocks_;

    bool can_read(const MemoryBlock& block, uint32_t agent_id) const;
    bool can_write(const MemoryBlock& block, uint32_t agent_id) const;
};

} // namespace clove
