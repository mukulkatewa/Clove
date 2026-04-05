# CLOVE — Setup Guide

> From zero to running agents in 3 commands.

---

## Option A: npm install (recommended)

```bash
# 1. Install (builds kernel automatically)
npm install -g @clove/cli

# 2. Set your LLM key (free tier at https://openrouter.ai/keys)
clove config set openrouterKey sk-or-v1-your-key-here

# 3. Start
clove start
```

Done. Kernel running. Dashboard open. Run agents:

```bash
clove run "Research the top 5 AI companies in 2026"
clove fleet "Compare Rust, Go, and Python" -n 3
clove status
clove recall
clove stop
```

### Prerequisites for npm install

The postinstall script builds the kernel from C++ source. You need:

**macOS:**
```bash
xcode-select --install
brew install cmake openssl curl sqlite3
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt install cmake g++ libcurl4-openssl-dev libssl-dev libsqlite3-dev
```

If the build fails, run `clove build` to retry.

---

## Option B: Build from source

```bash
# Clone
git clone https://github.com/aniiiiXD/Clove.git
cd Clove
git checkout v2

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)    # macOS
# cmake --build . -j$(nproc)              # Linux
cd ..

# Install CLI
cd cli-js && npm install && npm link && cd ..

# Set LLM key
clove config set openrouterKey sk-or-v1-your-key-here

# Start
clove start
```

---

## CLI Reference

```
clove start                    Start kernel + open dashboard
clove stop                     Stop kernel
clove status                   Health, agents, cost, memory
clove run "goal"               Run an agent
clove run "goal" --budget 1.0  Run with specific budget
clove fleet "goal" -n 5        Run 5 agents in parallel
clove recall                   Show shared memory blocks
clove logs                     Recent audit entries
clove logs 50                  Last 50 audit entries
clove dashboard                Open dashboard in browser
clove build                    Rebuild kernel from source
clove config                   Show all config
clove config set key value     Set a config value
clove config get key           Get a config value
```

Shortcuts: `s` = status, `r` = run, `f` = fleet, `d` = dashboard, `c` = config

---

## Config

Config stored at `~/.clove/config.json`. Set via CLI or edit directly.

| Key | Default | What |
|-----|---------|------|
| `openrouterKey` | (required) | LLM API key from openrouter.ai |
| `apiPort` | 8080 | Kernel REST API port |
| `privacy` | true | Enable PII filtering |
| `privacyMode` | redact | PII mode: audit, redact, block |
| `sandbox` | true | Enable Seatbelt sandbox (macOS) |
| `kernelPath` | (auto-detect) | Path to kernel binary |

```bash
# Examples
clove config set openrouterKey sk-or-v1-abc123
clove config set apiPort 9090
clove config set privacy true
clove config set privacyMode block
```

---

## API Endpoints (quick reference)

Once the kernel is running (`clove start`), these endpoints are available:

### Run agents
```bash
# Single agent
curl -X POST localhost:8080/api/run -H "Content-Type: application/json" \
  -d '{"goal": "Your goal here", "budget": 0.50}'

# Fleet (parallel)
curl -N -X POST localhost:8080/api/fleet -H "Content-Type: application/json" \
  -d '{"goal": "Your goal", "agents": 3, "budget": 1.00}'

# OpenAI-compatible (works with any tool that speaks OpenAI format)
curl -X POST localhost:8080/api/v1/chat/completions -H "Content-Type: application/json" \
  -d '{"model": "anthropic/claude-sonnet-4", "messages": [{"role": "user", "content": "Hello"}]}'
```

### Memory
```bash
# List all memory blocks
curl localhost:8080/api/memory

# Search memory (relevance-scored)
curl "localhost:8080/api/memory/search?q=your+query"

# Create memory block
curl -X POST localhost:8080/api/memory -H "Content-Type: application/json" \
  -d '{"name": "my-findings", "content": "Important fact", "type": "core", "access": "shared_read"}'
```

### Monitoring
```bash
curl localhost:8080/api/health           # Kernel status
curl localhost:8080/api/cost             # Total LLM spend
curl localhost:8080/api/audit?limit=20   # Audit log
curl localhost:8080/api/history          # Run history
curl localhost:8080/api/sandbox/overview  # All agents + permissions
```

