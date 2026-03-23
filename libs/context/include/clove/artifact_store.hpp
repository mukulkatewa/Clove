#pragma once

#include <clove/artifact.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace clove {

struct ArtifactFilter {
    static constexpr size_t MAX_LIMIT = 10000;

    std::optional<std::string> chain_id;
    std::optional<ArtifactType> type;
    std::optional<ArtifactState> state;
    std::optional<uint32_t> author_agent_id;
    size_t limit = 100;
};

class ArtifactStore {
public:
    Artifact create(uint32_t author_agent_id, ArtifactType type,
                    const std::string& title, const std::string& content,
                    const std::string& chain_id,
                    const std::vector<std::string>& parent_ids = {},
                    const nlohmann::json& metadata = nlohmann::json::object());

    std::optional<Artifact> get(const std::string& id) const;

    bool update_content(const std::string& id, const std::string& content,
                        uint32_t agent_id);

    bool update_state(const std::string& id, ArtifactState new_state,
                      uint32_t agent_id);

    std::vector<Artifact> list(const ArtifactFilter& filter = {}) const;

    bool remove(const std::string& id, uint32_t agent_id);

    // Insert a pre-built artifact (for persistence loading)
    void insert(const Artifact& artifact);

    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Artifact> artifacts_;

    static bool is_valid_transition(ArtifactState from, ArtifactState to);
};

} // namespace clove
