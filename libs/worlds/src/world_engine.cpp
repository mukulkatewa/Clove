#include <clove/world_engine.hpp>

namespace clove {

WorldEngine::WorldEngine() = default;
WorldEngine::~WorldEngine() = default;

uint32_t WorldEngine::create(const std::string& name, const nlohmann::json& metadata) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto world = std::make_unique<World>();
    world->id = next_id_++;
    world->name = name;
    world->metadata = metadata;
    world->state = std::make_unique<StateStore>();
    world->events = std::make_unique<EventBus>();
    world->created_at = std::chrono::system_clock::now();
    uint32_t id = world->id;
    worlds_[id] = std::move(world);
    return id;
}

bool WorldEngine::destroy(uint32_t world_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return worlds_.erase(world_id) > 0;
}

std::vector<WorldSummary> WorldEngine::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WorldSummary> result;
    result.reserve(worlds_.size());
    for (const auto& [id, w] : worlds_) {
        result.push_back({w->id, w->name, w->members.size(), w->metadata});
    }
    return result;
}

bool WorldEngine::join(uint32_t world_id, uint32_t agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* w = find(world_id);
    if (!w) return false;
    w->members.insert(agent_id);
    // Auto-subscribe agent to world's event bus
    w->events->subscribe_all(agent_id);
    return true;
}

bool WorldEngine::leave(uint32_t world_id, uint32_t agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* w = find(world_id);
    if (!w) return false;
    w->members.erase(agent_id);
    w->events->unsubscribe_all(agent_id);
    return true;
}

void WorldEngine::emit_event(uint32_t world_id, KernelEventType type,
                              const nlohmann::json& data, uint32_t source_agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* w = find(world_id);
    if (!w) return;
    w->events->emit(type, data, source_agent_id);
}

std::optional<nlohmann::json> WorldEngine::get_state(uint32_t world_id,
                                                      const std::string& key,
                                                      uint32_t agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* w = find(world_id);
    if (!w) return std::nullopt;
    return w->state->fetch(key, agent_id);
}

bool WorldEngine::set_state(uint32_t world_id, const std::string& key,
                             const nlohmann::json& value, uint32_t agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* w = find(world_id);
    if (!w) return false;
    return w->state->store(key, value, agent_id, "global", 0);
}

nlohmann::json WorldEngine::snapshot(uint32_t world_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* w = find(world_id);
    if (!w) return nlohmann::json{};

    nlohmann::json snap;
    snap["name"] = w->name;
    snap["metadata"] = w->metadata;
    snap["members"] = nlohmann::json::array();
    for (uint32_t m : w->members) {
        snap["members"].push_back(m);
    }

    // Snapshot state: get all keys and their values
    auto keys = w->state->keys("", 0);
    nlohmann::json state_snap = nlohmann::json::object();
    for (const auto& k : keys) {
        auto val = w->state->fetch(k, 0);
        if (val) state_snap[k] = *val;
    }
    snap["state"] = state_snap;
    return snap;
}

uint32_t WorldEngine::restore(const nlohmann::json& snapshot_json) {
    std::string name = snapshot_json.value("name", "restored");
    nlohmann::json metadata = snapshot_json.value("metadata", nlohmann::json::object());

    // Create new world (takes lock internally)
    uint32_t new_id = create(name, metadata);

    std::lock_guard<std::mutex> lock(mutex_);
    auto* w = find(new_id);
    if (!w) return 0;

    // Restore members
    if (snapshot_json.contains("members")) {
        for (const auto& m : snapshot_json["members"]) {
            w->members.insert(m.get<uint32_t>());
        }
    }

    // Restore state
    if (snapshot_json.contains("state") && snapshot_json["state"].is_object()) {
        for (auto& [key, val] : snapshot_json["state"].items()) {
            w->state->store(key, val, 0, "global", 0);
        }
    }

    return new_id;
}

bool WorldEngine::exists(uint32_t world_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return worlds_.count(world_id) > 0;
}

World* WorldEngine::find(uint32_t world_id) const {
    auto it = worlds_.find(world_id);
    if (it == worlds_.end()) return nullptr;
    return it->second.get();
}

} // namespace clove
