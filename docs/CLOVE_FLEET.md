# CLOVE Fleet — Parallel Agent Execution

> Run N agents in parallel on a single goal. The kernel orchestrates, streams events, and synthesizes results.

## How It Works

```
POST /api/fleet
{
  "goal": "Research the AI agent market in 2026",
  "agents": 5,
  "budget": 2.00,
  "model": "anthropic/claude-sonnet-4"
}
```

The kernel:
1. Creates N RunEngine instances, one per agent
2. Runs them in parallel threads
3. Each agent gets `budget / N` cost cap
4. Streams SSE events as agents work
5. When all agents finish, synthesizes their outputs into one result
6. Stores the synthesis as an artifact

## SSE Event Stream

The response is `text/event-stream`:

```
data: {"type":"fleet_start","data":{"agent_count":5,"total_budget_usd":2.0}}

data: {"type":"agent_event","data":{"agent":"agent-1","event_type":"tool_call","tool":"search"}}
data: {"type":"agent_event","data":{"agent":"agent-3","event_type":"tool_call","tool":"search"}}
data: {"type":"agent_event","data":{"agent":"agent-2","event_type":"thinking","step":1}}

data: {"type":"agent_done","data":{"agent":"agent-1","success":true,"cost_usd":0.12,"completed":1,"total":5}}
data: {"type":"agent_done","data":{"agent":"agent-3","success":true,"cost_usd":0.09,"completed":2,"total":5}}
...

data: {"type":"synthesizing","data":{"message":"Synthesizing outputs from all agents..."}}

data: {"type":"fleet_done","data":{"success":true,"agent_count":5,"total_cost_usd":0.87,"total_tokens":4200}}
```

## Request Parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `goal` | string | required | What the fleet should accomplish |
| `agents` | integer | 3 | Number of parallel agents (max 20) |
| `budget` | number | 2.0 | Total budget in USD (split equally across agents) |
| `model` | string | kernel default | LLM model to use |
| `max_steps` | integer | 15 | Max tool-calling steps per agent |
| `tools` | string[] | all | Allowed tools (read_file, write_file, exec, search, http, store, fetch, remember, recall, mcp_*) |

## What Each Agent Can Do

Every agent in the fleet has access to real tools:

| Tool | What it does |
|------|-------------|
| `search` | Real web search via DuckDuckGo + LLM extraction |
| `read_file` | Read real files from disk |
| `write_file` | Write real files to disk |
| `exec` | Execute real shell commands |
| `http` | Make real HTTP requests (CURL) |
| `store` / `fetch` | Kernel KV store |
| `remember` / `recall` | Persistent memory blocks |
| `mcp_*` | Any connected MCP tool |

## Agent Coordination

Agents in a fleet currently run independently on the same goal. The kernel synthesizes their outputs after all complete.

Future: agents will coordinate via kernel IPC (SYS_SEND/SYS_RECV) and shared memory blocks (SYS_MEM_SHARE), enabling pipeline workflows where agent outputs feed into other agents.

## Cost Control

- Each agent gets `budget / agent_count` as its hard cap
- If an agent exceeds its budget, the kernel stops its tool-calling loop
- The synthesis step has its own cost (one additional LLM call)
- `fleet_done` event reports exact total cost

## Example: 3-Agent Research Fleet

```bash
curl -N -X POST localhost:8080/api/fleet \
  -H "Content-Type: application/json" \
  -d '{
    "goal": "Research the current state of quantum computing",
    "agents": 3,
    "budget": 1.00
  }'
```

Result: 3 agents search in parallel, each finds different angles, kernel synthesizes into a comprehensive report. Total cost: ~$0.001. Total time: ~15s (parallel, not 3x sequential).

## Scaling

The kernel's RunEngine is ~320 LOC C++. Each agent runs in its own thread. Memory overhead per agent is minimal (~3MB). A single kernel instance can comfortably run 20+ parallel agents on a modern machine.

| Agents | RAM overhead | Typical wall time | Typical cost |
|--------|-------------|-------------------|--------------|
| 1 | ~3MB | 10-30s | $0.001-0.01 |
| 3 | ~9MB | 10-30s | $0.003-0.03 |
| 5 | ~15MB | 15-40s | $0.005-0.05 |
| 10 | ~30MB | 20-60s | $0.01-0.10 |
| 20 | ~60MB | 30-90s | $0.02-0.20 |

Compare: CrewAI/AutoGen struggle above 4 agents due to Python GIL and per-agent memory overhead (~200-500MB each).
