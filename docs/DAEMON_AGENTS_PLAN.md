# CLOVE Daemon Agents — Always-On Agent Architecture

> Inspired by Anthropic's KAIROS. Built to be runtime-agnostic, scalable, and observable.
> Status: Planning → Ready to build

---

## What We're Building

Always-on daemon agents that watch, remember, and act — not just when triggered, but continuously. Like hiring an employee who works 24/7 for pennies.

**KAIROS does this locked to Claude. CLOVE does it across ANY runtime.**

---

## Architecture

### 1. Agent Daemon Loop (The Tick System)

Every daemon agent runs a loop:

```
while agent.enabled:
    tick()           # Check: is there anything worth doing?
    sleep(interval)  # Default: 60 seconds, configurable per agent
```

Each tick:
1. **Observe** — check subscriptions (GitHub events, Sentry alerts, file changes, queue depth)
2. **Decide** — is there something worth acting on? (LLM call with context, or rule-based)
3. **Act** — if yes, execute within the 15-second budget. If action needs more time, spawn a background task.
4. **Log** — record what happened (or didn't) in the agent's daily log

**Blocking budget**: 15 seconds per tick. If an action exceeds this:
- Spawn it as a background task in the agent's workspace
- Return to the tick loop immediately
- Background task writes results to workspace state when done

### 2. Memory Consolidation (The Dream Engine)

Agents accumulate memory from every run. Over time, memory gets stale and contradictory.

**Dream cycle triggers** (all must be true):
- 24+ hours since last consolidation
- 5+ runs completed since last consolidation
- Agent is idle (not mid-task)

**Four phases:**

| Phase | What it does | How |
|-------|-------------|-----|
| **Orient** | Survey current memory state | Read all memory blocks, count entries, check freshness |
| **Curate** | Fix time references | Convert "recently" → "on 2026-04-03", remove expired entries |
| **Consolidate** | Resolve contradictions | LLM call: "these memories contradict, which is current?" |
| **Prune** | Trim and reindex | Delete stale entries, update index, cap at budget |

**Cost**: ~$0.02-0.05 per consolidation (one LLM call with memory context)

### 3. Daemon Lifecycle Management

```
clove daemon start <agent-name>     # Start agent as background daemon
clove daemon stop <agent-name>      # Graceful stop
clove daemon list                   # Show all running daemons
clove daemon logs <agent-name>      # Stream daemon logs
clove daemon restart <agent-name>   # Stop + start
```

**Dashboard equivalent:**
- Agents page → agent card shows "daemon" badge if always-on
- Toggle: one-click start/stop daemon mode
- Logs tab: live streaming daemon output

### 4. Subscriptions (Reactive Watching)

Instead of just cron + webhook triggers, daemon agents can **subscribe** to event streams:

| Subscription Type | What it watches | Example |
|------------------|----------------|---------|
| `github.events` | PR opens, pushes, comments | "Watch for any PR that touches auth/" |
| `sentry.alerts` | Error spikes, new issues | "Alert me on any P0 incident" |
| `file.watch` | Local file changes | "Rebuild docs when src/ changes" |
| `queue.depth` | Internal task queue | "Scale up when queue > 10" |
| `schedule.tick` | Periodic heartbeat | "Check system health every 5 min" |
| `agent.output` | Another agent finishes | "When researcher is done, start writer" |
| `mcp.event` | Any MCP service event | "Watch Linear for new issues" |

**Implementation**: Each subscription type is a watcher that runs in the tick loop. On each tick, the agent checks all its subscriptions for new events.

### 5. Daily Logs (Append-Only Transcript)

Each daemon agent writes a daily log:

```
~/.clove/daemons/<agent-name>/logs/2026-04-03.jsonl
```

Each line:
```json
{"ts": "2026-04-03T06:00:12Z", "type": "tick", "action": "none", "reason": "no new events"}
{"ts": "2026-04-03T06:01:14Z", "type": "tick", "action": "scan", "detail": "checking deps for CVEs", "cost": 0.02}
{"ts": "2026-04-03T06:01:28Z", "type": "result", "detail": "found CVE-2026-1834 in express@4.19.2", "cost": 0.08}
{"ts": "2026-04-03T06:01:45Z", "type": "action", "detail": "opened PR #312 with fix", "cost": 0.02}
```

**Dashboard**: Audit page shows daemon logs. Agent detail → Logs tab shows live stream.

---

## Data Model

### Agent Definition (extended)

```typescript
interface AgentDef {
  name: string
  description: string
  enabled: boolean

  // Existing
  runtime: 'clove' | 'claude-code' | 'codex' | 'openclaw'
  model: string
  tools: string[]
  connections: string[]
  budget: { per_run: number; daily_max: number }
  sandbox: { can_exec: boolean; can_http: boolean; can_write: boolean }

  // NEW: Daemon config
  daemon: {
    enabled: boolean                    // Is this an always-on agent?
    tick_interval_s: number             // Seconds between ticks (default: 60)
    blocking_budget_ms: number          // Max ms per tick action (default: 15000)
    subscriptions: Subscription[]       // What to watch
    dream: {
      enabled: boolean                  // Auto memory consolidation?
      min_hours_between: number         // Default: 24
      min_runs_between: number          // Default: 5
    }
  }

  // Existing
  jobs: Job[]
  triggers: Trigger[]
}

interface Subscription {
  type: 'github.events' | 'sentry.alerts' | 'file.watch' | 'queue.depth' | 'schedule.tick' | 'agent.output' | 'mcp.event'
  config: Record<string, string>       // Type-specific config
  filter?: string                      // Optional filter expression
}
```

### Daemon State (in kernel)

```typescript
interface DaemonState {
  agent_name: string
  pid: number                          // OS process ID
  started_at: string                   // ISO timestamp
  last_tick_at: string
  ticks_total: number
  actions_today: number
  cost_today: number
  memory_last_consolidated: string     // ISO timestamp
  runs_since_consolidation: number
  status: 'running' | 'sleeping' | 'acting' | 'dreaming' | 'stopped'
  current_action?: string              // What it's doing right now
}
```

---

## Kernel Changes Needed

### 1. Daemon Manager (new)
`libs/api/include/clove/daemon_manager.hpp`

```
DaemonManager
  ├── start_daemon(agent_name) → spawns tick loop thread
  ├── stop_daemon(agent_name) → graceful shutdown
  ├── list_daemons() → all running daemons with state
  ├── get_daemon_state(agent_name) → current state
  └── get_daemon_logs(agent_name, date) → daily log entries
```

### 2. New API Endpoints
```
POST   /api/daemons/:name/start    → start daemon for agent
POST   /api/daemons/:name/stop     → stop daemon
GET    /api/daemons                 → list all running daemons
GET    /api/daemons/:name           → get daemon state
GET    /api/daemons/:name/logs      → get daemon logs (SSE stream)
POST   /api/daemons/:name/dream    → trigger manual memory consolidation
```

### 3. Tick Loop Implementation

```cpp
void DaemonManager::tick_loop(const std::string& agent_name) {
    auto agent = load_agent_def(agent_name);

    while (running_[agent_name]) {
        auto start = now();

        // 1. Check subscriptions
        auto events = check_subscriptions(agent);

        // 2. Decide
        if (!events.empty()) {
            // Run agent with events as context
            auto cfg = build_run_config(agent, events);
            cfg.max_duration_ms = agent.daemon.blocking_budget_ms;

            // 3. Act (within budget)
            auto result = run_with_timeout(cfg);

            // 4. Log
            append_daily_log(agent_name, result);
            update_daemon_state(agent_name, result);
        }

        // Check if dream cycle needed
        maybe_dream(agent_name);

        // Sleep until next tick
        auto elapsed = now() - start;
        auto sleep_ms = agent.daemon.tick_interval_s * 1000 - elapsed;
        if (sleep_ms > 0) sleep_for(sleep_ms);
    }
}
```

### 4. Dream Engine Implementation

```cpp
void DaemonManager::dream(const std::string& agent_name) {
    set_status(agent_name, "dreaming");

    // Load all memory for this agent
    auto memories = memory_store_.search(agent_name, "", 100);

    // Phase 1: Orient
    auto summary = llm_call("Survey these memories. Count entries, note staleness.");

    // Phase 2: Curate
    auto curated = llm_call("Fix time references. Convert relative to absolute dates.");

    // Phase 3: Consolidate
    auto consolidated = llm_call("Resolve contradictions. Keep the most recent truth.");

    // Phase 4: Prune
    auto pruned = llm_call("Remove stale entries. Rebuild index. Cap at 50 entries.");

    // Apply changes
    apply_memory_changes(agent_name, pruned);

    update_consolidation_timestamp(agent_name);
    set_status(agent_name, "running");
}
```

---

## Dashboard Changes

### Agents Page
- New toggle in agent card: **"Always-On"** with daemon badge
- Daemon config in Config tab: tick interval, blocking budget, subscriptions
- Status shows: `running | sleeping | acting | dreaming` for daemon agents
- Live tick counter in agent detail

### New: Daemon Status Widget (Overview page)
- Count of running daemons
- Total ticks today
- Total actions today
- Total daemon cost today
- "Dreaming" indicator when consolidation running

### Swarm Page Enhancement
- Daemon agents show as persistent nodes (don't disappear)
- Pulsing glow on tick
- "Zzz" indicator when sleeping between ticks
- "Dream" indicator during consolidation

### Jobs & Pipelines Page
- Subscription-triggered jobs show "always-on" badge
- Live indicator showing last tick time

---

## CLI Commands

```bash
# Daemon management
clove daemon start pr-reviewer          # Start as always-on daemon
clove daemon stop pr-reviewer           # Graceful stop
clove daemon list                       # Show all daemons
clove daemon logs pr-reviewer           # Stream logs
clove daemon logs pr-reviewer --date 2026-04-03  # Historical
clove daemon dream pr-reviewer          # Manual memory consolidation

# Agent definition with daemon config
clove agent create pr-reviewer \
  --runtime claude-code \
  --model claude-sonnet-4 \
  --daemon \
  --tick-interval 60 \
  --subscribe "github.events:pull_request.opened" \
  --subscribe "schedule.tick:*/5 * * * *"
```

---

## Build Order

### Phase 1: Daemon Loop (kernel)
1. `daemon_manager.hpp/.cpp` — DaemonManager class with start/stop/list
2. Tick loop with simple schedule-based subscriptions
3. API endpoints for daemon CRUD
4. Daily log file writer
5. CLI commands: `clove daemon start/stop/list/logs`

### Phase 2: Subscriptions (kernel)
6. GitHub event subscription (via MCP polling)
7. File watch subscription (inotify/kqueue)
8. Agent output subscription (internal event bus)
9. Queue depth subscription
10. MCP event subscription (generic)

### Phase 3: Dream Engine (kernel)
11. Memory consolidation logic
12. Auto-dream trigger in tick loop
13. Manual dream API endpoint
14. Dream cost tracking

### Phase 4: Dashboard
15. Agents page: daemon toggle, config, status
16. Overview: daemon status widget
17. Swarm: persistent daemon nodes
18. Jobs page: always-on badge

---

## Why CLOVE's Version is Better Than KAIROS

| Feature | KAIROS | CLOVE Daemons |
|---------|--------|---------------|
| Runtime | Claude only | Claude Code + Codex + OpenClaw + CLOVE |
| Interface | Terminal only | Dashboard + CLI + MCP + API |
| Scaling | Single instance | Scale to N instances per agent |
| Observation | Logs only | Live swarm view + audit + cost tracking |
| Isolation | Feature flags | Kernel-level workspace sandboxing |
| Memory | 3-layer local | 3-tier with shared workspace state |
| Subscriptions | GitHub only | GitHub, Sentry, Slack, Linear, files, queues, MCP |
| Cost control | None mentioned | Per-tick budget, daily caps, kill switches |
| Multi-agent | Coordinator mode | Native fleet/swarm orchestration |
| Open source | Leaked, not official | Fully open, MIT licensed |

---

## Cost Estimates

| Activity | Cost per occurrence | Daily cost (typical) |
|----------|-------------------|---------------------|
| Tick (observe only) | ~$0.001 | ~$1.44 (1440 ticks/day) |
| Tick with action | ~$0.02-0.10 | ~$0.50 (25 actions/day) |
| Dream consolidation | ~$0.03-0.05 | ~$0.05 (once/day) |
| **Total per daemon** | | **~$2.00/day** |

A full fleet of 5 always-on daemons: ~$10/day = ~$300/month. Replaces a team that costs $50K+/month.
