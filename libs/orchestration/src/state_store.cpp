#include <clove/state_store.hpp>

namespace clove {

bool StateStore::store(const std::string& key, const nlohmann::json& value,
                       uint32_t agent_id, const std::string& scope,
                       uint64_t ttl_ms) {
    std::lock_guard lock(mutex_);

    // If key already exists, check write access
    if (auto it = data_.find(key); it != data_.end()) {
        const auto& existing = it->second;
        if (!is_expired(existing)) {
            // "agent" scope: only the owner can overwrite
            if (existing.scope == "agent" && existing.owner_agent_id != agent_id) {
                return false;
            }
            // "global" and "session" allow writes from anyone
        }
    }

    StoredValue sv{
        .value          = value,
        .expires_at     = {},
        .owner_agent_id = agent_id,
        .scope          = scope,
    };

    if (ttl_ms > 0) {
        sv.expires_at = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(ttl_ms);
    } else {
        // Far future sentinel — effectively no expiry
        sv.expires_at = std::chrono::steady_clock::time_point::max();
    }

    data_[key] = std::move(sv);
    return true;
}

std::optional<nlohmann::json> StateStore::fetch(const std::string& key,
                                                uint32_t agent_id) const {
    std::lock_guard lock(mutex_);

    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }

    const auto& sv = it->second;
    if (is_expired(sv)) {
        return std::nullopt;
    }
    if (!can_access(sv, agent_id)) {
        return std::nullopt;
    }

    return sv.value;
}

bool StateStore::erase(const std::string& key, uint32_t agent_id) {
    std::lock_guard lock(mutex_);

    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }

    const auto& sv = it->second;
    // Only the owner or global-scoped entries can be deleted
    if (sv.scope != "global" && sv.owner_agent_id != agent_id) {
        return false;
    }

    data_.erase(it);
    return true;
}

std::vector<std::string> StateStore::keys(const std::string& prefix,
                                           uint32_t agent_id) const {
    std::lock_guard lock(mutex_);

    std::vector<std::string> result;
    for (const auto& [key, sv] : data_) {
        if (is_expired(sv)) {
            continue;
        }
        if (!prefix.empty() && key.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        if (agent_id != 0 && !can_access(sv, agent_id)) {
            continue;
        }
        result.push_back(key);
    }
    return result;
}

void StateStore::evict_expired() {
    std::lock_guard lock(mutex_);

    for (auto it = data_.begin(); it != data_.end(); ) {
        if (is_expired(it->second)) {
            it = data_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t StateStore::size() const {
    std::lock_guard lock(mutex_);
    return data_.size();
}

bool StateStore::can_access(const StoredValue& val, uint32_t agent_id) const {
    if (val.scope == "global") {
        return true; // anyone can read/write
    }
    if (val.scope == "agent") {
        return val.owner_agent_id == agent_id; // only owner
    }
    if (val.scope == "session") {
        return val.owner_agent_id == agent_id; // only owner can read
    }
    return false;
}

bool StateStore::is_expired(const StoredValue& val) const {
    if (val.expires_at == std::chrono::steady_clock::time_point::max()) {
        return false;
    }
    return std::chrono::steady_clock::now() >= val.expires_at;
}

} // namespace clove
