#include <clove/chain_store.hpp>
#include <algorithm>

namespace clove {

Chain ChainStore::create(uint32_t creator_agent_id, const std::string& name,
                          const std::string& description,
                          const nlohmann::json& metadata) {
    std::lock_guard lock(mutex_);

    Chain c;
    c.id = generate_chain_id();
    c.name = name;
    c.description = description;
    c.creator_agent_id = creator_agent_id;
    c.metadata = metadata;
    c.created_at_ms = now_ms();

    chains_[c.id] = c;
    return c;
}

std::optional<Chain> ChainStore::get(const std::string& id) const {
    std::lock_guard lock(mutex_);
    auto it = chains_.find(id);
    if (it == chains_.end()) return std::nullopt;
    return it->second;
}

std::vector<Chain> ChainStore::list(size_t limit) const {
    std::lock_guard lock(mutex_);

    std::vector<Chain> result;
    result.reserve(std::min(limit, chains_.size()));
    for (const auto& [_, c] : chains_) {
        result.push_back(c);
        if (result.size() >= limit) break;
    }

    std::sort(result.begin(), result.end(),
        [](const Chain& a, const Chain& b) {
            return a.created_at_ms > b.created_at_ms; // newest first
        });

    return result;
}

bool ChainStore::add_artifact(const std::string& chain_id,
                               const std::string& artifact_id) {
    std::lock_guard lock(mutex_);
    auto it = chains_.find(chain_id);
    if (it == chains_.end()) return false;
    it->second.artifact_ids.push_back(artifact_id);
    return true;
}

bool ChainStore::remove(const std::string& id, uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto it = chains_.find(id);
    if (it == chains_.end()) return false;
    if (it->second.creator_agent_id != agent_id) return false;
    chains_.erase(it);
    return true;
}

std::optional<Chain> ChainStore::fork(const std::string& chain_id,
                                       const std::string& at_artifact_id,
                                       uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto it = chains_.find(chain_id);
    if (it == chains_.end()) return std::nullopt;

    const auto& source = it->second;

    // Find the fork point
    auto fork_it = std::find(source.artifact_ids.begin(),
                              source.artifact_ids.end(),
                              at_artifact_id);
    if (fork_it == source.artifact_ids.end()) return std::nullopt;

    Chain forked;
    forked.id = generate_chain_id();
    forked.name = source.name + " (fork)";
    forked.description = "Forked from " + chain_id + " at " + at_artifact_id;
    forked.creator_agent_id = agent_id;
    forked.metadata = source.metadata;
    forked.created_at_ms = now_ms();

    // Copy artifacts up to and including the fork point
    forked.artifact_ids.assign(source.artifact_ids.begin(),
                                std::next(fork_it));

    chains_[forked.id] = forked;
    return forked;
}

void ChainStore::insert(const Chain& chain) {
    std::lock_guard lock(mutex_);
    chains_[chain.id] = chain;
}

size_t ChainStore::size() const {
    std::lock_guard lock(mutex_);
    return chains_.size();
}

} // namespace clove
