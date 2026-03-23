#include <clove/artifact_store.hpp>
#include <algorithm>

namespace clove {

Artifact ArtifactStore::create(uint32_t author_agent_id, ArtifactType type,
                                const std::string& title, const std::string& content,
                                const std::string& chain_id,
                                const std::vector<std::string>& parent_ids,
                                const nlohmann::json& metadata) {
    std::lock_guard lock(mutex_);

    Artifact a;
    a.id = generate_artifact_id();
    a.chain_id = chain_id;
    a.author_agent_id = author_agent_id;
    a.type = type;
    a.state = ArtifactState::DRAFT;
    a.title = title;
    a.content = content;
    a.parent_ids = parent_ids;
    a.metadata = metadata;
    a.created_at_ms = now_ms();
    a.updated_at_ms = a.created_at_ms;

    artifacts_[a.id] = a;
    return a;
}

std::optional<Artifact> ArtifactStore::get(const std::string& id) const {
    std::lock_guard lock(mutex_);
    auto it = artifacts_.find(id);
    if (it == artifacts_.end()) return std::nullopt;
    return it->second;
}

bool ArtifactStore::update_content(const std::string& id, const std::string& content,
                                    uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto it = artifacts_.find(id);
    if (it == artifacts_.end()) return false;
    if (it->second.author_agent_id != agent_id) return false;

    it->second.content = content;
    it->second.updated_at_ms = now_ms();
    return true;
}

bool ArtifactStore::update_state(const std::string& id, ArtifactState new_state,
                                  uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto it = artifacts_.find(id);
    if (it == artifacts_.end()) return false;
    if (it->second.author_agent_id != agent_id) return false;

    if (!is_valid_transition(it->second.state, new_state)) return false;

    it->second.state = new_state;
    it->second.updated_at_ms = now_ms();
    return true;
}

std::vector<Artifact> ArtifactStore::list(const ArtifactFilter& filter) const {
    std::lock_guard lock(mutex_);

    std::vector<Artifact> result;
    for (const auto& [_, a] : artifacts_) {
        if (filter.chain_id && a.chain_id != *filter.chain_id) continue;
        if (filter.type && a.type != *filter.type) continue;
        if (filter.state && a.state != *filter.state) continue;
        if (filter.author_agent_id && a.author_agent_id != *filter.author_agent_id) continue;
        result.push_back(a);
        if (result.size() >= filter.limit) break;
    }

    // Sort by creation time
    std::sort(result.begin(), result.end(),
        [](const Artifact& a, const Artifact& b) {
            return a.created_at_ms < b.created_at_ms;
        });

    return result;
}

bool ArtifactStore::remove(const std::string& id, uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto it = artifacts_.find(id);
    if (it == artifacts_.end()) return false;
    if (it->second.author_agent_id != agent_id) return false;
    if (it->second.state != ArtifactState::DRAFT) return false;

    artifacts_.erase(it);
    return true;
}

void ArtifactStore::insert(const Artifact& artifact) {
    std::lock_guard lock(mutex_);
    artifacts_[artifact.id] = artifact;
}

size_t ArtifactStore::size() const {
    std::lock_guard lock(mutex_);
    return artifacts_.size();
}

bool ArtifactStore::is_valid_transition(ArtifactState from, ArtifactState to) {
    // Forward-only transitions: DRAFT → IN_REVIEW → APPROVED → FINAL → ARCHIVED
    return static_cast<uint8_t>(to) > static_cast<uint8_t>(from);
}

} // namespace clove
