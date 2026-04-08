# @clove/cli

The command-line interface for CLOVE — the AI Agent Fleet OS.

Build, deploy, and manage always-on AI agent fleets from your terminal. Supports multiple runtimes (Claude Code, Codex, OpenClaw), MCP service connections, daemon agents with memory consolidation, and pipeline orchestration.

## Install

```bash
npm install -g @cloveos/cli
```

## Quick Start

```bash
# 1. Start the kernel
clove start

# 2. Add your LLM provider keys
clove provider set anthropic sk-ant-xxx
clove provider set openai sk-xxx

# 3. Connect services via MCP
clove connect github ghp_xxx        # 26 tools available
clove connect slack xoxb-xxx        # 12 tools

# 4. Create and run an agent
clove agent create pr-reviewer
clove agent run pr-reviewer

# 5. Make it always-on (daemon mode)
clove daemon start pr-reviewer
```

## Command Reference

### Kernel Lifecycle

| Command | Description |
|---------|-------------|
| `clove start` | Start kernel, API server, and dashboard |
| `clove stop` | Graceful shutdown |
| `clove status` | Health, agents, cost, uptime, memory |

### Agent Management

| Command | Description |
|---------|-------------|
| `clove agent list` | List all agent definitions |
| `clove agent create <name>` | Create a new agent definition |
| `clove agent show <name>` | Show agent details |
| `clove agent run <name>` | Manually trigger an agent |
| `clove agent enable <name>` | Enable agent (accepts triggers) |
| `clove agent disable <name>` | Disable agent |
| `clove agent delete <name>` | Remove agent definition |

### Agent Execution

| Command | Description |
|---------|-------------|
| `clove run "goal"` | Single agent run with a goal |
| `clove run "goal" --budget 1.0` | Set budget cap |
| `clove run "goal" --model claude-sonnet-4` | Specify model |
| `clove fleet "goal" -n 3` | Run 3 agents in parallel |
| `clove fleet "goal" --world research` | Run in isolated workspace |

### Daemon Agents (Always-On)

Daemon agents run continuously with a tick loop — checking subscriptions, acting when needed, and consolidating memory during idle time.

| Command | Description |
|---------|-------------|
| `clove daemon list` | Show all running daemons with uptime, ticks, cost |
| `clove daemon start <name>` | Start agent as always-on daemon |
| `clove daemon stop <name>` | Graceful stop |
| `clove daemon status <name>` | Detailed daemon state |
| `clove daemon logs <name>` | Stream daemon activity log |
| `clove daemon logs <name> 50` | Last 50 log entries |
| `clove daemon dream <name>` | Trigger memory consolidation |

Daemon states: `sleeping` (between ticks), `acting` (running a task), `dreaming` (memory consolidation), `stopped`.

### Runtimes

CLOVE orchestrates multiple agent runtimes:

| Runtime | Models | Use Case |
|---------|--------|----------|
| `clove` | Any (via OpenRouter) | General agent tasks |
| `claude-code` | Anthropic only | Autonomous coding |
| `codex` | OpenAI only | Autonomous coding |
| `openclaw` | Any (via OpenRouter) | Chat agents |

| Command | Description |
|---------|-------------|
| `clove runtime list` | Show available runtimes |
| `clove runtime run claude-code "Fix auth bug"` | Spawn Claude Code |
| `clove runtime run codex "Write tests"` | Spawn Codex |

### LLM Providers

| Command | Description |
|---------|-------------|
| `clove provider list` | Show configured providers |
| `clove provider set anthropic <key>` | Add Anthropic API key |
| `clove provider set openai <key>` | Add OpenAI API key |
| `clove provider set google <key>` | Add Google AI key |
| `clove provider set openrouter <key>` | Add OpenRouter key (fallback) |
| `clove provider remove <name>` | Remove provider |

### Pipelines

Multi-step sequential workflows across runtimes.

| Command | Description |
|---------|-------------|
| `clove pipeline list` | List all pipelines |
| `clove pipeline create <name>` | Create pipeline with sample step |
| `clove pipeline show <name>` | Show pipeline steps |
| `clove pipeline run <name>` | Execute with live SSE streaming |
| `clove pipeline enable <name>` | Enable pipeline |
| `clove pipeline delete <name>` | Remove pipeline |

### Connections (MCP)

Connect external services via Model Context Protocol.

| Command | Description |
|---------|-------------|
| `clove connect list` | Show all connections |
| `clove connect github <token>` | Connect GitHub (26 tools) |
| `clove connect slack <token>` | Connect Slack |
| `clove connect sentry <token>` | Connect Sentry |
| `clove connect linear <token>` | Connect Linear |
| `clove connect postgres <url>` | Connect PostgreSQL |
| `clove connect notion <token>` | Connect Notion |
| `clove connect filesystem` | Connect local filesystem |
| `clove connect remove <name>` | Disconnect service |

### MCP Server Management

