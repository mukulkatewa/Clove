#include <catch2/catch_test_macros.hpp>
#include <clove/agent_scheduler.hpp>

using namespace clove;

// ---------------------------------------------------------------------------
// String conversions
// ---------------------------------------------------------------------------

TEST_CASE("AgentPriority string conversion", "[scheduler]") {
    REQUIRE(std::string(agent_priority_to_string(AgentPriority::CRITICAL)) == "critical");
    REQUIRE(std::string(agent_priority_to_string(AgentPriority::NORMAL)) == "normal");
    REQUIRE(std::string(agent_priority_to_string(AgentPriority::IDLE)) == "idle");

    REQUIRE(agent_priority_from_string("critical") == AgentPriority::CRITICAL);
    REQUIRE(agent_priority_from_string("high") == AgentPriority::HIGH);
    REQUIRE(agent_priority_from_string("invalid") == AgentPriority::NORMAL); // default
}

TEST_CASE("AgentScheduleState string conversion", "[scheduler]") {
    REQUIRE(std::string(schedule_state_to_string(AgentScheduleState::IDLE)) == "idle");
    REQUIRE(std::string(schedule_state_to_string(AgentScheduleState::READY)) == "ready");
    REQUIRE(std::string(schedule_state_to_string(AgentScheduleState::WAITING_LLM)) == "waiting_llm");
    REQUIRE(std::string(schedule_state_to_string(AgentScheduleState::WAITING_TOOL)) == "waiting_tool");
}

// ---------------------------------------------------------------------------
// Priority management
// ---------------------------------------------------------------------------

TEST_CASE("AgentScheduler default priority is NORMAL", "[scheduler]") {
    AgentScheduler sched;
    REQUIRE(sched.get_priority(1) == AgentPriority::NORMAL);
}

TEST_CASE("AgentScheduler set and get priority", "[scheduler]") {
    AgentScheduler sched;
    sched.set_priority(1, AgentPriority::CRITICAL);
    REQUIRE(sched.get_priority(1) == AgentPriority::CRITICAL);

    sched.set_priority(1, AgentPriority::LOW);
    REQUIRE(sched.get_priority(1) == AgentPriority::LOW);
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

TEST_CASE("AgentScheduler state transitions", "[scheduler]") {
    AgentScheduler sched;

    REQUIRE(sched.get_state(1) == AgentScheduleState::IDLE);

    sched.mark_ready(1);
    REQUIRE(sched.get_state(1) == AgentScheduleState::READY);

    sched.mark_waiting_llm(1);
    REQUIRE(sched.get_state(1) == AgentScheduleState::WAITING_LLM);

    sched.mark_waiting_tool(1);
    REQUIRE(sched.get_state(1) == AgentScheduleState::WAITING_TOOL);

    sched.mark_ready(1);
    REQUIRE(sched.get_state(1) == AgentScheduleState::READY);

    sched.mark_completed(1);
    REQUIRE(sched.get_state(1) == AgentScheduleState::COMPLETED);
}

// ---------------------------------------------------------------------------
// Priority queue ordering
// ---------------------------------------------------------------------------

TEST_CASE("AgentScheduler next_ready returns highest priority", "[scheduler]") {
    AgentScheduler sched;

    sched.set_priority(1, AgentPriority::LOW);
    sched.set_priority(2, AgentPriority::CRITICAL);
    sched.set_priority(3, AgentPriority::NORMAL);

    sched.mark_ready(1);
    sched.mark_ready(2);
    sched.mark_ready(3);

    // Should return agent 2 (CRITICAL) first
    auto next = sched.next_ready();
    REQUIRE(next.has_value());
    REQUIRE(*next == 2);
}

TEST_CASE("AgentScheduler FIFO within same priority", "[scheduler]") {
    AgentScheduler sched;

    sched.set_priority(1, AgentPriority::NORMAL);
    sched.set_priority(2, AgentPriority::NORMAL);
    sched.set_priority(3, AgentPriority::NORMAL);

    sched.mark_ready(3); // queued first
    sched.mark_ready(1); // queued second
    sched.mark_ready(2); // queued third

    // Should return agent 3 (first queued)
    auto next = sched.next_ready();
    REQUIRE(next.has_value());
    REQUIRE(*next == 3);
}

TEST_CASE("AgentScheduler next_ready returns nullopt when none ready", "[scheduler]") {
    AgentScheduler sched;

    sched.mark_waiting_llm(1);
    sched.mark_waiting_tool(2);

    auto next = sched.next_ready();
    REQUIRE_FALSE(next.has_value());
}

// ---------------------------------------------------------------------------
// Stats tracking
// ---------------------------------------------------------------------------

TEST_CASE("AgentScheduler tracks LLM and tool calls", "[scheduler]") {
    AgentScheduler sched;

    sched.mark_waiting_llm(1); // llm_calls = 1
    sched.mark_ready(1);
    sched.mark_waiting_llm(1); // llm_calls = 2
    sched.mark_ready(1);
    sched.mark_waiting_tool(1); // tool_calls = 1

    auto entry = sched.get_entry(1);
    REQUIRE(entry.has_value());
    REQUIRE(entry->llm_calls == 2);
    REQUIRE(entry->tool_calls == 1);
}

TEST_CASE("AgentScheduler stats", "[scheduler]") {
    AgentScheduler sched;

    sched.mark_ready(1);
    sched.mark_waiting_llm(2);
    sched.mark_waiting_tool(3);
    sched.mark_idle(4);

    auto s = sched.stats();
    REQUIRE(s.total_agents == 4);
    REQUIRE(s.agents_ready == 1);
    REQUIRE(s.agents_waiting_llm == 1);
    REQUIRE(s.agents_waiting_tool == 1);
    REQUIRE(s.agents_idle == 1);
}

TEST_CASE("AgentScheduler stats_json", "[scheduler]") {
    AgentScheduler sched;
    sched.mark_ready(1);

    auto j = sched.stats_json();
    REQUIRE(j["total_agents"] == 1);
    REQUIRE(j["agents_ready"] == 1);
}

// ---------------------------------------------------------------------------
// Agent queries
// ---------------------------------------------------------------------------

TEST_CASE("AgentScheduler agents_in_state", "[scheduler]") {
    AgentScheduler sched;
    sched.mark_ready(1);
    sched.mark_ready(2);
    sched.mark_waiting_llm(3);

    auto ready = sched.agents_in_state(AgentScheduleState::READY);
    REQUIRE(ready.size() == 2);

    auto waiting = sched.agents_in_state(AgentScheduleState::WAITING_LLM);
    REQUIRE(waiting.size() == 1);
    REQUIRE(waiting[0] == 3);
}

TEST_CASE("AgentScheduler llm_queue_depth", "[scheduler]") {
    AgentScheduler sched;
    sched.mark_ready(1);
    sched.mark_ready(2);
    sched.mark_waiting_llm(3);

    // READY + WAITING_LLM = 3
    REQUIRE(sched.llm_queue_depth() == 3);
}

TEST_CASE("AgentScheduler remove", "[scheduler]") {
    AgentScheduler sched;
    sched.mark_ready(1);
    sched.set_priority(1, AgentPriority::HIGH);

    sched.remove(1);

    REQUIRE(sched.get_state(1) == AgentScheduleState::IDLE); // fresh default
    REQUIRE(sched.get_priority(1) == AgentPriority::NORMAL); // fresh default
}