### Governance
```bash
# Get agent permissions
curl localhost:8080/api/agents/0/permissions

# Set permissions
curl -X PUT localhost:8080/api/agents/0/permissions -H "Content-Type: application/json" \
  -d '{"permissions": {"can_exec": true, "can_http": true, "allowed_domains": ["*"]}}'

# Schedule a recurring run
curl -X POST localhost:8080/api/schedules -H "Content-Type: application/json" \
  -d '{"name": "daily-report", "cron": "0 9 * * *", "run": {"goal": "Write a daily summary", "budget": 0.50}}'

# Set up webhook notifications
curl -X POST localhost:8080/api/webhooks -H "Content-Type: application/json" \
  -d '{"url": "https://hooks.slack.com/your/hook", "events": ["run_complete", "budget_exceeded"]}'
```

Full API spec: [docs/API_SPEC.md](docs/API_SPEC.md)

---

## Dashboard

### Built-in (no setup)
```bash
clove start
open http://localhost:8080/dashboard
```

### React dashboard (full UI, 8 pages)
```bash
cd dashboard
npm install
npm run dev
open http://localhost:3000
```

Pages: Overview, Sandbox Visualizer, Runs, Fleet, OpenClaw, Audit, Cost, Settings

**Note:** React dashboard uses Next.js proxy — must run in dev mode (`npm run dev`).

---

## OpenClaw Integration

Connect agents to Telegram, Slack, WhatsApp, Discord via OpenClaw.

### Setup

```bash
# Install OpenClaw
npm install -g @openclaw/cli

# Install CLOVE plugin
mkdir -p ~/.openclaw/extensions/clove
cd integrations/openclaw-plugin
npm install && npx tsc
cp dist/* ~/.openclaw/extensions/clove/
cp openclaw.plugin.json package.json ~/.openclaw/extensions/clove/
```

### Configure

Create `~/.openclaw/openclaw.json`:
```json
{
  "models": {
    "providers": {
      "clove": {
        "baseUrl": "http://localhost:8080/api/v1",
        "apiKey": "clove-internal",
        "api": "openai-completions",
        "models": [{
          "id": "google/gemini-2.0-flash-001",
          "name": "Gemini Flash (via CLOVE)",
          "reasoning": false,
          "input": ["text"],
          "cost": {"input": 0, "output": 0, "cacheRead": 0, "cacheWrite": 0},
          "contextWindow": 1000000,
          "maxTokens": 8192
        }]
      }
    }
  },
  "gateway": {"port": 18789, "mode": "local"}
}
```

For Telegram, add a `channels` section with your bot token (get from @BotFather).

### Run

```bash
# Start CLOVE first
clove start

# Start OpenClaw
openclaw gateway

# Open OpenClaw dashboard
openclaw dashboard
```

### Shared memory

RunEngine agents and OpenClaw agents share memory through the kernel:

```bash
# Agent stores findings
clove run "Research Rust and store findings in memory" --budget 0.30

# OpenClaw bot recalls them (via dashboard or Telegram)
# Ask: "What do you know about Rust?"
```

---

## MCP Server

Expose CLOVE memory and tools to any MCP client (Claude Code, Cursor, etc).

```bash
cd integrations/mcp-server
npm install && npx tsc
```

### Claude Code
Add to `~/.claude/settings.json`:
```json
{
  "mcpServers": {
    "clove": {
      "command": "node",
      "args": ["/absolute/path/to/clove-v2/integrations/mcp-server/dist/index.js"]
    }
  }
}
```

Tools available: `remember`, `recall`, `share`, `artifact`, `agents`, `cost`, `search`, `run`

---

## Agent Tools

| Tool | What | Permission |
|------|------|------------|
| `read_file` | Read files | `can_read` + path ACL |
| `write_file` | Write files | `can_write` + path ACL |
| `exec` | Shell commands | `can_exec` + command blocklist |
| `http` | HTTP requests | `can_http` + domain allowlist |
| `search` | Web search (DuckDuckGo + LLM) | always allowed |
| `store` / `fetch` | Key-value store | always allowed |
| `remember` / `recall` | Persistent shared memory | always allowed |
| `delegate` | Spawn sub-agent | budget inherited, depth max 3 |
| `mcp_*` | Any MCP tool | requires `--mcp` flag |

