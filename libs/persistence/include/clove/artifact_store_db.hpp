#pragma once

#include <clove/artifact.hpp>
#include <string>
#include <optional>
#include <vector>

namespace clove {

class Database;

class ArtifactStoreDb {
public:
    explicit ArtifactStoreDb(Database& db);

    // Persist an artifact
    bool store(const Artifact& artifact);

    // Load a single artifact
    std::optional<Artifact> get(const std::string& id) const;

    // Delete an artifact
    bool erase(const std::string& id);

    // Load all artifacts (for kernel boot)
    std::vector<Artifact> load_all() const;

    // Persist a chain
    bool store_chain(const Chain& chain);

    // Load all chains (for kernel boot)
    std::vector<Chain> load_all_chains() const;

    // Delete a chain
    bool erase_chain(const std::string& id);

    size_t count() const;

private:
    Database& db_;
};

} // namespace clove
