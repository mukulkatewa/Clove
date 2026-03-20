#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace clove {

struct DenialEvent {
    uint32_t    agent_id;
    std::string category;  // "http", "exec", "read", "write", "llm"
    std::string resource;  // domain, command, path, model
    std::string reason;
    uint64_t    timestamp_ms;
};

struct PolicyRecommendation {
    std::string    category;
    std::string    action;           // "add_domain", "add_command", "increase_quota"
    std::string    resource;
    uint32_t       occurrence_count;
    nlohmann::json suggested_change;
};

class PolicyRecommender {
public:
    /// Record a denial event.
    void record_denial(const DenialEvent& event);

    /// Get recommendations based on accumulated denials.
    [[nodiscard]] std::vector<PolicyRecommendation> get_recommendations(
        size_t max = 20) const;

    /// Clear all recorded denials.
    void clear();

    /// Get total denial count.
    [[nodiscard]] size_t denial_count() const;

private:
    mutable std::mutex mutex_;

    struct DenialAggregate {
        std::string category;
        std::string resource;
        uint32_t    count         = 0;
        uint64_t    first_seen_ms = 0;
        uint64_t    last_seen_ms  = 0;
    };

    // Key: "category:resource"
    std::unordered_map<std::string, DenialAggregate> aggregates_;
};

} // namespace clove
