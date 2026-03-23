# CLOVE v2 — Architecture

> 86 syscalls | 14 libraries | 22 syscall modules | C++23

---

## System Overview

```
                    ┌─────────────────────────────────┐
                    │         Agent Processes          │
                    │  (Python, Node, Go, any lang)    │
                    └──────────────┬──────────────────┘
                                   │ Unix Socket (/tmp/clove.sock)
                                   │ 17-byte header + JSON payload
                    ┌──────────────▼──────────────────┐
                    │         CLOVE KERNEL             │
                    │                                  │
                    │  ┌──────────────────────────┐   │
                    │  │     Syscall Router        │   │
                    │  │  ┌────────────────────┐  │   │
                    │  │  │  Budget Middleware  │  │   │
                    │  │  │  (step + time chk) │  │   │
                    │  │  └────────┬───────────┘  │   │
                    │  │           │ dispatch      │   │
                    │  │  ┌────────▼───────────┐  │   │
                    │  │  │  22 Syscall Modules │  │   │
                    │  │  │  (86 handlers)      │  │   │
                    │  │  └────────────────────┘  │   │
                    │  └──────────────────────────┘   │
                    │                                  │
                    │  ┌──────────────────────────┐   │
                    │  │     KernelContext         │   │
                    │  │  (dependency injection)   │   │
                    │  └──────────────────────────┘   │
                    │         │        │        │      │
                    └─────────┼────────┼────────┼──────┘
                              │        │        │
              ┌───────────────┤        │        ├────────────────┐
              │               │        │        │                │
    ┌─────────▼────┐  ┌──────▼───┐ ┌──▼─────┐ ┌▼──────────┐ ┌──▼──────┐
    │  Isolation    │  │ Orchestr │ │Context │ │Governance  │ │  Integ  │
    │              │  │          │ │        │ │            │ │         │
    │ namespaces   │  │scheduler │ │artifacts│ │permissions │ │OpenRouter│
    │ cgroups v2   │  │llm_queue │ │chains  │ │audit_log   │ │MCP      │
    │ landlock     │  │state_str │ │mem_blks│ │pii_filter  │ │A2A      │
    │ seccomp      │  │event_bus │ │ctx_asm │ │exec_replay │ │OTel     │
    │ egress proxy │  │mailbox   │ │        │ │budget_enf  │ │tunnels  │
    └──────────────┘  └──────────┘ └────────┘ └────────────┘ └─────────┘
                              │        │
                      ┌───────▼────────▼───────┐
                      │     Persistence        │
                      │  SQLite (WAL mode)     │
                      │  ┌──────┬──────┬─────┐ │
                      │  │state │arts  │mem  │ │
                      │  │store │chains│blks │ │
                      │  │audit │      │     │ │
                      │  └──────┴──────┴─────┘ │
                      └────────────────────────┘
```

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

## Data Flow: SYS_THINK (with full pipeline)

```
Agent: client.think_with_context(prompt, chain_id)
         │
         ▼
    ┌─────────────┐
    │ SyscallRouter│──→ Budget check (step + time)
    └──────┬──────┘
           ▼
    ┌─────────────┐    ┌──────────────────────────────────┐
    │ LlmSyscalls │───→│ ContextAssembler.assemble()       │
    │             │    │  1. SYSTEM memory blocks (pinned)  │
    │             │    │  2. CORE memory blocks (always)    │
    │             │    │  3. FINAL artifacts from chain     │
    │             │    │  4. RECALL blocks (if space)       │
    │             │    │  5. Observation masking (compress)  │
    │             │    └──────────────┬───────────────────┘
    │             │                   │ assembled context
    │             │◄──────────────────┘
    │             │
    │  ┌──────────▼──────────┐
    │  │ Scheduler: WAITING_LLM │
    │  └──────────┬──────────┘
    │             ▼
    │  ┌──────────────────┐
    │  │ PrivacyFilter    │──→ PII scan/redact/block
    │  └──────┬───────────┘
    │         ▼
    │  ┌──────────────────┐
    │  │ InferenceGateway │──→ Model allowlist + cost limit
    │  └──────┬───────────┘
    │         ▼
    │  ┌──────────────────┐
    │  │ LlmQueue         │──→ Worker thread
    │  │ (8 workers)      │    │
    │  └──────────────────┘    ▼
    │                    ┌──────────────┐
    │                    │ OpenRouter   │──→ HTTPS POST
    │                    │ (300+ models)│    openrouter.ai/api/v1
    │                    └──────┬───────┘
    │                           ▼
    │  ┌────────────────────────────────────┐
    │  │ Budget tracking:                    │
    │  │  budget.record_tokens(response.tok) │
    │  │  budget.record_cost(response.usd)   │
    │  │  if exceeded → BUDGET_EXCEEDED event│
    │  └────────────────────────────────────┘
    │             │
    │  ┌──────────▼──────────┐
    │  │ Scheduler: READY     │
    │  └──────────┬──────────┘
    │             ▼
    └──→ Response to agent: {content, tokens, cost_usd, model}
```

---

## Isolation Model