| Command | Description |
|---------|-------------|
| `clove mcp list` | Show MCP servers and tools |
| `clove mcp add <name> <command>` | Add MCP server to config |
| `clove mcp remove <name>` | Remove MCP server |

### Workspaces (Worlds)

Isolated environments for agent teams.

| Command | Description |
|---------|-------------|
| `clove world list` | List all workspaces |
| `clove world create <name>` | Create workspace |
| `clove world launch <template>` | Launch workspace from template |

### OpenClaw

Sandboxed chat agent instances.

| Command | Description |
|---------|-------------|
| `clove openclaw status` | Show running instances |
| `clove openclaw spawn <name>` | Spawn new instance |
| `clove openclaw spawn <name> --soul "helpful assistant"` | With personality |
| `clove openclaw stop <id>` | Stop instance |
| `clove openclaw stop-all` | Stop all instances |

### Key-Value Store

| Command | Description |
|---------|-------------|
| `clove store get <key>` | Read a value |
| `clove store set <key> <value>` | Write a value (JSON supported) |

### Observability

| Command | Description |
|---------|-------------|
| `clove logs [N]` | Last N audit entries (default 20) |
| `clove recall` | Show agent memory blocks |
| `clove policy` | Policy recommendations |
| `clove privacy "text with emails"` | PII scan/redaction |
| `clove replay status` | Recording state |
| `clove replay start` | Begin event recording |
| `clove replay stop` | End recording |

### Configuration

| Command | Description |
|---------|-------------|
| `clove config` | Show current config |
| `clove config set <key> <value>` | Update config |
| `clove config get <key>` | Read config value |
| `clove inference show` | Model gateway config |
| `clove inference set model <id>` | Set default model |
| `clove inference set max_cost <N>` | Set cost cap |

### Swarms

| Command | Description |
|---------|-------------|
| `clove swarm list` | List all swarms |
| `clove swarm create <name> --goal "..." --budget N` | Create a swarm |
| `clove swarm start <name>` | Start / run a swarm |
| `clove swarm delete <name>` | Delete a swarm |

### Schedules

| Command | Description |
|---------|-------------|
| `clove schedule list` | List all cron schedules |
| `clove schedule create <name> "<cron>" --goal "..."` | Create schedule |
| `clove schedule delete <name>` | Remove schedule |

### Webhooks

| Command | Description |
|---------|-------------|
| `clove webhook list` | List registered webhooks |
| `clove webhook create <url> --events run_complete,agent_error` | Register webhook |
| `clove webhook delete <id>` | Remove webhook (id starts with `wh_`) |

### Jobs (Async)

| Command | Description |
|---------|-------------|
| `clove jobs list` | List all jobs (filter by status) |
| `clove jobs show <id>` | Job detail + step log |
| `clove jobs submit <goal>` | Submit async job |
| `clove jobs cancel <id>` | Cancel queued job |
| `clove jobs retry <id>` | Re-queue failed job |

### Other

| Command | Description |
|---------|-------------|
| `clove dashboard` | Open dashboard in browser |
| `clove templates` | Browse agent/pipeline templates |
| `clove deploy <template>` | Deploy a template |
| `clove analyze [path]` | AI-powered project analysis |
| `clove scheduler` | Run scheduler daemon |
| `clove build` | Build mcp-server TypeScript (`npm run build` in mcp-server/) |

## Environment Variables

| Variable | Description |
|----------|-------------|
| `OPENROUTER_API_KEY` | Default OpenRouter key (used by `clove start`) |
| `CLOVE_KERNEL_PATH` | Path to kernel binary |
| `ANTHROPIC_API_KEY` | Anthropic key (used when spawning Claude Code) |
| `OPENAI_API_KEY` | OpenAI key (used when spawning Codex) |

## Configuration File

Stored at `~/.clove/config.json`:

```json
{
  "kernelPath": "/path/to/clove_kernel",
  "apiPort": 8080,
  "openrouterKey": "sk-or-v1-...",
  "privacy": true,
  "privacyMode": "redact",
  "sandbox": true
}
```

## File Structure

```
~/.clove/
├── config.json           # CLI configuration
├── kernel.pid            # Running kernel PID
├── mcp.yaml              # MCP server definitions
├── connections.json      # Connected services
├── clove.db              # Kernel SQLite database
├── agents/               # User-created agent templates
│   └── <name>/
│       └── agent.yaml
└── daemons/              # Daemon logs
    └── <name>/
        └── logs/
```

## Architecture

```
clove CLI ──→ HTTP ──→ CLOVE Kernel (C++) ──→ Runtimes
                           │                      ├── CLOVE RunEngine
                           │                      ├── Claude Code (subprocess)
                           │                      ├── Codex (subprocess)
                           │                      └── OpenClaw (subprocess)
                           │
                           ├── MCP Bridge ──→ GitHub, Slack, Sentry...
                           ├── State Store (SQLite)
                           ├── Memory Blocks
                           ├── Audit Logger
                           └── Daemon Manager (tick loops)
```

## License

MIT
