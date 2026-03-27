# CLOVE — Setup Guide

> Everything you need to get CLOVE running on your machine and start building with it.

---

## macOS Setup (5 minutes)

### 1. Prerequisites

```bash
# Xcode command line tools
xcode-select --install

# Dependencies
brew install cmake openssl curl sqlite3 node
```

### 2. Clone and build

```bash
git clone https://github.com/aniiiiXD/Clove.git
cd Clove
git checkout v2

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
cd ..
```

Build produces: `build/kernel/clove_kernel`

### 3. Get an LLM API key

Get a free key from [OpenRouter](https://openrouter.ai/keys) (gives access to 300+ models).

```bash
export OPENROUTER_API_KEY="sk-or-v1-your-key-here"
```

Or create a `.env` file in the repo root:
```
OPENROUTER_API_KEY=sk-or-v1-your-key-here
```

### 4. Start the kernel

```bash
./build/kernel/clove_kernel --sandbox --privacy --api
```

You should see:
```
✓  macOS: Seatbelt sandbox enabled
✓  Starting CLOVE v2.0.0
[info] API server listening on port 8080

═══════════════════════════════════════
  CLOVE READY  ·  Ctrl+C to shutdown
═══════════════════════════════════════
```

### 5. Verify it works

```bash
# Health check
curl localhost:8080/api/health

# Run an agent
curl -X POST localhost:8080/api/run \
  -H "Content-Type: application/json" \
  -d '{"goal": "What is 2+2? Answer with just the number.", "budget": 0.05}'

# Check cost
curl localhost:8080/api/cost
```

---

## Dashboard Setup

### Built-in dashboard (no setup needed)

Open `http://localhost:8080/dashboard` — HTMX dashboard, works immediately.

### React dashboard (full UI)

```bash
cd dashboard
npm install
npm run dev
```

Open `http://localhost:3000`

Pages: Overview, Sandbox Visualizer, Runs, Fleet, OpenClaw, Audit, Cost, Settings.

**Important:** The React dashboard proxies API calls to `localhost:8080` via Next.js rewrites. Only works in dev mode (`npm run dev`), not production build.

---

## OpenClaw Integration (optional)

For agents that need chat platforms (Telegram, Slack, WhatsApp).

### 1. Install OpenClaw

```bash
npm install -g @openclaw/cli
# or use a local copy:
# npx @openclaw/cli --version
```

### 2. Install the CLOVE plugin

```bash
# Copy plugin to OpenClaw extensions
mkdir -p ~/.openclaw/extensions/clove
cp integrations/openclaw-plugin/dist/* ~/.openclaw/extensions/clove/
cp integrations/openclaw-plugin/openclaw.plugin.json ~/.openclaw/extensions/clove/
cp integrations/openclaw-plugin/package.json ~/.openclaw/extensions/clove/
```

Or build from source:
```bash
cd integrations/openclaw-plugin
npm install
npx tsc
# Then copy dist/ to ~/.openclaw/extensions/clove/
```

### 3. Configure OpenClaw

Create `~/.openclaw/openclaw.json`:
```json
{
  "models": {
    "providers": {
      "clove": {
        "baseUrl": "http://localhost:8080/api/v1",
        "apiKey": "clove-internal",
        "api": "openai-completions",
        "models": [
          {
            "id": "google/gemini-2.0-flash-001",
            "name": "Gemini Flash (via CLOVE)",
            "reasoning": false,
            "input": ["text"],
            "cost": { "input": 0.0, "output": 0.0, "cacheRead": 0.0, "cacheWrite": 0.0 },
            "contextWindow": 1000000,
            "maxTokens": 8192
          }
        ]
      }
    }
  },
  "gateway": {
    "port": 18789,
    "mode": "local"
  }
}
```

For Telegram, add:
```json
{
  "channels": {
    "telegram": {
      "enabled": true,
      "botToken": "YOUR_BOT_TOKEN",
      "groupPolicy": "open"
    }
  }
}
```

### 4. Start OpenClaw

```bash
# Make sure CLOVE kernel is running first
openclaw gateway
```

### 5. Verify shared memory works

```bash
# Agent stores findings
curl -X POST localhost:8080/api/run -d '{
  "goal": "Search for what Rust is used for and store findings in memory",
  "tools": ["search", "remember"], "agent_name": "researcher"
}'

# OpenClaw bot recalls them (via dashboard at localhost:18789)
# Ask: "What do you know about Rust?"
```

---

## MCP Server (optional)

Exposes CLOVE memory/artifacts to any MCP client (Claude Code, Cursor, etc).

```bash
cd integrations/mcp-server
npm install
npx tsc
```

### Claude Code integration

Add to `~/.claude/settings.json`:
```json
{
  "mcpServers": {
    "clove": {
      "command": "node",
      "args": ["/path/to/clove-v2/integrations/mcp-server/dist/index.js"]
    }
  }
}
```

### Available MCP tools

| Tool | What it does |
|---|---|
| `remember` | Store fact in shared kernel memory |
| `recall` | Retrieve facts from shared memory |
| `share` | Share memory block with another agent |
| `artifact` | Create typed artifact (research, report, plan) |
| `agents` | List running agents |
| `cost` | Check LLM cost tracking |
| `search` | Web search through kernel |
| `run` | Delegate task to RunEngine agent |

---

## Common tasks

### Run a research agent
```bash
curl -X POST localhost:8080/api/run -d '{
  "goal": "Research the top 5 AI agent frameworks in 2026 and compare their features",
  "budget": 0.50,
  "tools": ["search", "remember", "recall", "write_file"]
}'
```

### Run a fleet
```bash
curl -N -X POST localhost:8080/api/fleet -d '{
  "goal": "Research 3 different programming languages",
  "agents": 3,
  "budget": 1.00
}'
```

### Create a scheduled run
```bash
curl -X POST localhost:8080/api/schedules -d '{
  "name": "daily-report",
  "cron": "0 9 * * *",
  "run": {"goal": "Research latest AI news and write a summary", "budget": 0.50}
}'
```

### Set up a webhook
```bash
curl -X POST localhost:8080/api/webhooks -d '{
  "url": "https://hooks.slack.com/services/YOUR/HOOK",
  "events": ["run_complete", "budget_exceeded"]
}'
```

### Spawn OpenClaw via API
```bash
curl -X POST localhost:8080/api/openclaw/spawn -d '{
  "name": "support-bot",
  "soul": "You triage support tickets and draft replies.",
  "budget_usd": 10.00,
  "channels": ["telegram"]
}'
```

### Check everything
```bash
curl localhost:8080/api/health          # Kernel status
curl localhost:8080/api/cost            # Total LLM spend
curl localhost:8080/api/audit?limit=20  # Recent actions
curl localhost:8080/api/memory          # Shared memory blocks
curl localhost:8080/api/history         # Run history
curl localhost:8080/api/openclaw/status # OpenClaw instances
curl localhost:8080/api/sandbox/overview # Full agent state
```

---

## CLI flags reference

```
./clove_kernel [OPTIONS]

Core:
  --socket <path>         Unix socket (default: /tmp/clove.sock)
  --sandbox               Enable sandbox (Seatbelt on macOS, namespaces on Linux)
  --db <path>             SQLite database (default: clove.db)

LLM:
  --openrouter            Enable OpenRouter
  --openrouter-key <key>  API key (or OPENROUTER_API_KEY env var)
  --llm-model <model>     Default model (default: google/gemini-2.0-flash-001)
  --llm-max-cost <usd>    System-wide cost cap

API:
  --api                   Enable REST API + dashboard
  --api-port <port>       Port (default: 8080)
  --api-key <key>         Auth key (or CLOVE_API_KEY env var)

Privacy:
  --privacy               Enable PII filter
  --privacy-mode <mode>   audit | redact | block

Integrations:
  --mcp                   Enable MCP bridge
  --a2a                   Enable A2A bridge
```

---

## Project structure (for contributors)

```
kernel/src/              Kernel binary — main.cpp, kernel.cpp, 22 syscall modules
libs/
  api/                   REST API (71 endpoints) + RunEngine (11 tools)
  governance/            Permissions, audit, PII, budget enforcement
  sandbox/               Seatbelt (macOS), namespaces+Landlock+seccomp (Linux)
  integrations/          OpenRouter, MCP bridge, A2A bridge, tunnel bridge
  orchestration/         Scheduler, event bus, mailbox/IPC, state store
  context/               Memory blocks, artifacts, chains, context assembler
  persistence/           SQLite persistence (state, artifacts, memory, audit)
  core/                  Protocol (86 opcodes), config, types
  agents/                Agent process manager
  ipc/                   Unix socket server/client
  reactor/               Event loop (kqueue/epoll)
  worlds/                World engine (tenant isolation)

integrations/
  openclaw-plugin/       OpenClaw plugin (TypeScript) — tools + prompt injection
  mcp-server/            MCP server (TypeScript) — 8 tools, 2 resource types

dashboard/               React dashboard (Next.js) — 8 pages
sdk/python/              Python SDK — 86 methods
benchmarks/              CLOVE vs OpenShell benchmarks
tests/                   165 tests (Catch2)
docs/                    Architecture, API spec, roadmap
```

---

## Tests

```bash
cd build
ctest --output-on-failure
# 165/165 tests passing
```

---

## Troubleshooting

**Kernel won't start:**
- Check if port 8080 is in use: `lsof -i :8080`
- Use a different port: `--api-port 9090`
- Check OpenRouter key: `echo $OPENROUTER_API_KEY`

**Build fails:**
- Make sure Xcode CLT is installed: `xcode-select --install`
- Make sure cmake is recent: `cmake --version` (need 3.20+)
- Clean build: `rm -rf build && mkdir build && cd build && cmake ..`

**Dashboard won't connect:**
- React dashboard must run in dev mode: `npm run dev` (not `npm start`)
- Check kernel is on port 8080: `curl localhost:8080/api/health`

**OpenClaw won't start:**
- Check if port 18789 is free: `lsof -i :18789`
- Kill stale processes: `pkill -f openclaw`
- Re-run: `openclaw gateway`

**OpenClaw says "unauthorized":**
- Get token: `openclaw dashboard`
- Open the URL with `#token=...` in browser

**Telegram bot not responding:**
- Approve pairing: `openclaw pairing approve telegram <code>`
- Check bot token: `curl https://api.telegram.org/bot<TOKEN>/getMe`

---

## Environment variables

| Variable | What | Default |
|---|---|---|
| `OPENROUTER_API_KEY` | LLM API key | required |
| `CLOVE_API_KEY` | REST API auth key | none (no auth) |
| `CLOVE_KERNEL_PATH` | Path to kernel binary | auto-detect |
| `OPENCLAW_PATH` | Path to OpenClaw binary | auto-detect |