```
┌───────────────────────────────────────────────────┐
│ Host OS                                            │
│                                                    │
│  ┌─────────────────────────────────────────────┐  │
│  │ CLOVE Kernel Process                         │  │
│  │                                              │  │
│  │  ┌─────────────┐  ┌─────────────┐           │  │
│  │  │ Agent 1     │  │ Agent 2     │           │  │
│  │  │             │  │             │           │  │
│  │  │ PID NS  ✓   │  │ PID NS  ✓   │  ...     │  │
│  │  │ MNT NS  ✓   │  │ MNT NS  ✓   │           │  │
│  │  │ UTS NS  ✓   │  │ UTS NS  ✓   │           │  │
│  │  │ NET NS  ✓   │  │ NET NS  ✓   │           │  │
│  │  │             │  │             │           │  │
│  │  │ cgroups v2  │  │ cgroups v2  │           │  │
│  │  │ mem: 256MB  │  │ mem: 512MB  │           │  │
│  │  │ cpu: 50%    │  │ cpu: 100%   │           │  │
│  │  │ pids: 64    │  │ pids: 128   │           │  │
│  │  │             │  │             │           │  │
│  │  │ Landlock    │  │ Landlock    │           │  │
│  │  │ /tmp: rw    │  │ /data: ro   │           │  │
│  │  │ /data: ro   │  │             │           │  │
│  │  │             │  │             │           │  │
│  │  │ seccomp BPF │  │ seccomp BPF │           │  │
│  │  │ 27 blocked  │  │ 27 blocked  │           │  │
│  │  └─────────────┘  └─────────────┘           │  │
│  │                                              │  │
│  │  Kernel enforces:                            │  │
│  │  - Per-agent permissions (RBAC)              │  │
│  │  - Budget limits (token/step/time/cost)      │  │
│  │  - PII filtering on LLM calls                │  │
│  │  - Domain allowlists for HTTP                │  │
│  │  - Command blocklists for EXEC               │  │
│  └─────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────┘
```

---

## Memory Architecture (3-Tier)

```
┌───────────────────────────────────────────┐
│ Tier 1: Working Memory (LLM Context)      │
│ Location: In the prompt sent to LLM       │
│ Size: 4K─200K tokens (model-dependent)    │
│                                           │
│ Assembled by ContextAssembler:            │
│  ┌─────────────────────────────────────┐  │
│  │ [SYSTEM] Agent persona (pinned)     │  │
│  │ [CORE] Current task state           │  │
│  │ [CORE] Findings so far              │  │
│  │ ─── Shared Context ───              │  │
│  │ ### Research: Market Analysis        │  │
│  │ ### Research: Tech Landscape         │  │
│  │ [RECALL] Reference data             │  │
│  │ ─── Private Notes ───               │  │
│  │ ### Draft: My observations          │  │
│  │ ─── Task Instruction ───            │  │
│  │ "Synthesize the above research..." │  │
│  └─────────────────────────────────────┘  │
├───────────────────────────────────────────┤
│ Tier 2: Session Memory (Kernel RAM)       │
│ Location: In-memory stores                │
│ Latency: <1ms                             │
│                                           │
│ MemoryBlockStore  ArtifactStore  StateStore│
│ ┌────────────┐  ┌────────────┐  ┌───────┐│
│ │ SYSTEM blk │  │ art_abc123 │  │ key:v ││
│ │ CORE blk   │  │ art_def456 │  │ key:v ││
│ │ RECALL blk │  │ art_ghi789 │  │ key:v ││
│ └────────────┘  └────────────┘  └───────┘│
├───────────────────────────────────────────┤
│ Tier 3: Persistent Memory (SQLite)        │
│ Location: clove.db                        │
│ Latency: 1─10ms                           │
│                                           │
│ Tables:                                   │
│ ┌────────────┬──────────┬───────────────┐│
│ │ artifacts  │ chains   │ memory_blocks ││
│ │ state_store│ audit_log│               ││
│ └────────────┴──────────┴───────────────┘│
│ Write-through on every mutation           │
│ Boot-loaded into Tier 2 on kernel start   │
└───────────────────────────────────────────┘
```

---

## Agent Scheduler

```
Priority Queue (lower number = higher priority)

  CRITICAL (0) ──→ ┌─────┐ ┌─────┐
                    │ A:1 │ │ A:5 │  ← served first
                    └─────┘ └─────┘
  HIGH (1) ──────→ ┌─────┐
                    │ A:3 │
                    └─────┘
  NORMAL (2) ────→ ┌─────┐ ┌─────┐ ┌─────┐
                    │ A:2 │ │ A:7 │ │ A:9 │  ← FIFO within level
                    └─────┘ └─────┘ └─────┘
  LOW (3) ───────→ ┌─────┐
                    │ A:4 │
                    └─────┘
  IDLE (4) ──────→ (empty)

State tracking per agent:
  IDLE ──→ READY ──→ WAITING_LLM ──→ READY ──→ WAITING_TOOL ──→ READY ──→ COMPLETED
                          │                          │
                     (SYS_THINK)                (SYS_HTTP/EXEC)

Stats tracked: llm_calls, tool_calls, total_llm_wait_ms, total_tool_wait_ms
```

