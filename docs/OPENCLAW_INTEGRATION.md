# CLOVE × OpenClaw Integration Design

> Make CLOVE the sandbox backend for OpenClaw. Follow NemoClaw's exact plugin pattern.

## Overview

Build an OpenClaw plugin called `clove` that:
1. Registers `openclaw clove {launch, status, stop, fleet, connect}` CLI commands
2. Registers CLOVE as an inference provider (LLM calls route through kernel — cost tracked, PII filtered)
3. Manages the CLOVE kernel lifecycle as a background service
4. Implements `sandbox.mode: "clove"` via ISandboxProvider when available

**Reference implementation:** NemoClaw at `/Users/anixd/Documents/NemoClaw/nemoclaw/src/`

## Why This Approach

NemoClaw works. It has NVIDIA's backing. It's the blessed integration pattern. We copy the pattern exactly, replace Docker/OpenShell with the CLOVE kernel, and add fleet support on top.

| | NemoClaw | CLOVE Plugin |
|---|---|---|
| Sandbox backend | Docker + K3s + OpenShell | CLOVE kernel (native) |
| LLM routing | NVIDIA NIM endpoint | Any provider via OpenRouter |
| Startup | ~3.3s (Docker cold start) | ~27ms (native binary) |
| RAM | ~900MB (container overhead) | ~3MB (process overhead) |
| Multi-agent | No | Yes (fleet with per-agent budgets) |
| Cost tracking | No | Yes (kernel-enforced) |
| PII filtering | No | Yes (kernel-level) |
| Audit trail | Basic Docker logs | Full 8-category audit |
| Execution replay | No | Yes |

## Architecture

```
┌──────────────────────────────────────────────────┐
│  openclaw gateway --headless                      │
│                                                    │
│  ┌─────────────┐  ┌──────────────────────────┐   │
│  │ OpenClaw     │  │ CLOVE Plugin              │   │
│  │ Gateway      │  │                            │   │
│  │              │  │ 1. registerCli()           │   │
│  │ Sessions ─── │  │    openclaw clove launch   │   │
│  │ Channels ─── │  │    openclaw clove status   │   │
│  │ Agents ───── │  │    openclaw clove fleet    │   │
│  │ Tools ────── │  │                            │   │
│  │              │  │ 2. registerProvider()      │   │
│  │              │  │    id: "clove"             │   │
│  │              │  │    routes LLM → kernel     │   │
│  │              │  │                            │   │
│  │              │  │ 3. registerService()       │   │
│  │              │  │    starts/stops kernel     │   │
│  └──────┬───────┘  └────────────┬───────────────┘   │
│         │                        │                    │
│         │  exec("ls -la")       │                    │
│         ├────────────────────────▶                    │
│         │                        │                    │
│         │       ┌────────────────▼──────────────┐    │
│         │       │  @clove/sdk (TypeScript)       │    │
│         │       │  CloveClient → Unix socket     │    │
│         │       └────────────────┬──────────────┘    │
└─────────┼────────────────────────┼────────────────────┘
          │                        │
          │              ┌─────────▼──────────┐
          │              │  CLOVE KERNEL       │
          │              │  /tmp/clove.sock    │
          │              │                     │
          │              │  SYS_EXEC  (audit)  │
          │              │  SYS_READ  (perms)  │
          │              │  SYS_WRITE (perms)  │
          │              │  SYS_HTTP  (SSRF)   │
          │              │  SYS_THINK (PII,$)  │
          │              │  SET_BUDGET (limit)  │
          │              │                     │
          │              │  Sandbox:            │
          │              │  PID NS · NET NS    │
          │              │  Landlock · seccomp  │
          │              └─────────────────────┘
```

## Plugin Structure

```
integrations/openclaw-plugin/
  openclaw.plugin.json           # Plugin manifest
  package.json                   # npm package: @clove/openclaw-plugin
  tsconfig.json
  src/
    index.ts                     # Plugin entry: register(), types
    cli.ts                       # CLI registrar: openclaw clove <cmd>
    kernel-manager.ts            # Start/stop/health check CLOVE kernel
    exec-provider.ts             # Route OpenClaw exec through kernel
    inference-provider.ts        # Route LLM calls through kernel
    commands/
      launch.ts                  # openclaw clove launch
      status.ts                  # openclaw clove status
      stop.ts                    # openclaw clove stop
      fleet.ts                   # openclaw clove fleet
      connect.ts                 # openclaw clove connect (shell into sandbox)
      slash.ts                   # /clove chat command handler
    config/
      defaults.ts                # Default CLOVE config
      schema.ts                  # Zod validation
```

## Plugin Manifest

