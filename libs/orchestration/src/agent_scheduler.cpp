#include <clove/agent_scheduler.hpp>
#include <algorithm>

namespace clove {

// ---------------------------------------------------------------------------
// String conversions
// ---------------------------------------------------------------------------

const char* agent_priority_to_string(AgentPriority p) {
    switch (p) {
        case AgentPriority::CRITICAL: return "critical";
        case AgentPriority::HIGH:     return "high";
        case AgentPriority::NORMAL:   return "normal";
        case AgentPriority::LOW:      return "low";
        case AgentPriority::IDLE:     return "idle";
    }
    return "unknown";
}

AgentPriority agent_priority_from_string(const std::string& s) {
    if (s == "critical") return AgentPriority::CRITICAL;
    if (s == "high")     return AgentPriority::HIGH;
    if (s == "normal")   return AgentPriority::NORMAL;
    if (s == "low")      return AgentPriority::LOW;
    if (s == "idle")     return AgentPriority::IDLE;
    return AgentPriority::NORMAL;
}

const char* schedule_state_to_string(AgentScheduleState s) {
    switch (s) {
        case AgentScheduleState::IDLE:         return "idle";
        case AgentScheduleState::READY:        return "ready";
        case AgentScheduleState::WAITING_LLM:  return "waiting_llm";
        case AgentScheduleState::WAITING_TOOL: return "waiting_tool";
        case AgentScheduleState::COMPLETED:    return "completed";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Priority management
// ---------------------------------------------------------------------------

void AgentScheduler::set_priority(uint32_t agent_id, AgentPriority priority) {
    std::lock_guard lock(mutex_);
    get_or_create(agent_id).priority = priority;
}

AgentPriority AgentScheduler::get_priority(uint32_t agent_id) const {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(agent_id);
    if (it == entries_.end()) return AgentPriority::NORMAL;
    return it->second.priority;
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

void AgentScheduler::mark_ready(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto& e = get_or_create(agent_id);

    // If transitioning from WAITING_TOOL, record tool wait time
    if (e.state == AgentScheduleState::WAITING_TOOL && e.llm_started_at_ms > 0) {
        e.total_tool_wait_ms += now_ms() - e.llm_started_at_ms;
    }

    e.state = AgentScheduleState::READY;
    e.queued_at_ms = now_ms();
}

void AgentScheduler::mark_waiting_llm(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto& e = get_or_create(agent_id);
    e.state = AgentScheduleState::WAITING_LLM;
    e.llm_started_at_ms = now_ms();
    e.llm_calls++;
}

void AgentScheduler::mark_waiting_tool(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto& e = get_or_create(agent_id);

    // If transitioning from WAITING_LLM, record LLM wait time
    if (e.state == AgentScheduleState::WAITING_LLM && e.llm_started_at_ms > 0) {
        e.total_llm_wait_ms += now_ms() - e.llm_started_at_ms;
    }

    e.state = AgentScheduleState::WAITING_TOOL;
    e.llm_started_at_ms = now_ms(); // reuse for tool timing
    e.tool_calls++;
}

void AgentScheduler::mark_completed(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto& e = get_or_create(agent_id);

    // Record final wait time
    if (e.llm_started_at_ms > 0) {
        uint64_t elapsed = now_ms() - e.llm_started_at_ms;
        if (e.state == AgentScheduleState::WAITING_LLM) {
            e.total_llm_wait_ms += elapsed;
        } else if (e.state == AgentScheduleState::WAITING_TOOL) {
            e.total_tool_wait_ms += elapsed;
        }
    }

    e.state = AgentScheduleState::COMPLETED;
}

void AgentScheduler::mark_idle(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    auto& e = get_or_create(agent_id);
    e.state = AgentScheduleState::IDLE;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

AgentScheduleState AgentScheduler::get_state(uint32_t agent_id) const {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(agent_id);
    if (it == entries_.end()) return AgentScheduleState::IDLE;
    return it->second.state;
}

std::optional<SchedulerEntry> AgentScheduler::get_entry(uint32_t agent_id) const {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(agent_id);
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

std::optional<uint32_t> AgentScheduler::next_ready() const {
    std::lock_guard lock(mutex_);

    const SchedulerEntry* best = nullptr;
    for (const auto& [_, e] : entries_) {
        if (e.state != AgentScheduleState::READY) continue;
        if (!best ||
            static_cast<uint8_t>(e.priority) < static_cast<uint8_t>(best->priority) ||
            (e.priority == best->priority && e.queued_at_ms < best->queued_at_ms)) {
            best = &e;
        }
    }

    if (best) return best->agent_id;
    return std::nullopt;
}

std::vector<uint32_t> AgentScheduler::agents_in_state(AgentScheduleState state) const {
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> result;
    for (const auto& [id, e] : entries_) {
        if (e.state == state) result.push_back(id);
    }
    return result;
}

uint32_t AgentScheduler::llm_queue_depth() const {
    std::lock_guard lock(mutex_);
    uint32_t count = 0;
    for (const auto& [_, e] : entries_) {
        if (e.state == AgentScheduleState::WAITING_LLM ||
            e.state == AgentScheduleState::READY) {
            count++;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

SchedulerStats AgentScheduler::stats() const {
    std::lock_guard lock(mutex_);
    SchedulerStats s;
    s.total_agents = static_cast<uint32_t>(entries_.size());

    for (const auto& [_, e] : entries_) {
        switch (e.state) {
            case AgentScheduleState::READY:        s.agents_ready++; break;
            case AgentScheduleState::WAITING_LLM:  s.agents_waiting_llm++; break;
            case AgentScheduleState::WAITING_TOOL: s.agents_waiting_tool++; break;
            case AgentScheduleState::IDLE:         s.agents_idle++; break;
            default: break;
        }
        s.total_llm_calls += e.llm_calls;
        s.total_tool_calls += e.tool_calls;
    }
    return s;
}

nlohmann::json AgentScheduler::stats_json() const {
    auto s = stats();
    return {
        {"total_agents", s.total_agents},
        {"agents_ready", s.agents_ready},
        {"agents_waiting_llm", s.agents_waiting_llm},
        {"agents_waiting_tool", s.agents_waiting_tool},
        {"agents_idle", s.agents_idle},
        {"total_llm_calls", s.total_llm_calls},
        {"total_tool_calls", s.total_tool_calls},
    };
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void AgentScheduler::remove(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    entries_.erase(agent_id);
}

SchedulerEntry& AgentScheduler::get_or_create(uint32_t agent_id) {
    auto& e = entries_[agent_id];
    if (e.agent_id == 0) {
        e.agent_id = agent_id;
    }
    return e;
}

} // namespace clove