---

## Intelligence Features

Built into the RunEngine, active by default:

| Feature | What | Research basis |
|---------|------|---------------|
| Plan-then-execute | Agent writes plan before acting | Wang et al. 2023 |
| Observation masking | Old tool outputs compressed | JetBrains NeurIPS 2025 |
| Reflexion | Retry with self-reflection on failure | Shinn et al. 2023 |
| Context positioning | Critical info at prompt boundaries | Liu/Stanford 2023 |
| Relevance scoring | Memory search by keywords + recency + type | Park/Stanford 2023 |

---

## Kernel Flags

```
./clove_kernel [OPTIONS]

--socket <path>         Unix socket (default: /tmp/clove.sock)
--sandbox               Enable sandbox (Seatbelt macOS, namespaces Linux)
--db <path>             SQLite database (default: clove.db)
--api                   Enable REST API + dashboard
--api-port <port>       API port (default: 8080)
--api-key <key>         API auth key
--openrouter            Enable LLM via OpenRouter
--openrouter-key <key>  API key
--llm-model <model>     Default model
--llm-max-cost <usd>    System-wide cost cap
--privacy               Enable PII filter
--privacy-mode <mode>   audit | redact | block
--mcp                   Enable MCP bridge
--a2a                   Enable A2A bridge
```

Env vars: `OPENROUTER_API_KEY`, `CLOVE_API_KEY` (also reads `.env` file).

---

## Project Structure

```
clove-v2/
  kernel/src/              Kernel (4,457 LOC C++)
  libs/
    api/                   REST API + RunEngine (4,715 LOC)
    governance/            Permissions, audit, PII (2,169 LOC)
    sandbox/               Seatbelt/namespaces (1,467 LOC)
    integrations/          OpenRouter, MCP, A2A (1,462 LOC)
    orchestration/         Scheduler, events, IPC (1,280 LOC)
    context/               Memory, artifacts, assembler (1,007 LOC)
    persistence/           SQLite (932 LOC)
    core/                  Protocol, config (698 LOC)
    + agents, ipc, reactor, worlds
  cli-js/                  CLI (@clove/cli)
  integrations/
    openclaw-plugin/       OpenClaw plugin (953 LOC TS)
    mcp-server/            MCP server (359 LOC TS)
  dashboard/               React dashboard (1,480 LOC, 8 pages)
  sdk/python/              Python SDK (878 LOC)
  benchmarks/              CLOVE vs OpenShell
  tests/                   165 tests (Catch2)
  docs/                    Architecture, API spec, roadmap
```

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `clove start` says "No LLM API key" | `clove config set openrouterKey sk-or-v1-...` |
| `clove start` says "Kernel not found" | `clove build` or set `clove config set kernelPath /path/to/binary` |
| Port 8080 in use | `clove config set apiPort 9090` |
| Build fails on macOS | `xcode-select --install && brew install cmake openssl curl sqlite3` |
| Build fails on Linux | `sudo apt install cmake g++ libcurl4-openssl-dev libssl-dev libsqlite3-dev` |
| Dashboard shows "Offline" | Wait 5 seconds (client-side fetch), or check `curl localhost:8080/api/health` |
| React dashboard CORS error | Use `npm run dev` (not `npm start`) — dev mode has API proxy |
| OpenClaw "unauthorized" | Run `openclaw dashboard` to get the token URL |
| OpenClaw Telegram "pairing" | Approve with `openclaw pairing approve telegram <code>` |

---

## Numbers

| Metric | Value |
|--------|-------|
| Kernel LOC | 19,737 C++ |
| Total LOC | ~26,000 |
| Syscalls | 86 |
| API endpoints | 65 |
| Tests | 165 (all passing) |
| Startup | 52ms |
| Base RAM | 12.9 MB |
| Per-agent RAM | ~3 MB |
| LLM proxy overhead | 10ms |
