# CLOVE

**An operating system for AI agents.** One binary. Agents get process isolation, shared memory, cost tracking, PII filtering, and a REST API. Run one agent or a fleet of 20 in parallel.

```
52ms startup  |  12.9 MB RAM  |  86 syscalls  |  71 API endpoints  |  165 tests  |  C++23
```

---

## Install

```bash
# Prerequisites (macOS)
xcode-select --install
brew install cmake openssl curl sqlite3

# Install (builds kernel automatically)
npm install -g @clove/cli

# Set your LLM key (free at https://openrouter.ai/keys)
clove config set openrouterKey sk-or-v1-your-key

# Start
clove start
```

That's it. Kernel running. Dashboard open at `http://localhost:8080/dashboard`.

```bash
# Run an agent
clove run "What are the top 3 AI companies in 2026?"

# Run a fleet (3 agents in parallel)
clove fleet "Compare Rust, Go, and Python" -n 3

# Check status
clove status

# Show shared memory
clove recall

# Stop
clove stop
```

> Full setup guide: [SETUP.md](SETUP.md) — covers npm install, building from source, OpenClaw integration, MCP server, dashboard, troubleshooting.

---

## What you can do

### Run a single agent
```bash
curl -X POST localhost:8080/api/run -d '{
  "goal": "Research Rust programming language and write a summary to /tmp/report.md",
  "budget": 0.30,
  "tools": ["search", "write_file", "remember", "recall"]
}'
```

### Run a fleet (parallel agents)
```bash
curl -N -X POST localhost:8080/api/fleet -d '{
  "goal": "Research 3 different databases: PostgreSQL, MongoDB, Redis",
  "agents": 3,
  "budget": 1.00
}'
```
3 agents run in parallel. SSE events stream as they work. Kernel synthesizes results.

### Use it as an LLM proxy (OpenAI-compatible)
```bash
# Point any tool at CLOVE — gets cost tracking + PII filtering for free
curl -X POST localhost:8080/api/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"model": "anthropic/claude-sonnet-4", "messages": [{"role": "user", "content": "Hello"}]}'
```
Works with Cursor, LangChain, LlamaIndex, OpenClaw — anything that speaks OpenAI format.

### Shared memory between agents
```bash
# Agent 1 stores findings
curl -X POST localhost:8080/api/run -d '{
  "goal": "Research Landlock LSM and store findings in memory",
  "tools": ["search", "remember"], "agent_name": "researcher"
}'

# Agent 2 recalls them (no re-searching)
curl -X POST localhost:8080/api/run -d '{
  "goal": "Recall everything about Landlock and write a report",
  "tools": ["recall", "write_file"], "agent_name": "writer"
}'
```

### Sub-agent delegation
```bash
curl -X POST localhost:8080/api/run -d '{
  "goal": "Delegate: search for Rust uses, search for Go uses, then compare",
  "tools": ["search", "delegate", "remember", "recall"],
  "budget": 0.50
}'
```
The agent spawns sub-agents, each with its own budget. Results flow back up.

### Dashboard
```bash
# Start kernel with --api flag, then open:
open http://localhost:8080/dashboard    # Built-in HTMX dashboard

# Or run the React dashboard:
cd dashboard && npm install && npm run dev
open http://localhost:3000              # 8-page dashboard
```

Pages: Overview, Sandbox Visualizer, Runs, Fleet, OpenClaw, Audit, Cost, Settings.

---

## Agent tools

Every tool is a real operation, permission-gated and audited:

| Tool | What it does |
|---|---|
| `read_file` | Read files from disk (path ACL enforced) |
| `write_file` | Write files to disk (path ACL enforced) |
| `exec` | Run shell commands (command blocklist enforced) |
| `http` | Make HTTP requests (domain allowlist enforced) |
| `search` | Search the web via DuckDuckGo + LLM extraction |
| `store` / `fetch` | Key-value store with TTL and agent scoping |
| `remember` / `recall` | Persistent memory blocks — survive restarts, shared across agents |
| `delegate` | Spawn a sub-agent with its own context, budget, and tools |
| `mcp_*` | Call any connected MCP server tool |

## Agent intelligence

Research-backed techniques built into the RunEngine:

| Feature | What it does | Research |
|---|---|---|
| Plan-then-execute | Agent writes a plan before acting | Wang et al. 2023 |
| Observation masking | Old tool outputs compressed to save context | JetBrains, NeurIPS 2025 |
| Reflexion | On failure, reflects and retries with insight | Shinn et al. 2023 |
| Context positioning | Critical info at start/end of prompt | Liu et al., Stanford 2023 |
| Relevance-scored recall | Memory search by keyword overlap + recency + type priority | Park et al., Stanford 2023 |

## Governance

| Feature | What it does |
|---|---|
| Per-agent permissions | `can_read`, `can_write`, `can_exec`, `can_http` — individually controllable |
| Path ACLs | Which directories each agent can read/write |
| Domain allowlist | Which URLs each agent can access |
| Budget enforcement | Hard kill when cost exceeds limit |
| PII filtering | SSN, email, phone, credit card, IP — scanned on every LLM call |
| Audit trail | 8 categories, per-agent attribution, JSONL export |
| Execution replay | Record and deterministically replay past runs |

## macOS sandbox

CLOVE uses Apple's Seatbelt (`sandbox-exec`) for kernel-enforced isolation on macOS:
- Filesystem: deny all except explicitly allowed paths
- Network: deny all except allowed domains (+ localhost for kernel IPC)
- No root required. No Docker required.

