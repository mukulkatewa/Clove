#pragma once

#include <clove/state_store.hpp>
#include <clove/event_bus.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <chrono>

namespace clove {

struct World {
    uint32_t id;
    std::string name;
    std::set<uint32_t> members;
    std::unique_ptr<StateStore> state;
    std::unique_ptr<EventBus> events;
    nlohmann::json metadata;
    std::chrono::system_clock::time_point created_at;
};

struct WorldSummary {
    uint32_t id;
    std::string name;
    size_t member_count;
    nlohmann::json metadata;
};

class WorldEngine {
public:
    WorldEngine();
    ~WorldEngine();

    WorldEngine(const WorldEngine&) = delete;
    WorldEngine& operator=(const WorldEngine&) = delete;

    // Lifecycle
    uint32_t create(const std::string& name, const nlohmann::json& metadata = {});
    bool destroy(uint32_t world_id);
    std::vector<WorldSummary> list() const;

    // Membership
    bool join(uint32_t world_id, uint32_t agent_id);
    bool leave(uint32_t world_id, uint32_t agent_id);

    // World-scoped events
    void emit_event(uint32_t world_id, KernelEventType type,
                    const nlohmann::json& data, uint32_t source_agent_id);

    // World-scoped state
    std::optional<nlohmann::json> get_state(uint32_t world_id,
                                             const std::string& key,
                                             uint32_t agent_id) const;
    bool set_state(uint32_t world_id, const std::string& key,
                   const nlohmann::json& value, uint32_t agent_id);

    // Snapshot / Restore
    nlohmann::json snapshot(uint32_t world_id) const;
    uint32_t restore(const nlohmann::json& snapshot_json);

    // Lookup
    bool exists(uint32_t world_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<World>> worlds_;
    uint32_t next_id_ = 1;

    World* find(uint32_t world_id) const;
};

} // namespace clove