---

## Budget Enforcement Pipeline

```
Every syscall enters SyscallRouter::handle()
         │
         ▼
    ┌──────────────────────────────┐
    │ 1. budget.record_step()      │  ← increment step counter
    │ 2. budget.check_steps()      │  ← max_steps exceeded?
    │ 3. budget.check_time(now)    │  ← max_time_ms exceeded?
    └──────────┬───────────────────┘
               │
          pass │                fail
               │                  │
               ▼                  ▼
        Dispatch to          Return error
        handler              + emit BUDGET_EXCEEDED
               │              + audit log
               ▼              + kill if kill_on_exceeded
        (after SYS_THINK)
               │
    ┌──────────▼───────────────────┐
    │ 4. budget.record_tokens(N)   │  ← from LLM response
    │ 5. budget.record_cost($)     │  ← from LLM response
    │ 6. budget.check_tokens()     │  ← max_tokens exceeded?
    │ 7. budget.check_cost()       │  ← max_cost_usd exceeded?
    └──────────────────────────────┘

Time budget: checked every reactor poll (~100ms) in Kernel::run()
```

---

## Library Dependency Graph

```
core ◄──────────────────────────────────────────────────────────┐
  │                                                              │
  ├──→ reactor                                                   │
  │                                                              │
  ├──→ ipc ──→ (uses core for protocol)                         │
  │                                                              │
  ├──→ orchestration ──→ (state, events, mailbox, scheduler)    │
  │         │                                                    │
  │         └──→ agents ──→ sandbox                              │
  │                                                              │
  ├──→ context ──→ (artifacts, chains, memory blocks, assembler)│
  │                                                              │
  ├──→ governance ──→ (permissions, audit, pii, policy, budget) │
  │                                                              │
  ├──→ integrations ──→ (openrouter, mcp, a2a, tunnel, otel)   │
  │                                                              │
  ├──→ persistence ──→ (sqlite: context + governance)           │
  │                                                              │
  ├──→ worlds ──→ (isolated realms)                              │
  │                                                              │
  ├──→ api ──→ (REST + dashboard)                                │
  │                                                              │
  └──→ kernel ──→ (wires everything, 22 syscall modules)────────┘
```

---

## Wire Protocol

```
┌──────────┬──────────┬────────┬──────────────┬─────────────────┐
│  magic   │ agent_id │ opcode │ payload_size │    payload      │
│  4 bytes │ 4 bytes  │ 1 byte │   8 bytes    │ variable (JSON) │
│ "AGNT"   │ uint32   │ uint8  │   uint64     │ max 1 MB        │
└──────────┴──────────┴────────┴──────────────┴─────────────────┘
             17-byte header                    UTF-8 JSON body
```

---

## File Structure

```
clove-v2/
├── kernel/src/
│   ├── main.cpp                 # Entry point
│   ├── kernel.cpp/hpp           # Kernel class (wiring)
│   ├── context.hpp              # KernelContext (DI)
│   ├── syscall_router.cpp/hpp   # Dispatch + budget middleware
│   └── syscalls/
│       ├── mod.hpp              # 22 module class declarations
│       ├── state_syscalls.cpp   # STORE/FETCH/DELETE/KEYS
│       ├── ipc_syscalls.cpp     # SEND/RECV/BROADCAST/REGISTER
│       ├── event_syscalls.cpp   # SUBSCRIBE/EMIT/POLL
│       ├── agent_syscalls.cpp   # SPAWN/KILL/LIST/PAUSE/RESUME
│       ├── llm_syscalls.cpp     # THINK + context assembly + budget tracking
│       ├── context_syscalls.cpp # DOC/CHAIN ops + persistence
│       ├── memory_syscalls.cpp  # MEM ops + persistence
│       ├── budget_syscalls.cpp  # SET/GET_BUDGET + SET/GET_PRIORITY
│       ├── replay_syscalls.cpp  # RECORD + REPLAY (with on_tick playback)
│       └── ... (13 more)
├── libs/
│   ├── core/          # Protocol, config, types
│   ├── reactor/       # Event loop (epoll/kqueue)
│   ├── ipc/           # Unix socket server/client
│   ├── agents/        # Process management
│   ├── sandbox/       # Namespaces, cgroups, Landlock, seccomp
│   ├── orchestration/ # Scheduler, LLM queue, state, events, mailbox
│   ├── context/       # Artifacts, chains, memory blocks, assembler
│   ├── governance/    # Permissions, audit, PII, policy, budget
│   ├── integrations/  # OpenRouter, MCP, A2A, tunnels, OTel
│   ├── persistence/   # SQLite (5 tables)
│   ├── worlds/        # Multi-tenant isolation
│   └── api/           # REST API + HTMX dashboard
├── cli/src/           # CLI binary
├── sdk/python/        # Python SDK (86 methods + Agent class)
├── tests/             # 19 test files (Catch2)
├── deploy/            # Dockerfile, docker-compose, systemd, install.sh
├── docs/              # 8 docs (capabilities, roadmap, architecture, research)
└── examples/          # OpenClaw demo, 10-agent research station
```
