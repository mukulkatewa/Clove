# CLOVE v2 — Scheduler Design

> Status: **Implemented** | Date: 2026-03-23
> Based on: Autellix (4-15x), Astraea (25.5% JCT reduction), Helium (1.56x),
> Cortex, Continuum, Agent-OS Blueprint

## Problem

CLOVE's current `LlmQueue` is a simple 8-worker thread pool with FIFO scheduling.
Every `SYS_THINK` call blocks a worker until the LLM response arrives. This means:

- 8 concurrent LLM calls max, regardless of how many agents are running
- No priority differentiation — a low-priority log summarizer blocks a high-priority user query
- No state awareness — agents waiting for tool results still hold a worker slot
- No preemption — a stuck agent ties up resources until timeout
- No KV cache awareness — cache thrashing when switching between agents

## Design: State-Aware Agent Scheduler

### Agent States

From Astraea research: agents have distinct I/O states that require different scheduling.

```
┌──────────┐     SYS_THINK      ┌──────────────┐
│  READY   │ ──────────────────→ │ WAITING_LLM  │
│          │                     │              │
│ (queued, │     SYS_EXEC/      │ (blocked on  │
│  can run)│     SYS_HTTP        │  inference)  │
└──────────┘ ──────────────────→ └──────────────┘
      ↑      │                         │
      │      ↓                         │
      │ ┌──────────────┐               │
      │ │ WAITING_TOOL │               │
      │ │              │               │
      │ │ (blocked on  │               │
      │ │  exec/http)  │               │
      │ └──────────────┘               │
      │      │                         │
      └──────┴─────────────────────────┘
              response received
```

### Scheduling Rules

| Agent State | Scheduling Decision |
|-------------|-------------------|
| READY | Can be scheduled on any worker. Priority determines order. |
| WAITING_LLM | Worker is occupied. Cannot schedule other work on this LLM slot. |
| WAITING_TOOL | **Release the LLM worker.** Tool I/O is async. Schedule another agent. |
| PAUSED | Do not schedule. Resume moves to READY. |
| BUDGET_EXCEEDED | Kill the agent. Reclaim all resources. |

**Key insight:** When an agent calls `SYS_EXEC` or `SYS_HTTP`, it doesn't need an LLM worker.
Release the worker immediately and re-queue the agent as READY when the tool returns.

### Priority Levels

```cpp
enum class AgentPriority : uint8_t {
    CRITICAL  = 0,  // User-facing, interactive queries
    HIGH      = 1,  // Time-sensitive tasks
    NORMAL    = 2,  // Standard background work
    LOW       = 3,  // Batch processing, summarization
    IDLE      = 4,  // Only runs when no other work
};
```

Priorities assigned per-agent via permissions or manifest. Higher priority agents
preempt lower priority ones when all workers are busy.

### Preemption

When a CRITICAL agent needs a worker but all are busy:
1. Find the lowest-priority WAITING_LLM agent
2. If its priority < the new agent's priority:
   - Save its context (chain_id, current prompt)
   - Cancel its pending LLM request
   - Move it back to READY queue
   - Give the worker to the higher-priority agent
3. If no lower-priority agent exists, queue the new agent (FIFO within priority)

### Four-Budget Enforcement

Integrated into the scheduler. Checked before every dispatch.

```cpp
struct AgentBudget {
    uint64_t max_tokens    = 0;  // 0 = unlimited
    uint32_t max_steps     = 0;  // max syscall count
    uint64_t max_time_ms   = 0;  // max wall-clock time
    double   max_cost_usd  = 0;  // max dollar spend

    // Tracked
    uint64_t tokens_used   = 0;
    uint32_t steps_taken   = 0;
    uint64_t started_at_ms = 0;
    double   cost_usd      = 0;
};
```

**Enforcement points:**
- Before every syscall dispatch: check step budget
- Before every SYS_THINK: check token budget (estimate) and cost budget
- On scheduler tick (100ms): check time budget for all running agents
- After every SYS_THINK response: update tokens_used and cost_usd

**On budget exceeded:**
- Log to audit (RESOURCE category)
- Emit AGENT_BUDGET_EXCEEDED event
- Set agent state to BUDGET_EXCEEDED
- If `kill_on_budget_exceeded` policy: send SIGTERM

### Proposed Syscalls

```
SYS_SET_BUDGET   = 0xF1   — Set budget for an agent (token, step, time, cost)
SYS_GET_BUDGET   = 0xF2   — Get current budget and usage
SYS_SET_PRIORITY = 0xF3   — Set agent priority
SYS_GET_PRIORITY = 0xF4   — Get agent priority
```

### Data Structures

```cpp
struct SchedulerEntry {
    uint32_t agent_id;
    AgentPriority priority;
    AgentScheduleState state;  // READY, WAITING_LLM, WAITING_TOOL, PAUSED
    AgentBudget budget;
    uint64_t queued_at_ms;     // for FIFO within same priority
};

class AgentScheduler {
public:
    // Submit a task for scheduling
    void submit(uint32_t agent_id, SyscallOp op);

    // Called by worker when it becomes free
    std::optional<SchedulerEntry> next();

    // State transitions
    void mark_waiting_llm(uint32_t agent_id);
    void mark_waiting_tool(uint32_t agent_id);
    void mark_ready(uint32_t agent_id);

    // Budget
    void set_budget(uint32_t agent_id, const AgentBudget& budget);
    bool check_budget(uint32_t agent_id, SyscallOp op);
    void record_usage(uint32_t agent_id, uint64_t tokens, double cost_usd);

    // Priority
    void set_priority(uint32_t agent_id, AgentPriority priority);

    // Tick (called every reactor iteration)
    void on_tick();  // checks time budgets, reaps stuck agents

private:
    std::mutex mutex_;
    // Priority queue: lower priority value = higher priority
    // Within same priority: FIFO by queued_at_ms
    std::priority_queue<SchedulerEntry, ..., PriorityComparator> ready_queue_;
    std::unordered_map<uint32_t, SchedulerEntry> agents_;
};
```

## Implementation Status

All items implemented (2026-03-23):
1. AgentBudget struct in libs/governance/include/clove/permissions.hpp
2. Budget enforcement in kernel/src/syscall_router.cpp (step + time check on every syscall)
3. 4 syscalls: SET_BUDGET (0xF1), GET_BUDGET (0xF2), SET_PRIORITY (0xF3), GET_PRIORITY (0xF4)
4. AgentScheduler class in libs/orchestration/src/agent_scheduler.cpp
5. State tracking wired into SYS_THINK (WAITING_LLM), SYS_HTTP/SYS_EXEC (WAITING_TOOL)
6. Priority queue with FIFO within same priority
7. Time budget check in reactor loop (kernel.cpp Kernel::run())

### LlmQueue retained
The original LlmQueue worker pool is kept for actual LLM execution. AgentScheduler is a layer above that manages priority and state. This avoids breaking the existing worker pool.
