#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <vector>

namespace clove {

class Database;

class StateStoreDb {
public:
    explicit StateStoreDb(Database& db);

    // Persist a key-value pair
    bool store(const std::string& key, const nlohmann::json& value,
               uint32_t agent_id, const std::string& scope);

    // Fetch a value
    std::optional<nlohmann::json> fetch(const std::string& key) const;

    // Delete a key
    bool erase(const std::string& key);

    // List keys with prefix
    std::vector<std::string> keys(const std::string& prefix = "") const;

    // Load all persisted state into memory (for kernel boot)
    struct Entry {
        std::string key;
        nlohmann::json value;
        uint32_t agent_id;
        std::string scope;
    };
    std::vector<Entry> load_all() const;

    // Count entries
    size_t count() const;

private:
    Database& db_;
};

} // namespace clove
