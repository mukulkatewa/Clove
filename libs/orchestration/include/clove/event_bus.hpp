#pragma once
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace clove {

enum class KernelEventType : uint32_t {
    AGENT_SPAWNED = 1,
    AGENT_EXITED,
    AGENT_PAUSED,
    AGENT_RESUMED,
    AGENT_RESTARTING,
    AGENT_ESCALATED,
    MESSAGE_RECEIVED,
    STATE_CHANGED,
    SYSCALL_BLOCKED,
    RESOURCE_WARNING,
    POLICY_UPDATED,
    CUSTOM
};

const char* event_type_to_string(KernelEventType type);

struct KernelEvent {
    KernelEventType type;
    nlohmann::json data;
    uint32_t source_agent_id;
    uint64_t timestamp_ms;
};

class EventBus {
public:
    // Subscribe an agent to event types
    void subscribe(uint32_t agent_id, KernelEventType type);
    void subscribe_all(uint32_t agent_id);
    void unsubscribe(uint32_t agent_id, KernelEventType type);
    void unsubscribe_all(uint32_t agent_id);

    // Emit an event (delivered to all subscribers)
    void emit(KernelEventType type, const nlohmann::json& data, uint32_t source_agent_id);

    // Poll pending events for an agent
    std::vector<KernelEvent> poll(uint32_t agent_id, size_t max_events = 50);

    // Check if agent has pending events
    bool has_events(uint32_t agent_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> subscriptions_; // type → {agent_ids}
    std::unordered_map<uint32_t, std::queue<KernelEvent>> event_queues_; // agent_id → events
    static constexpr size_t MAX_QUEUE_SIZE = 1000;

    uint64_t now_ms() const;
};

} // namespace clove
