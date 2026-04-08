# CLOVE v2 — Claude Code Instructions

## Scope

**This repo is backend only** — C++ kernel, libs, CLI, build system.
Frontend lives in the sibling repo `/Users/anixd/Documents/clove-website`.
Do NOT write any frontend (Next.js / React / TypeScript / CSS) code here.

---

## Running the kernel

```bash
./start.sh                        # sources .env.kernel, starts on port 8080
./build/kernel/clove_kernel --api --sandbox --mcp   # direct
curl http://localhost:8080/api/health                # verify
```

## Seeding demo data

```bash
source .env.kernel && node tools/setup.mjs           # full setup: MCP + seed
source .env.kernel && node tools/seed_incident_pipeline.mjs  # seed only
```

## Building

```bash
cmake --build build --target clove_kernel            # incremental
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build  # full
```

---

## Architecture

```
kernel/src/kernel.cpp       — boot, subsystem wiring, MCP auto-registration
libs/api/src/api_server.cpp — 65+ HTTP endpoints
libs/api/src/job_queue.cpp  — async worker pool, job chaining (depends_on)
libs/api/src/run_engine.cpp — tool-calling agent loop
libs/persistence/           — SQLite: WorkspaceDb, AgentDefDb, AgentRunDb, SwarmDb, ...
libs/integrations/          — McpBridge (stdio MCP protocol)
```

## Key env vars (.env.kernel)

| Var | Purpose |
|-----|---------|
| `SUPABASE_URL` / `SUPABASE_SERVICE_KEY` | Supabase cloud sync |
| `ANTHROPIC_API_KEY` | Native Anthropic API (claude-sonnet-4, claude-opus-4, etc.) |
| `OPENROUTER_API_KEY` | OpenRouter — 300+ models, enable with `--openrouter` flag |
| `CLOVE_API_KEY` | REST API auth key (passed as `Authorization: Bearer`) |
| `GITHUB_TOKEN` | Auto-registers GitHub MCP (26 tools) on boot |
| `SLACK_BOT_TOKEN` | Auto-registers Slack MCP on boot |
| `LINEAR_API_KEY` | Auto-registers Linear MCP on boot |
| `FILESYSTEM_PATH` | Filesystem MCP root (defaults to $HOME) |
| `GITHUB_REPO` | Used by seed scripts (e.g. aniiiiXD/clove-v2) |

Any token present in `.env.kernel` = that MCP server auto-starts at boot. No YAML editing needed.

## MCP servers (auto-registered from env)

- `github` — `@modelcontextprotocol/server-github` (GITHUB_TOKEN)
- `slack` — `@modelcontextprotocol/server-slack` (SLACK_BOT_TOKEN)
- `linear` — `@linear/mcp-server` (LINEAR_API_KEY)
- `filesystem` — `@modelcontextprotocol/server-filesystem` (always, defaults to $HOME)
- Additional: edit `~/.clove/mcp.yaml` or POST `/api/connections/setup`

## Job pipeline

Jobs support `depends_on: "<job_id>"` — worker re-queues until dependency completes.
Cascade: if dependency fails → dependent is automatically failed too.

POST `/api/jobs` fields:
```json
{
  "goal": "...", "agent_name": "...", "workspace_id": "...",
  "model": "...", "budget_usd": 0.5, "max_steps": 20,
  "allowed_tools": ["mcp_call", "store"], "priority": 0,
  "depends_on": "<job_id>"
}
```

## Current demo pipeline: incident-response

Workspace `eng-ops` → 5 chained agents → Supabase → dashboard

```
triager → code-auditor → patch-writer → reviewer → notifier
```

Each uses `mcp_call` with `server="github"` / `server="slack"` / `server="filesystem"`.
Daemon `incident-monitor` watches Slack #incidents and auto-triggers the chain.

## Supabase tables

`workspaces`, `agent_definitions`, `agent_runs`, `workspace_data`, `workspace_outputs`, `swarms`

Dashboard: https://supabase.com/dashboard/project/pzldqapdbiszeumueyzh/editor

## SQLite DB

`~/.clove/clove.db` — local cache, source of truth when Supabase is offline.

## Deployment

```bash
# Docker Compose (kernel + MCP server wired together)
cd deploy && docker-compose up

# Railway — two services:
#   Kernel:     railway.toml at repo root → deploy/Dockerfile
#   MCP server: mcp-server/railway.json  → mcp-server/Dockerfile
#
# Kernel env vars for Railway: OPENROUTER_API_KEY, ANTHROPIC_API_KEY, CLOVE_API_KEY
# MCP env vars for Railway:    PORT=3001, CLOVE_API_URL=<kernel-internal>, CLOVE_MCP_KEY
```

## Tools directory

| Script | Purpose |
|--------|---------|
| `tools/setup.mjs` | One-shot: configure MCP connections + run seed |
| `tools/seed_incident_pipeline.mjs` | Create eng-ops workspace, 5 agents, swarm, chained jobs |
| `tools/seed_github_swarm.mjs` | Original simpler GitHub swarm (3 agents) |
