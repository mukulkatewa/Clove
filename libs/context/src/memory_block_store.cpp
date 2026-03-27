#include <clove/memory_block_store.hpp>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <unordered_set>
#include <cctype>

namespace clove {

// ── Tokenizer for relevance scoring ──
static std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string word;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            if (!word.empty() && word.size() > 2) {
                tokens.push_back(word);
            }
            word.clear();
        }
    }
    if (!word.empty() && word.size() > 2) tokens.push_back(word);
    return tokens;
}

static double keyword_overlap_score(const std::vector<std::string>& query_tokens,
                                     const std::string& text) {
    if (query_tokens.empty()) return 0.0;
    auto text_tokens = tokenize(text);
    std::unordered_set<std::string> text_set(text_tokens.begin(), text_tokens.end());

    int matches = 0;
    for (const auto& qt : query_tokens) {
        if (text_set.count(qt)) matches++;
    }
    return static_cast<double>(matches) / static_cast<double>(query_tokens.size());
}

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

std::vector<MemoryBlockStore::ScoredBlock> MemoryBlockStore::search(
    const std::string& query, uint32_t agent_id, size_t top_k) const {

    std::lock_guard lock(mutex_);

    auto query_tokens = tokenize(query);
    uint64_t now = now_ms();

    std::vector<ScoredBlock> scored;

    for (const auto& [_, b] : blocks_) {
        if (!can_read(b, agent_id)) continue;

        double score = 0.0;

        // 1. Keyword overlap (0.0 - 1.0) — weight: 0.5
        double name_overlap = keyword_overlap_score(query_tokens, b.name);
        double content_overlap = keyword_overlap_score(query_tokens, b.content);
        double keyword_score = std::max(name_overlap * 1.2, content_overlap); // name match worth more
        score += keyword_score * 0.5;

        // 2. Recency decay (0.0 - 1.0) — weight: 0.2
        // Exponential decay: 0.995^hours_since_update (from Generative Agents, Park et al. 2023)
        double hours_since = static_cast<double>(now - b.updated_at_ms) / 3600000.0;
        double recency = std::pow(0.995, hours_since);
        score += recency * 0.2;

        // 3. Type priority (0.0 - 1.0) — weight: 0.3
        // SYSTEM=1.0, CORE=0.7, RECALL=0.4
        double type_score = 0.4;
        if (b.type == MemoryBlockType::SYSTEM) type_score = 1.0;
        else if (b.type == MemoryBlockType::CORE) type_score = 0.7;
        score += type_score * 0.3;

        // Only include if there's some relevance (keyword match > 0 OR it's SYSTEM/CORE)
        if (keyword_score > 0.0 || b.type == MemoryBlockType::SYSTEM || b.type == MemoryBlockType::CORE) {
            scored.push_back({b, score});
        }
    }

    // Sort by score descending
    std::sort(scored.begin(), scored.end(),
        [](const ScoredBlock& a, const ScoredBlock& b) { return a.score > b.score; });

    // Return top-K
    if (scored.size() > top_k) scored.resize(top_k);
    return scored;
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
