# CLOVE v2 — Architecture

> 86 syscalls · 14 libraries · 65+ API endpoints · C++23  
> Deployed: Railway (kernel + MCP) · npm: `@cloveos/mcp-server`

---

## System Overview

```
  ┌──────────────────────────────────────────────────────────┐
  │  Clients                                                  │
  │  Claude Code · Cursor · any MCP host · REST API calls     │
  └────────────────────────┬─────────────────────────────────┘
                           │ HTTPS
              ┌────────────┴────────────┐
              │                         │
  ┌───────────▼──────────┐  ┌──────────▼──────────────────┐
  │  MCP Server (Node.js) │  │  Kernel REST API             │
  │  /mcp  StreamableHTTP │  │  65+ endpoints  port $PORT  │
  │  /health              │  │  Auth: Bearer CLOVE_API_KEY │
  └───────────┬───────────┘  └──────────┬──────────────────┘
              │ HTTP (Railway internal)  │
              └──────────┬──────────────┘
                         │
  ┌──────────────────────▼───────────────────────────────────┐
  │  CLOVE KERNEL (C++23)                                     │
  │                                                           │
  │  ┌──────────────────────────────────────────────────┐    │
  │  │ SyscallRouter  ──→ Budget middleware              │    │
  │  │                ──→ 22 syscall modules (86 ops)    │    │
  │  └──────────────────────────────────────────────────┘    │
  │                                                           │
  │  ┌─────────────┐ ┌──────────────┐ ┌──────────────────┐  │
  │  │ Orchestration│ │ Context Layer│ │ Governance       │  │
  │  │ scheduler    │ │ artifacts    │ │ permissions/RBAC │  │
  │  │ llm_queue    │ │ chains       │ │ audit_log        │  │
  │  │ state_store  │ │ memory_blks  │ │ pii_filter       │  │
  │  │ event_bus    │ │ ctx_assembler│ │ budget_enforcer  │  │
  │  └─────────────┘ └──────────────┘ └──────────────────┘  │
  │                                                           │
  │  ┌─────────────┐ ┌──────────────┐ ┌──────────────────┐  │
  │  │ Sandbox      │ │ Integrations │ │ Persistence      │  │
  │  │ namespaces   │ │ OpenRouter   │ │ SQLite WAL       │  │
  │  │ cgroups v2   │ │ Anthropic    │ │ workspaces       │  │
  │  │ landlock     │ │ MCP bridge   │ │ agent_runs       │  │
  │  │ seccomp BPF  │ │ A2A bridge   │ │ memory_blocks    │  │
  │  └─────────────┘ └──────────────┘ └──────────────────┘  │
  └──────────────────────────────────────────────────────────┘
                         │
              ┌──────────▼──────────┐
              │  Supabase (cloud)    │
              │  workspaces          │
              │  agent_definitions   │
              │  agent_runs          │
              │  workspace_outputs   │
              └─────────────────────┘
```

---

## Deployment (Current)

| Service | URL | Notes |
|---------|-----|-------|
| Kernel | `https://kernel-production-96de.up.railway.app` | C++ binary, Alpine Linux |
| MCP | `https://mcp-production-07a6.up.railway.app/mcp` | Node.js, StreamableHTTP |
| Database | SQLite `/data/clove.db` on Railway volume | Local + Supabase sync |

Internal communication: MCP → Kernel via `http://kernel.railway.internal:8080`

---

## Syscall Map (86 opcodes)

```
0x00─0x04  CORE          NOOP, THINK, EXEC, READ, WRITE
0x10─0x15  AGENTS        SPAWN, KILL, LIST, PAUSE, RESUME
0x20─0x23  IPC           SEND, RECV, BROADCAST, REGISTER
0x30─0x33  STATE         STORE, FETCH, DELETE, KEYS
0x40─0x43  PERMISSIONS   GET_PERMS, SET_PERMS, AUTH, POLICY_UPDATE
0x50       NETWORK       HTTP
0x60─0x63  EVENTS        SUBSCRIBE, UNSUBSCRIBE, POLL_EVENTS, EMIT
0x70─0x77  REPLAY/AUDIT  RECORD_START/STOP/STATUS, REPLAY_START/STATUS, AUDIT_LOG/CONFIG
0x80       ASYNC         ASYNC_POLL
0xA0─0xA8  WORLDS        CREATE, DESTROY, LIST, JOIN, LEAVE, EVENT, STATE, SNAPSHOT, RESTORE
0xB0─0xB4  TUNNELS       CONNECT, DISCONNECT, STATUS, LIST_REMOTES, CONFIG
0xC0─0xC3  METRICS       SYSTEM, AGENT, ALL_AGENTS, CGROUP
0xD0─0xD9  INTEGRATIONS  LLM_CONFIG, PII_SCAN/REDACT, MCP_CALL/LIST, A2A_SEND/RECV,
                          POLICY_RECOMMEND, CREDS_GET, OTEL_SPAN
0xE0─0xE8  CONTEXT       DOC_CREATE/READ/UPDATE/LIST/DELETE, CHAIN_CREATE/GET/FORK,
                          CONTEXT_ASSEMBLE
0xE9─0xEF  MEMORY        MEM_CREATE/READ/WRITE/APPEND/DELETE/LIST/SHARE
0xF0─0xF4  META/BUDGET   LLM_REPORT, SET_BUDGET/GET_BUDGET, SET_PRIORITY/GET_PRIORITY
0xFE─0xFF  META          HELLO, EXIT
```

---

## Data Flow: Agent Job (end to end)

