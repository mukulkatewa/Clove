#include <clove/event_bus.hpp>
#include <chrono>

namespace clove {

const char* event_type_to_string(KernelEventType type) {
    switch (type) {
        case KernelEventType::AGENT_SPAWNED:    return "AGENT_SPAWNED";
        case KernelEventType::AGENT_EXITED:     return "AGENT_EXITED";
        case KernelEventType::AGENT_PAUSED:     return "AGENT_PAUSED";
        case KernelEventType::AGENT_RESUMED:    return "AGENT_RESUMED";
        case KernelEventType::AGENT_RESTARTING: return "AGENT_RESTARTING";
        case KernelEventType::AGENT_ESCALATED:  return "AGENT_ESCALATED";
        case KernelEventType::MESSAGE_RECEIVED: return "MESSAGE_RECEIVED";
        case KernelEventType::STATE_CHANGED:    return "STATE_CHANGED";
        case KernelEventType::SYSCALL_BLOCKED:  return "SYSCALL_BLOCKED";
        case KernelEventType::RESOURCE_WARNING: return "RESOURCE_WARNING";
        case KernelEventType::BUDGET_EXCEEDED:  return "BUDGET_EXCEEDED";
        case KernelEventType::BUDGET_WARNING:   return "BUDGET_WARNING";
        case KernelEventType::POLICY_UPDATED:   return "POLICY_UPDATED";
        case KernelEventType::CUSTOM:           return "CUSTOM";
    }
    return "UNKNOWN";
}

void EventBus::subscribe(uint32_t agent_id, KernelEventType type) {
    std::lock_guard lock(mutex_);
    uint32_t type_key = static_cast<uint32_t>(type);
    subscriptions_[type_key].insert(agent_id);
    // Ensure queue exists for this agent
    event_queues_[agent_id];
}

void EventBus::subscribe_all(uint32_t agent_id) {
    std::lock_guard lock(mutex_);

    static constexpr KernelEventType all_types[] = {
        KernelEventType::AGENT_SPAWNED,
        KernelEventType::AGENT_EXITED,
        KernelEventType::AGENT_PAUSED,
        KernelEventType::AGENT_RESUMED,
        KernelEventType::AGENT_RESTARTING,
        KernelEventType::AGENT_ESCALATED,
        KernelEventType::MESSAGE_RECEIVED,
        KernelEventType::STATE_CHANGED,
        KernelEventType::SYSCALL_BLOCKED,
        KernelEventType::RESOURCE_WARNING,
        KernelEventType::POLICY_UPDATED,
        KernelEventType::CUSTOM,
    };

    for (auto t : all_types) {
        subscriptions_[static_cast<uint32_t>(t)].insert(agent_id);
    }
    event_queues_[agent_id];
}

void EventBus::unsubscribe(uint32_t agent_id, KernelEventType type) {
    std::lock_guard lock(mutex_);
    uint32_t type_key = static_cast<uint32_t>(type);
    if (auto it = subscriptions_.find(type_key); it != subscriptions_.end()) {
        it->second.erase(agent_id);
    }
}

void EventBus::unsubscribe_all(uint32_t agent_id) {
    std::lock_guard lock(mutex_);

    for (auto& [type_key, subscribers] : subscriptions_) {
        subscribers.erase(agent_id);
    }
    event_queues_.erase(agent_id);
}

void EventBus::emit(KernelEventType type, const nlohmann::json& data,
                    uint32_t source_agent_id) {
    std::lock_guard lock(mutex_);

    uint32_t type_key = static_cast<uint32_t>(type);
    auto sub_it = subscriptions_.find(type_key);
    if (sub_it == subscriptions_.end()) {
        return; // no subscribers for this type
    }

    uint64_t ts = now_ms();

    for (uint32_t agent_id : sub_it->second) {
        auto& queue = event_queues_[agent_id];

        // Cap queue size — drop oldest if full
        if (queue.size() >= MAX_QUEUE_SIZE) {
            queue.pop();
        }

        KernelEvent event{
            .type            = type,
            .data            = data,
            .source_agent_id = source_agent_id,
            .timestamp_ms    = ts,
        };
        queue.push(std::move(event));
    }
}

std::vector<KernelEvent> EventBus::poll(uint32_t agent_id, size_t max_events) {
    std::lock_guard lock(mutex_);

    std::vector<KernelEvent> result;
    auto it = event_queues_.find(agent_id);
    if (it == event_queues_.end()) {
        return result;
    }

    auto& queue = it->second;
    while (!queue.empty() && result.size() < max_events) {
        result.push_back(std::move(queue.front()));
        queue.pop();
    }
    return result;
}

bool EventBus::has_events(uint32_t agent_id) const {
    std::lock_guard lock(mutex_);

    auto it = event_queues_.find(agent_id);
    if (it == event_queues_.end()) {
        return false;
    }
    return !it->second.empty();
}

uint64_t EventBus::now_ms() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

} // namespace clove
