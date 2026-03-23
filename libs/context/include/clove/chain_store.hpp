#pragma once

#include <clove/artifact.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace clove {

class ChainStore {
public:
    Chain create(uint32_t creator_agent_id, const std::string& name,
                 const std::string& description = "",
                 const nlohmann::json& metadata = nlohmann::json::object());

    std::optional<Chain> get(const std::string& id) const;

    std::vector<Chain> list(size_t limit = 100) const;

    bool add_artifact(const std::string& chain_id,
                      const std::string& artifact_id);

    bool remove(const std::string& id, uint32_t agent_id);

    // Fork a chain at a given artifact — creates a new chain with artifacts
    // up to and including `at_artifact_id`.
    std::optional<Chain> fork(const std::string& chain_id,
                              const std::string& at_artifact_id,
                              uint32_t agent_id);

    // Insert a pre-built chain (for persistence loading)
    void insert(const Chain& chain);

    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Chain> chains_;
};

} // namespace clove