```
User (Claude Code) calls clove_submit_job
         │
         ▼ POST /api/jobs  (Bearer CLOVE_API_KEY)
  ┌──────────────────┐
  │  ApiServer       │──→ auth middleware
  │  job_queue.push()│
  └──────┬───────────┘
         │ async worker picks up job
         ▼
  ┌──────────────────┐
  │  RunEngine       │──→ load agent def from workspace
  │  tool-call loop  │──→ build_tools(allowed_tools)
  └──────┬───────────┘
         │
    ┌────▼──────────────────────────────────────┐
    │ Step N: SYS_THINK                          │
    │  ContextAssembler.assemble()               │
    │  PrivacyFilter.scan()                      │
    │  LlmQueue.submit() → OpenRouter/Anthropic  │
    │  budget.record_tokens/cost()               │
    └────┬──────────────────────────────────────┘
         │ tool call in response?
         ├─ YES → dispatch tool (mcp_call, store, http, exec...)
         │        → loop back to SYS_THINK
         └─ NO  → job COMPLETED
                  persist run to SQLite + Supabase
                  emit job.completed event
```

---

## Wire Protocol (Unix socket, local agents)

```
┌──────────┬──────────┬────────┬──────────────┬─────────────────┐
│  magic   │ agent_id │ opcode │ payload_size │    payload      │
│  4 bytes │ 4 bytes  │ 1 byte │   8 bytes    │ variable (JSON) │
│ "AGNT"   │ uint32   │ uint8  │   uint64     │ max 1 MB        │
└──────────┴──────────┴────────┴──────────────┴─────────────────┘
  17-byte header                               UTF-8 JSON body
```

---

## Memory Architecture (3-Tier)

```
Tier 1 — Working Memory (LLM context window)
  Assembled per SYS_THINK call by ContextAssembler
  [SYSTEM] pinned persona blocks
  [CORE]   current task state + findings
  [SHARED] cross-agent artifacts
  [RECALL] retrieved relevant blocks
  [TASK]   current instruction

Tier 2 — Session Memory (kernel RAM, <1ms)
  MemoryBlockStore   — typed blocks (SYSTEM/CORE/RECALL/ARCHIVE)
  ArtifactStore      — typed docs (research/analysis/report/plan)
  StateStore         — flat KV (agent scratch space)

Tier 3 — Persistent Memory (SQLite WAL, 1-10ms)
  memory_blocks, artifacts, chains, state_store, audit_log
  Boot-loaded into Tier 2 on kernel start
  Write-through on every mutation
```

---

## Sandbox (Linux)

```
Per-agent process isolation:
  PID namespace   — agent can't see/kill host processes
  MNT namespace   — private filesystem view
  UTS namespace   — isolated hostname
  NET namespace   — optional: no network access
  cgroups v2      — memory/CPU/PID limits
  Landlock        — filesystem path allowlist
  seccomp BPF     — 27 dangerous syscalls blocked
                    (ptrace, kexec, mount, bpf, userfaultfd, ...)
  Default policy  — allow-all minus deny-list
```

---

## Library Dependency Graph

```
core
 ├── reactor          (epoll/kqueue event loop)
 ├── ipc              (unix socket server/client)
 ├── orchestration    (scheduler, llm_queue, state, events, mailbox)
 │    └── agents      (process management)
 │         └── sandbox (namespaces, cgroups, landlock, seccomp)
 ├── context          (artifacts, chains, memory blocks, assembler)
 ├── governance       (permissions, audit, pii, policy, budget)
 ├── integrations     (openrouter, anthropic, mcp, a2a, tunnel)
 ├── persistence      (sqlite: 5 tables)
 ├── worlds           (multi-tenant workspace isolation)
 └── api              (REST + job queue + run engine)
      └── kernel      (wires all 22 syscall modules)
```

---

## File Structure

```
clove-v2/
├── kernel/src/
│   ├── main.cpp                 # Entry point, env var config (PORT, CLOVE_API_KEY)
│   ├── kernel.cpp               # Kernel class — subsystem wiring
│   ├── syscall_router.cpp       # Dispatch + budget middleware
│   └── syscalls/                # 22 modules, 86 handlers
├── libs/
│   ├── core/                    # Protocol, config, types
│   ├── reactor/                 # Event loop
│   ├── ipc/                     # Unix socket
│   ├── agents/                  # Process management
│   ├── sandbox/                 # OS isolation
│   ├── orchestration/           # Scheduler, LLM queue, state, events
│   ├── context/                 # Artifacts, chains, memory, assembler
│   ├── governance/              # Permissions, audit, PII, budget
│   ├── integrations/            # OpenRouter, Anthropic, MCP, A2A
│   ├── persistence/             # SQLite (WAL)
│   ├── worlds/                  # Workspace isolation
│   └── api/                     # REST API, job queue, run engine
├── mcp-server/                  # Node.js MCP server (npm: @cloveos/mcp-server)
├── deploy/                      # Dockerfile, docker-compose, railway.toml
├── docs/                        # Architecture, API spec, capabilities, roadmap
└── tests/                       # 165 tests (Catch2)
```

---

## Related Docs

- [API Reference](./API_SPEC.md)
- [Capabilities](./CAPABILITIES.md)
- [Multi-User Architecture](./MULTI_USER.md)
- [Security & Compliance](./architecture/SECURITY_COMPLIANCE.md)
- [Memory Architecture](./architecture/MEMORY_ARCHITECTURE.md)
- [Scheduler Design](./architecture/SCHEDULER_DESIGN.md)