```json
{
  "id": "clove",
  "name": "CLOVE",
  "version": "0.1.0",
  "description": "Run OpenClaw inside CLOVE kernel sandbox — 122x faster than Docker, with cost tracking, PII filtering, and fleet support",
  "configSchema": {
    "type": "object",
    "properties": {
      "kernelPath": {
        "type": "string",
        "description": "Path to clove_kernel binary. Auto-detected if on PATH.",
        "default": ""
      },
      "socketPath": {
        "type": "string",
        "description": "Unix socket path for kernel IPC",
        "default": "/tmp/clove.sock"
      },
      "apiPort": {
        "type": "number",
        "description": "Kernel REST API port",
        "default": 8080
      },
      "budget": {
        "type": "object",
        "properties": {
          "maxCostUsd": { "type": "number", "default": 10.0 },
          "maxTokens": { "type": "number", "default": 0 }
        }
      },
      "privacy": {
        "type": "object",
        "properties": {
          "enabled": { "type": "boolean", "default": true },
          "mode": { "type": "string", "enum": ["audit", "redact", "block"], "default": "redact" }
        }
      },
      "sandbox": {
        "type": "object",
        "properties": {
          "memoryLimitMb": { "type": "number", "default": 512 },
          "cpuQuotaPercent": { "type": "number", "default": 50 },
          "maxPids": { "type": "number", "default": 64 }
        }
      },
      "network": {
        "type": "object",
        "properties": {
          "allowedDomains": {
            "type": "array",
            "items": { "type": "string" },
            "default": [
              "api.openai.com", "api.anthropic.com", "openrouter.ai",
              "api.telegram.org", "discord.com", "gateway.discord.gg",
              "slack.com", "web.whatsapp.com"
            ]
          }
        }
      }
    },
    "additionalProperties": false
  }
}
```

## Plugin Entry Point (index.ts)

Mirrors NemoClaw's `register()` exactly:

```typescript
export default function register(api: OpenClawPluginApi): void {
  // 1. /clove slash command
  api.registerCommand({
    name: "clove",
    description: "CLOVE kernel management (status, budget, fleet)",
    acceptsArgs: true,
    handler: (ctx) => handleSlashCommand(ctx, api),
  });

  // 2. openclaw clove <subcommand> CLI
  api.registerCli(
    (cliCtx) => registerCliCommands(cliCtx, api),
    { commands: ["clove"] },
  );

  // 3. CLOVE as inference provider
  api.registerProvider({
    id: "clove",
    label: "CLOVE Kernel (OpenRouter + cost tracking + PII filter)",
    aliases: ["clove-kernel"],
    envVars: ["OPENROUTER_API_KEY", "OPENAI_API_KEY", "ANTHROPIC_API_KEY"],
    models: { chat: [
      { id: "clove/auto", label: "Auto-detect (uses kernel's configured provider)", contextWindow: 200000 },
    ]},
    auth: [{ type: "bearer", envVar: "OPENROUTER_API_KEY", headerName: "Authorization", label: "API Key (any supported provider)" }],
  });

  // 4. CLOVE kernel as background service
  api.registerService({
    id: "clove-kernel",
    start: async ({ config, logger }) => {
      await startKernel(getPluginConfig(api), logger);
    },
    stop: async ({ logger }) => {
      await stopKernel(logger);
    },
  });

  // Banner
  const cfg = getPluginConfig(api);
  api.logger.info("");
  api.logger.info("  ┌─────────────────────────────────────────────────┐");
  api.logger.info("  │  CLOVE registered                                │");
  api.logger.info("  │                                                   │");
  api.logger.info("  │  Sandbox:   kernel (namespaces + seccomp)        │");
  api.logger.info("  │  Budget:    $" + cfg.budget.maxCostUsd.toFixed(2).padEnd(37) + "│");
  api.logger.info("  │  Privacy:   " + cfg.privacy.mode.padEnd(39) + "│");
  api.logger.info("  │  Commands:  openclaw clove <command>             │");
  api.logger.info("  │  Dashboard: http://localhost:" + cfg.apiPort + "/dashboard".padEnd(21) + "│");
  api.logger.info("  └─────────────────────────────────────────────────┘");
  api.logger.info("");
}
```

## CLI Commands

```
openclaw clove launch              Start CLOVE kernel + sandbox OpenClaw
openclaw clove status              Show kernel health, agent count, cost, budget
openclaw clove stop                Stop kernel + all agents
openclaw clove fleet               Launch multiple OpenClaw agents (see Fleet section)
openclaw clove connect             Shell into the sandbox
openclaw clove budget              Show cost breakdown
openclaw clove audit [--limit N]   Show audit log
openclaw clove replay              List/replay recorded sessions
```

## Fleet Command

```bash
openclaw clove fleet --config fleet.yaml
```

```yaml
# fleet.yaml
fleet:
  name: my-team
  budget_usd: 10.00

agents:
  inbox:
    soul: "You manage my email. Triage, draft replies, flag urgent."
    channels: [gmail]
    skills: [email, calendar]
    budget_usd: 2.00
    can: [email, calendar]

  researcher:
    soul: "You research topics deeply. Cite sources."
    channels: [telegram]
    skills: [web, research]
    budget_usd: 3.00
    can: [http]

  coder:
    soul: "You write code, run tests, fix bugs."
    channels: [slack]
    skills: [coding, exec]
    budget_usd: 5.00
    can: [exec, read, write]
```

