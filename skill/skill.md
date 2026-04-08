---
name: clove
description: >
  Use CLOVE — the AI agent kernel — to spawn agents, run jobs, manage memory,
  orchestrate swarms, call MCP tools, and interact with the kernel API.
  Use this skill whenever the user wants to run an agent, submit a job, store
  memory, check agent status, launch a swarm, use a daemon, create a world,
  or interact with the CLOVE kernel in any way — even if they don't say "CLOVE"
  explicitly. Also activate when the user asks about agents running, what's in
  memory, what jobs are queued, or wants Claude to DO something autonomously.
allowed-tools: Bash(curl:*)
---

# CLOVE Agent Kernel

CLOVE is a C++ agent kernel running on Railway. It exposes 65+ HTTP endpoints
and an MCP server. You have both available in this session.

**MCP server**: Connected as `mcp__clove__*` tools — use these first.
**Kernel API**: `https://kernel-production-96de.up.railway.app` — use curl for
anything not covered by MCP tools.

---

## Quick reference — most-used MCP tools

```
clove_status          — kernel health, uptime, syscall count, cost
clove_run             — run a single agent synchronously (goal + model + budget)
clove_submit_job      — async job with depends_on chaining
clove_list_jobs       — see queue state
clove_get_job         — poll a specific job
clove_spawn_agent     — start a long-lived agent process
clove_list_agents     — see running agents
clove_kill_agent      — stop an agent
clove_define_agent    — create a named reusable agent definition
clove_run_agent_def   — fire a named agent at a goal
clove_audit           — every action every agent took
remember              — write a memory block (persists across runs)
recall                — read memory blocks (all agents share these)
clove_mcp_servers     — see what MCP servers the kernel has registered
clove_mcp_tools       — list all tools available through the kernel bridge
clove_call_mcp_tool   — invoke a kernel MCP tool (GitHub, Slack, Linear, etc.)
```

---

## Memory system

Three types, three access levels:

| Type | Behavior |
|------|----------|
| `system` | Injected into EVERY agent context automatically on boot |
| `core` | Always loaded for agents that request memory |
| `recall` | Fetched on demand by name/query |

| Access | Who can read |
|--------|-------------|
| `private` | Only the writing agent |
| `shared_read` | All agents can read |
| `shared_readwrite` | All agents can read and update |

**Pattern**: Store project-level facts as `system/shared_read` so every agent
you ever spawn already knows the context without being told.

```
remember(name, content, type="core", access="shared_read")
recall(query?)            — returns all visible blocks, filter by query
clove_write_memory(...)   — update a block by ID
clove_delete_memory(id)   — remove a block
clove_store_set(key, val) — flat KV store (no types, global scope)
clove_store_get(key)      — read flat KV
```

---

## Job pipeline

Jobs are async, chainable, and cascade on failure.

```json
{
  "goal": "...",
  "agent_name": "...",
  "workspace_id": "...",
  "model": "claude-sonnet-4-6",
  "budget_usd": 0.5,
  "max_steps": 20,
  "allowed_tools": ["mcp_call", "store"],
  "depends_on": "<job_id>"   ← only runs after this job succeeds
}
```

**Workflow**:
1. `clove_submit_job` → get job_id
2. `clove_get_job(job_id)` — poll until status is `completed` or `failed`
3. `clove_run_history` — see full execution trace
4. `clove_audit` — see every tool call, every step

---

## Multi-agent patterns

### Swarms — parallel agents sharing a goal
```
clove_create_swarm(name, goal, agents[], budget)
clove_start_swarm(swarm_id)
clove_list_swarms()
```

### Daemons — always-on tick-based agents
```
clove_list_daemons()
clove_start_daemon(name)
clove_stop_daemon(name)
clove_daemon_logs(name, limit?)
clove_daemon_dream(name)   ← consolidate episodic memory into long-term
```

### Worlds — isolated multi-agent environments
```
clove_create_world(name, description)
clove_launch_world(world_id)
clove_add_to_world(world_id, agent_id)
clove_world_agents(world_id)
```

---

## Kernel MCP bridge

The kernel proxies MCP servers (GitHub, Slack, Linear, Filesystem) for agents.
Check what's available, then call through:

```
clove_mcp_servers()              — list connected servers
clove_mcp_tools()                — list all tools across all servers
clove_call_mcp_tool(
  server,    — e.g. "github"
  tool,      — e.g. "create_issue"
  args       — tool-specific args object
)
```

To add MCP servers: set `GITHUB_TOKEN`, `SLACK_BOT_TOKEN`, `LINEAR_API_KEY`
in the kernel Railway service env vars — they auto-register on boot.

---

## Execution rules

1. **Check kernel status first** if something seems off: `clove_status`
2. **Use MCP tools over curl** — they're typed, tracked, and audited
3. **Use `system` memory** for facts that should survive across all sessions
4. **Poll with `clove_get_job`** — don't assume a job finished; check status
5. **Check `clove_audit`** when debugging — it shows every step an agent took
6. **Budget conservatively** — start at $0.10–$0.50 per job; increase if needed

---

## Composition patterns

**Run a quick one-shot agent:**
```
clove_run(goal, model, budget) → result
```

**Async job with follow-up:**
```
clove_submit_job(goal, ...) → job_id
clove_get_job(job_id)       → poll until done
clove_audit()               → inspect what happened
```

**Chained pipeline (e.g. research → write → review):**
```
job1 = clove_submit_job("research X")
job2 = clove_submit_job("write report on X", depends_on=job1.id)
job3 = clove_submit_job("review report", depends_on=job2.id)
```

**Store context for future agents:**
```
remember("project-context", "This repo is...", type="system", access="shared_read")
```

**Debug a failed agent:**
```
clove_audit(limit=50, category="llm")
clove_run_history()
clove_get_job(job_id)
```
