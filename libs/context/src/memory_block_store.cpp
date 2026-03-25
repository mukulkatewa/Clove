#include <clove/memory_block_store.hpp>
#include <algorithm>

namespace clove {

MemoryBlock MemoryBlockStore::create(uint32_t owner_agent_id, const std::string& name,
                                      MemoryBlockType type, MemoryAccess access,
                                      const std::string& content, size_t max_tokens) {
    std::lock_guard lock(mutex_);

    MemoryBlock b;
    b.id = "mem_" + generate_hex_id();
    b.name = name;
    b.owner_agent_id = owner_agent_id;
    b.type = type;
    b.access = access;
    b.content = content;
    b.max_tokens = max_tokens;
    b.created_at_ms = now_ms();
    b.updated_at_ms = b.created_at_ms;

    blocks_[b.id] = b;
    return b;
}

std::optional<MemoryBlock> MemoryBlockStore::get(const std::string& id, uint32_t agent_id) const {
    std::lock_guard lock(mutex_);
    auto it = blocks_.find(id);
    if (it == blocks_.end()) return std::nullopt;
    if (!can_read(it->second, agent_id)) return std::nullopt;
    return it->second;
}

std::optional<MemoryBlock> MemoryBlockStore::get_by_name(const std::string& name,
                                                          uint32_t owner_id) const {
    std::lock_guard lock(mutex_);
    for (const auto& [_, b] : blocks_) {
        if (b.name == name && b.owner_agent_id == owner_id) {
            return b;
        }
    }
    return std::nullopt;
}

bool MemoryBlockStore::write(const std::string& id, const std::string& content,
                              uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto it = blocks_.find(id);
    if (it == blocks_.end()) return false;
    if (!can_write(it->second, agent_id)) return false;

    // SYSTEM blocks cannot be edited by agents (only kernel can set them)
    if (it->second.type == MemoryBlockType::SYSTEM &&
        agent_id != 0 && agent_id != it->second.owner_agent_id) {
        return false;
    }

    // Check max_tokens if set
    if (it->second.max_tokens > 0 && (content.size() / 4) > it->second.max_tokens) {
        return false;
    }

    it->second.content = content;
    it->second.updated_at_ms = now_ms();
    return true;
}

bool MemoryBlockStore::append(const std::string& id, const std::string& content,
                               uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto it = blocks_.find(id);
    if (it == blocks_.end()) return false;
    if (!can_write(it->second, agent_id)) return false;

    if (it->second.type == MemoryBlockType::SYSTEM &&
        agent_id != 0 && agent_id != it->second.owner_agent_id) {
        return false;
    }

    // Check max_tokens if set
    if (it->second.max_tokens > 0 &&
        ((it->second.content.size() + content.size()) / 4) > it->second.max_tokens) {
        return false;
    }

    it->second.content += content;
    it->second.updated_at_ms = now_ms();
    return true;
}

bool MemoryBlockStore::remove(const std::string& id, uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto it = blocks_.find(id);
    if (it == blocks_.end()) return false;
    if (it->second.owner_agent_id != agent_id) return false;

    blocks_.erase(it);
    return true;
}

std::vector<MemoryBlock> MemoryBlockStore::list(uint32_t agent_id, size_t limit) const {
    std::lock_guard lock(mutex_);

    std::vector<MemoryBlock> result;
    for (const auto& [_, b] : blocks_) {
        if (can_read(b, agent_id)) {
            result.push_back(b);
            if (result.size() >= limit) break;
        }
    }

    std::sort(result.begin(), result.end(),
        [](const MemoryBlock& a, const MemoryBlock& b) {
            // SYSTEM first, then CORE, then RECALL
            if (a.type != b.type) return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
            return a.created_at_ms < b.created_at_ms;
        });

    return result;
}

bool MemoryBlockStore::share(const std::string& id, uint32_t target_agent_id,
                              uint32_t requesting_agent_id) {
    std::lock_guard lock(mutex_);
    auto it = blocks_.find(id);
    if (it == blocks_.end()) return false;
    if (it->second.owner_agent_id != requesting_agent_id) return false;
    if (it->second.access == MemoryAccess::PRIVATE) return false;

    it->second.shared_with.insert(target_agent_id);
    it->second.updated_at_ms = now_ms();
    return true;
}

std::vector<MemoryBlock> MemoryBlockStore::get_assembly_blocks(uint32_t agent_id) const {
    std::lock_guard lock(mutex_);

    std::vector<MemoryBlock> result;
    for (const auto& [_, b] : blocks_) {
        if (!can_read(b, agent_id)) continue;

        // SYSTEM and CORE are always included
        if (b.type == MemoryBlockType::SYSTEM || b.type == MemoryBlockType::CORE) {
            result.push_back(b);
        }
    }

    // Sort: SYSTEM first, then CORE, by creation time
    std::sort(result.begin(), result.end(),
        [](const MemoryBlock& a, const MemoryBlock& b) {
            if (a.type != b.type) return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
            return a.created_at_ms < b.created_at_ms;
        });

    return result;
}

void MemoryBlockStore::insert(const MemoryBlock& block) {
    std::lock_guard lock(mutex_);
    blocks_[block.id] = block;
}

size_t MemoryBlockStore::size() const {
    std::lock_guard lock(mutex_);
    return blocks_.size();
}

bool MemoryBlockStore::can_read(const MemoryBlock& block, uint32_t agent_id) const {
    if (block.owner_agent_id == agent_id) return true;
    if (block.access == MemoryAccess::PRIVATE) return false;
    // SHARED_READ or SHARED_READWRITE: check if agent is in shared_with
    return block.shared_with.count(agent_id) > 0;
}

bool MemoryBlockStore::can_write(const MemoryBlock& block, uint32_t agent_id) const {
    if (block.owner_agent_id == agent_id) return true;
    if (block.access != MemoryAccess::SHARED_READWRITE) return false;
    return block.shared_with.count(agent_id) > 0;
}

} // namespace clove
