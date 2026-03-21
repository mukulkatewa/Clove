# clove-openclaw

Run OpenClaw inside CLOVE's kernel instead of OpenShell/NemoClaw.

**122x faster startup. 324x less memory. Full audit trail.**

## What This Does

OpenClaw normally runs with full system access (dangerous) or inside NemoClaw/OpenShell (900MB RAM, 3.3s startup, Docker dependency).

`clove-openclaw` runs OpenClaw as a CLOVE agent process — sandboxed with Linux namespaces, seccomp, and Landlock. Every command OpenClaw executes is audited. LLM costs are tracked. PII is filtered.

## Architecture

```
Without CLOVE:
  OpenClaw daemon → full system access → scary

With NemoClaw:
  OpenClaw → Docker container → K3s → OpenShell → 900MB RAM

With CLOVE:
  OpenClaw → CLOVE kernel sandbox → 3MB RAM
  ├── Namespace isolation (PID/NET/MNT/UTS)
  ├── seccomp BPF (27 blocked syscalls)
  ├── Landlock (filesystem ACLs)
  ├── Network policy (domain allowlist)
  ├── LLM cost tracking + budget enforcement
  ├── PII filtering on all LLM calls
  └── Full audit trail (every action logged)
```

## Prerequisites

- CLOVE v2 kernel built (`clove_kernel` binary)
- Node.js 22+ (for OpenClaw)
- OpenClaw installed (`npm install -g @openclaw/cli` or from source)

## Quick Start

```bash
# 1. Start the CLOVE kernel
clove_kernel --api --api-port 8080 --privacy --openrouter --openrouter-key $OPENROUTER_API_KEY &

# 2. Launch OpenClaw inside CLOVE sandbox
python3 launch.py

# 3. Check status
clove status
clove ps
clove audit --limit 20

# 4. OpenClaw is running sandboxed — use it normally via your messaging apps
```

## Configuration

Edit `clove-openclaw.yaml`:

```yaml
# OpenClaw process configuration
openclaw:
  command: "openclaw"
  args: ["gateway", "--headless"]

# CLOVE sandbox settings
sandbox:
  memory_limit: 512MB
  cpu_quota: 50          # percent
  max_pids: 32

# Filesystem access
filesystem:
  allowed_read:
    - /tmp
    - $HOME/.openclaw
    - $HOME/Documents
  allowed_write:
    - /tmp
    - $HOME/.openclaw

# Network policy
network:
  allowed_domains:
    - api.openai.com
    - api.anthropic.com
    - openrouter.ai
    - api.telegram.org
    - discord.com
    - slack.com
    - web.whatsapp.com

# LLM budget
llm:
  max_cost_usd: 10.0     # daily budget

# PII filtering
privacy:
  enabled: true
  mode: redact            # audit | redact | block
```

## What You Get vs NemoClaw

| Feature | NemoClaw (OpenShell) | clove-openclaw |
|---------|---------------------|----------------|
| Startup time | ~3.3 seconds | ~27ms |
| Memory usage | ~900 MB | ~3 MB |
| Dependencies | Docker, K3s, Colima | None (native binary) |
| Filesystem isolation | Container overlay | Landlock LSM |
| Syscall filtering | seccomp | seccomp BPF (27 rules) |
| Network policy | Container networking | Namespace + domain allowlist |
| LLM cost tracking | No | Yes (kernel-enforced budget) |
| PII filtering | Privacy Router | Regex scanner (5 patterns) |
| Audit trail | Basic logging | Full structured audit (8 categories) |
| Execution replay | No | Yes (record + playback every action) |
| Multi-agent | No | Yes (run multiple OpenClaw instances) |
| GPU passthrough | Yes (NVIDIA) | No |
| Dashboard | No | HTMX live dashboard |
| CLI management | nemoclaw CLI | clove CLI (13 commands) |

## Multi-Agent Mode

Run multiple OpenClaw instances that coordinate through CLOVE:

```bash
# Launch 3 specialized OpenClaw agents
python3 launch.py --name "researcher" --skills research,web
python3 launch.py --name "writer" --skills writing,email
python3 launch.py --name "reviewer" --skills review,qa

# They coordinate via CLOVE's IPC
# Researcher finds information → stores in shared state
# Writer reads state → drafts content
# Reviewer reads draft → sends feedback via mailbox
```

This is impossible with NemoClaw/OpenShell (single-agent only).
