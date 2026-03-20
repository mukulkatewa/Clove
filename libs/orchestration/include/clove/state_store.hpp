#pragma once
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace clove {

struct StoredValue {
    nlohmann::json value;
    std::chrono::steady_clock::time_point expires_at;
    uint32_t owner_agent_id;
    std::string scope;  // "global", "agent", "session"
};

class StateStore {
public:
    // Store a value with optional TTL (0 = no expiry)
    bool store(const std::string& key, const nlohmann::json& value,
               uint32_t agent_id, const std::string& scope = "global",
               uint64_t ttl_ms = 0);

    // Fetch a value (checks TTL and scope)
    std::optional<nlohmann::json> fetch(const std::string& key, uint32_t agent_id) const;

    // Delete a key (only owner or global scope)
    bool erase(const std::string& key, uint32_t agent_id);

    // List keys with optional prefix filter
    std::vector<std::string> keys(const std::string& prefix = "", uint32_t agent_id = 0) const;

    // Evict expired entries
    void evict_expired();

    // Get total entry count
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, StoredValue> data_;

    bool can_access(const StoredValue& val, uint32_t agent_id) const;
    bool is_expired(const StoredValue& val) const;
};

} // namespace clove