Internally calls `POST /api/openclaw/fleet` on the kernel API.

## LLM Routing (Inference Provider)

When OpenClaw is configured with `provider: "clove"`, all LLM calls route through the kernel:

```
OpenClaw agent wants to think
    ↓
OpenClaw calls clove provider
    ↓
Provider POSTs to localhost:8080/api/think
    ↓
Kernel pipeline:
  1. PII filter → scan/redact
  2. Budget check → agent within limits?
  3. Inference gateway → model allowed?
  4. LLM Queue → OpenRouter → actual LLM
  5. Record cost → agent budget
  6. Audit log → the call
    ↓
Response back to OpenClaw
```

OpenClaw thinks it's just calling another LLM provider. The kernel transparently adds cost tracking, PII protection, budget enforcement, and audit logging.

## Exec Routing

Via `tools.exec.host: "node"` or ISandboxProvider:

```
OpenClaw agent wants to run "ls -la"
    ↓
Exec tool routes to CLOVE
    ↓
CloveClient.exec("ls -la")
    ↓
Kernel:
  1. Permission check (can_exec?)
  2. Command allowlist check
  3. popen() inside sandboxed namespace
  4. Audit log the command + output
    ↓
stdout/stderr back to OpenClaw
```

## Kernel Management

```typescript
// kernel-manager.ts

const KERNEL_SEARCH_PATHS = [
  '~/.clove/bin/clove_kernel',
  '/usr/local/bin/clove_kernel',
  process.env.CLOVE_KERNEL_PATH,
];

async function startKernel(config: ClovePluginConfig, logger: PluginLogger) {
  const binary = findKernelBinary();
  if (!binary) {
    logger.error('CLOVE kernel not found. Install from https://cloveos.com');
    return;
  }

  const args = [
    '--socket', config.socketPath,
    '--api', '--api-port', String(config.apiPort),
    '--privacy', '--privacy-mode', config.privacy.mode,
  ];

  // Auto-detect LLM provider from environment
  // Kernel reads OPENROUTER_API_KEY and ANTHROPIC_API_KEY directly from env on startup
  if (process.env.OPENROUTER_API_KEY) {
    args.push('--openrouter'); // key is read from OPENROUTER_API_KEY env var
  }

  // Spawn kernel as background process
  const child = spawn(binary, args, {
    detached: true, stdio: 'ignore'
  });
  child.unref();

  // Wait for socket
  await waitForSocket(config.socketPath, 5000);
  logger.info(`CLOVE kernel started (PID ${child.pid})`);
}
```

## Build Order

| Step | What | Deliverable |
|------|------|-------------|
| 1 | Plugin scaffold | openclaw.plugin.json + index.ts + types |
| 2 | Kernel manager | Start/stop/health check kernel from plugin |
| 3 | CLI: launch/status/stop | Core management commands |
| 4 | Inference provider | LLM calls route through kernel |
| 5 | Exec routing | Shell commands route through kernel |
| 6 | Fleet command + API | Multiple OpenClaw agents with per-agent config |
| 7 | /clove slash command | Chat-based management |
| 8 | Submit PR to OpenClaw | Plugin listed in their ecosystem |

## User Experience

```bash
# Install
npm install -g @clove/openclaw-plugin
# or: openclaw plugin install @clove/openclaw-plugin

# Configure
openclaw config set plugins.clove.budget.maxCostUsd 10
openclaw config set plugins.clove.privacy.enabled true

# Launch (kernel starts automatically)
openclaw clove launch

# Check status
openclaw clove status
# → Kernel: running (PID 1234)
# → Budget: $0.42 / $10.00
# → Agents: 1 active
# → Audit: 47 entries
# → Dashboard: http://localhost:8080/dashboard

# Launch a fleet
openclaw clove fleet --config fleet.yaml
# → Starting 3 agents...
# → inbox (gmail) · $2.00 budget
# → researcher (telegram) · $3.00 budget
# → coder (slack) · $5.00 budget
# → Fleet running. Dashboard: http://localhost:8080/dashboard

# In any chat app:
/clove status    → Shows kernel health + cost
/clove budget    → Shows per-agent spend
/clove stop      → Stops the fleet
```

## What NemoClaw Can't Do (Our Advantage)

| Feature | NemoClaw | CLOVE Plugin |
|---------|----------|-------------|
| Multi-agent fleet | No | `openclaw clove fleet` |
| Per-agent budget | No | Kernel-enforced per process |
| Per-agent permissions | No | Kernel RBAC per process |
| PII filtering | No | Every LLM call scanned |
| Execution replay | No | Record + deterministic replay |
| Cost dashboard | No | Real-time via REST API |
| Agent IPC | No | Kernel mailbox between agents |
| World isolation | No | Kernel worlds per fleet |
| Audit export | No | JSONL export for compliance |
| 27ms startup | 3.3s | Native binary, no Docker |
| 3MB RAM | 900MB | No container overhead |