On Linux: full namespace isolation (PID/NET/MNT/UTS) + cgroups v2 + Landlock + seccomp.

---

## OpenClaw integration

[OpenClaw](https://github.com/openclaw/openclaw) (321K stars) runs AI agents on chat platforms. CLOVE sandboxes it:

```bash
# Spawn OpenClaw inside CLOVE sandbox
curl -X POST localhost:8080/api/openclaw/spawn -d '{
  "name": "telegram-bot",
  "soul": "You are a helpful assistant.",
  "budget_usd": 5.00,
  "channels": ["telegram"]
}'
```

Every LLM call OpenClaw makes routes through CLOVE — cost tracked, PII filtered, audited. Shared memory works: RunEngine agents store findings, OpenClaw bot recalls them.

---

## API (71 endpoints)

Key endpoints:

| Endpoint | What it does |
|---|---|
| `POST /api/run` | Run an agent (goal + tools + budget) |
| `POST /api/run/stream` | Same with SSE streaming |
| `POST /api/fleet` | Run N agents in parallel |
| `POST /api/v1/chat/completions` | OpenAI-compatible LLM proxy (with streaming) |
| `POST /api/exec` | Direct shell execution (no LLM) |
| `POST /api/think` | Single LLM call |
| `GET /api/memory` | List memory blocks |
| `GET /api/memory/search?q=` | Relevance-scored memory search |
| `POST /api/memory` | Create memory block |
| `GET /api/audit` | Audit log |
| `GET /api/cost` | Cost tracking |
| `GET /api/health` | Kernel status |
| `GET /api/history` | Run history |
| `GET/PUT /api/agents/:id/permissions` | Per-agent permissions |
| `GET /api/sandbox/overview` | All agents + permissions + activity |
| `POST /api/openclaw/spawn` | Spawn OpenClaw instance |
| `POST /api/schedules` | Create cron schedule |
| `POST /api/webhooks` | Register webhook |

Full spec: [docs/API_SPEC.md](docs/API_SPEC.md)

---

## Python SDK

```bash
pip install -e sdk/python/
```

```python
from clove_sdk.client import CloveClient

with CloveClient("/tmp/clove.sock", agent_id=1) as client:
    # LLM
    result = client.think("What is the capital of France?")

    # Memory
    client.mem_create("notes", type="core", content="Important finding")
    client.mem_share(block_id, target_agent_id=2)

    # IPC
    client.send_message(target_id=2, content="Check shared memory")
    client.broadcast("Status update")

    # Budget
    client.set_budget(max_cost_usd=1.0, kill_on_exceeded=True)
```

---

## CLI flags

```
./clove_kernel [OPTIONS]

Core:
  --socket <path>         Unix socket (default: /tmp/clove.sock)
  --sandbox               Enable sandbox (Seatbelt on macOS, namespaces on Linux)
  --db <path>             SQLite database (default: clove.db)

LLM:
  --openrouter            Enable OpenRouter (300+ models)
  --openrouter-key <key>  API key (or set OPENROUTER_API_KEY)
  --llm-model <model>     Default model (default: google/gemini-2.0-flash-001)

API:
  --api                   Enable REST API + dashboard
  --api-port <port>       Port (default: 8080)
  --api-key <key>         Auth key (or set CLOVE_API_KEY)

Privacy:
  --privacy               Enable PII filter
  --privacy-mode <mode>   audit | redact | block

Integrations:
  --mcp                   Enable MCP bridge
  --a2a                   Enable A2A bridge
```

Environment variables: `OPENROUTER_API_KEY`, `CLOVE_API_KEY` (also reads `.env`).

---

## Project structure

```
clove-v2/
  kernel/src/              Kernel binary (4,457 LOC)
  libs/
    api/                   REST API + RunEngine (4,715 LOC)
    governance/            Permissions, audit, PII, budget (2,169 LOC)
    sandbox/               Seatbelt (macOS), namespaces (Linux) (1,467 LOC)
    integrations/          OpenRouter, MCP, A2A, tunnels (1,462 LOC)
    orchestration/         Scheduler, events, mailbox, state (1,280 LOC)
    context/               Artifacts, memory blocks, assembler (1,007 LOC)
    persistence/           SQLite persistence (932 LOC)
    core/                  Protocol, config, types (698 LOC)
    + agents, ipc, reactor, worlds
  integrations/
    openclaw-plugin/       OpenClaw plugin (953 LOC TypeScript)
    mcp-server/            MCP server (359 LOC TypeScript)
  dashboard/               React dashboard (1,480 LOC, 8 pages)
  sdk/python/              Python SDK (878 LOC)
  benchmarks/              CLOVE vs OpenShell benchmarks
  tests/                   165 tests (Catch2)
  docs/                    Architecture, API spec, roadmap
```

---

## Tests

```bash
cd build && ctest --output-on-failure
# 165/165 tests passing
```

---

## Numbers

| Metric | Value |
|---|---|
| C++ LOC | 19,737 |
| TypeScript LOC | 2,792 |
| Total LOC | ~26,000 |
| Syscalls | 86 |
| API endpoints | 71 |
| Tests | 165 |
| Startup | 52ms |
| Memory (base) | 12.9 MB |
| Memory (per agent) | ~3 MB |
| LLM proxy overhead | 10ms |

---

## License

MIT
