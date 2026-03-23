#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace clove {

// ---------------------------------------------------------------------------
// Agent Priority (lower value = higher priority)
// ---------------------------------------------------------------------------
enum class AgentPriority : uint8_t {
    CRITICAL = 0,   // User-facing, interactive queries
    HIGH     = 1,   // Time-sensitive tasks
    NORMAL   = 2,   // Standard background work (default)
    LOW      = 3,   // Batch processing, summarization
    IDLE     = 4,   // Only runs when no other work
};

const char* agent_priority_to_string(AgentPriority p);
AgentPriority agent_priority_from_string(const std::string& s);

// ---------------------------------------------------------------------------
// Agent Schedule State
// ---------------------------------------------------------------------------
enum class AgentScheduleState : uint8_t {
    IDLE         = 0,  // Not actively scheduled
    READY        = 1,  // Queued, waiting for a worker
    WAITING_LLM  = 2,  // Blocked on LLM inference
    WAITING_TOOL = 3,  // Blocked on tool call (exec/http)
    COMPLETED    = 4,  // Task finished
};

const char* schedule_state_to_string(AgentScheduleState s);

// ---------------------------------------------------------------------------
// Scheduler Entry (per-agent tracking)
// ---------------------------------------------------------------------------
struct SchedulerEntry {
    uint32_t agent_id = 0;
    AgentPriority priority = AgentPriority::NORMAL;
    AgentScheduleState state = AgentScheduleState::IDLE;
    uint64_t queued_at_ms = 0;       // when entered READY state
    uint64_t llm_started_at_ms = 0;  // when LLM call started
    uint64_t total_llm_wait_ms = 0;  // cumulative LLM wait time
    uint64_t total_tool_wait_ms = 0; // cumulative tool wait time
    uint32_t llm_calls = 0;          // number of LLM calls
    uint32_t tool_calls = 0;         // number of tool calls
};

// ---------------------------------------------------------------------------
// Scheduler Stats
// ---------------------------------------------------------------------------
struct SchedulerStats {
    uint32_t total_agents = 0;
    uint32_t agents_ready = 0;
    uint32_t agents_waiting_llm = 0;
    uint32_t agents_waiting_tool = 0;
    uint32_t agents_idle = 0;
    uint64_t total_llm_calls = 0;
    uint64_t total_tool_calls = 0;
};

// ---------------------------------------------------------------------------
// AgentScheduler
// ---------------------------------------------------------------------------
class AgentScheduler {
public:
    // --- Priority management ---
    void set_priority(uint32_t agent_id, AgentPriority priority);
    AgentPriority get_priority(uint32_t agent_id) const;

    // --- State transitions ---
    void mark_ready(uint32_t agent_id);
    void mark_waiting_llm(uint32_t agent_id);
    void mark_waiting_tool(uint32_t agent_id);
    void mark_completed(uint32_t agent_id);
    void mark_idle(uint32_t agent_id);

    // --- Query ---
    AgentScheduleState get_state(uint32_t agent_id) const;
    std::optional<SchedulerEntry> get_entry(uint32_t agent_id) const;

    // --- Priority-ordered queue ---
    // Returns the highest-priority READY agent (lowest priority value, then oldest)
    std::optional<uint32_t> next_ready() const;

    // Get all agents at a given state
    std::vector<uint32_t> agents_in_state(AgentScheduleState state) const;

    // Count agents waiting for LLM (to decide if we should yield)
    uint32_t llm_queue_depth() const;

    // --- Stats ---
    SchedulerStats stats() const;
    nlohmann::json stats_json() const;

    // --- Cleanup ---
    void remove(uint32_t agent_id);

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, SchedulerEntry> entries_;

    SchedulerEntry& get_or_create(uint32_t agent_id);

    static uint64_t now_ms() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }
};

} // namespace clove
