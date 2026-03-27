# CLOVE v2 — Complete Capabilities Reference

**Version:** 2.0.0
**Language:** C++23
**Codebase:** 19,737+ LOC (119 source files) + 878 LOC Python SDK + 1,305 LOC TypeScript SDK
**Libraries:** 14 modular static libraries + 2 binaries (kernel, CLI)
**Syscalls:** 86 opcodes over binary IPC protocol
**Tests:** 165 (Catch2)
**REST API:** 71 endpoints including agent runner, fleet, SSE streaming

---

## 1. Process Isolation & Sandboxing

### Linux Namespace Isolation
- **PID namespace** — Agent cannot see or signal other processes on host
- **Network namespace** — Agent has no network access unless explicitly granted
- **Mount namespace** — Agent gets its own filesystem view, cannot access host mounts
- **UTS namespace** — Agent gets its own hostname (`clove-<name>`)
- Uses `clone()` with `CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWNET`
- Graceful degradation: falls back to `fork()` if no root/CAP_SYS_ADMIN

### cgroups v2 Resource Limits
- **Memory limit** — Hard cap per agent (default 256MB), kernel OOM-kills on violation
- **CPU quota** — Microsecond-precision CPU time limits per period
- **CPU weight** — Fair scheduling between agents (shares-based)
- **PID limit** — Max child processes per agent (default 64)
- Hot-reloadable — update limits on running agents without restart
- Writes to `/sys/fs/cgroup/clove/<agent>/memory.max`, `cpu.max`, `pids.max`

### Landlock Filesystem Restriction (Linux)
- LSM-based filesystem access control
- Configurable per-agent `allowed_paths` (read + execute) and `writable_paths` (full access)
- Handles: `READ_FILE`, `READ_DIR`, `WRITE_FILE`, `REMOVE_FILE`, `REMOVE_DIR`, `MAKE_REG`, `MAKE_DIR`, `EXECUTE`
- Applied in child process before `execvp()` — no escape window
- Uses direct syscall wrappers (`landlock_create_ruleset`, `landlock_add_rule`, `landlock_restrict_self`)

### seccomp BPF Syscall Filtering (Linux)
- BPF filter blocking 27 dangerous Linux syscalls across 7 categories:
  - **Debugging:** `ptrace`, `process_vm_readv`, `process_vm_writev`
  - **Kernel replacement:** `kexec_load`, `kexec_file_load`
  - **Kernel modules:** `init_module`, `finit_module`, `delete_module`
  - **System ops:** `reboot`, `swapon`, `swapoff`, `acct`
  - **Filesystem mounting:** `mount`, `umount2`, `pivot_root`, `chroot`
  - **Clock manipulation:** `settimeofday`, `clock_settime`
  - **Namespace escape:** `unshare`, `setns`, `bpf`, `userfaultfd`, `perf_event_open`
  - **Keyring:** `add_key`, `request_key`, `keyctl`
- Default-allow policy — agents can still do I/O, networking, file access
- `PR_SET_NO_NEW_PRIVS` set before filter installation

### Isolation Status Tracking
- Per-sandbox `IsolationStatus` struct tracks what's active vs degraded
- Reports: `pid_namespace`, `net_namespace`, `mnt_namespace`, `uts_namespace`, `cgroups_available`, `memory_limit_applied`, `cpu_quota_applied`, `pids_limit_applied`, `landlock_active`, `seccomp_active`, `fully_isolated`
- Degradation reasons logged (e.g., "cgroup v2 not available", "clone() failed — need root")

### macOS Support
- Fork-only mode (no namespaces on macOS)
- Egress proxy env vars set for defense-in-depth
- Sandbox auto-disabled with warning on macOS

---

## 2. Agent Lifecycle Management

### Spawning
- `SYS_SPAWN` (0x10) — Spawn agent process from command + args
- Each agent gets a unique monotonic ID (uint32)
- Agent wrapped in `Sandbox` with configurable isolation
- Parent-child tracking (agent trees)

### Lifecycle Control
- `SYS_KILL` (0x11) — SIGTERM → wait → SIGKILL (graceful shutdown with timeout)
- `SYS_PAUSE` (0x14) — SIGSTOP (freeze agent)
- `SYS_RESUME` (0x15) — SIGCONT (unfreeze agent)
- `SYS_LIST` (0x12) — List all agents with state, PID, name

### Restart Policies
- **NEVER** — Agent stays dead
- **ALWAYS** — Restart on any exit
- **ON_FAILURE** — Restart only on non-zero exit
- Exponential backoff: initial 1s, multiplier 2.0, max 30s
- `reap_and_restart_agents()` called every reactor tick

### Agent Metrics
- Per-agent: memory_bytes, cpu_percent, uptime_seconds, llm_request_count, llm_tokens_used
- Per-agent PID, state, parent_id, child_ids, created_at
- macOS: `mach_task_basic_info` for RSS/virtual memory
- Linux: `/proc/self/status` for VmRSS/VmSize

### SIGCHLD Handling
- Self-pipe pattern for async child reaping
- Reactor-integrated via pipe fd
- `waitpid(-1, &status, WNOHANG)` in non-blocking loop

---

## 3. Inter-Process Communication

### Binary Protocol
- 17-byte packed header: magic (4B `0x41474E54` "AGNT"), agent_id (4B), opcode (1B), payload_size (8B)
- Little-endian wire format
- Max payload: 1MB
- Unix domain socket transport (`/tmp/clove.sock` default)

### Mailbox IPC
- `SYS_SEND` (0x20) — Point-to-point message to agent by ID
- `SYS_RECV` (0x21) — Receive pending messages (configurable max)
- `SYS_BROADCAST` (0x22) — Send to all agents
- `SYS_REGISTER` (0x23) — Register a human-readable name for name-based routing
- Per-agent message queues with configurable limits

### Event Bus (Pub/Sub)
- `SYS_SUBSCRIBE` (0x60) — Subscribe to event type
- `SYS_UNSUBSCRIBE` (0x61) — Unsubscribe
- `SYS_POLL_EVENTS` (0x62) — Poll pending events (max 50 per call)
- `SYS_EMIT` (0x63) — Emit event to all subscribers
- 12 built-in event types: `AGENT_SPAWNED`, `AGENT_EXITED`, `AGENT_PAUSED`, `AGENT_RESUMED`, `AGENT_RESTARTING`, `AGENT_ESCALATED`, `MESSAGE_RECEIVED`, `STATE_CHANGED`, `SYSCALL_BLOCKED`, `RESOURCE_WARNING`, `POLICY_UPDATED`, `CUSTOM`
- Per-agent event queues capped at 1,000

### Reactor (Event Loop)
- kqueue (macOS) / epoll (Linux)
- Non-blocking I/O for all socket operations
- Per-client write buffering with state tracking to avoid redundant kqueue modify calls
- Module `on_tick()` called every iteration

---

## 4. State Management

### Key-Value State Store
- `SYS_STORE` (0x30) — Store key/value with optional TTL
- `SYS_FETCH` (0x31) — Fetch by key (checks scope and TTL)
- `SYS_DELETE` (0x32) — Delete key (owner or global only)
- `SYS_KEYS` (0x33) — List keys with prefix filter
- Scopes: `global` (shared), `agent` (private to owner), `session`
- TTL eviction: `evict_expired()` called every reactor tick
- JSON values (any nlohmann::json type)

### SQLite Persistence
- Write-through to SQLite for state and audit durability
- `StateStoreDb` — persists state entries, loads on startup
- `AuditStore` — persists audit log entries
- Migrations system (4 migration files)
- Configurable via `--db <path>` (default: `clove.db`)

---

## 5. LLM Integration

### OpenRouter Gateway (300+ Models)
- `SYS_THINK` (0x01) — Send prompt, get completion
- Supports all OpenRouter models: GPT-4, Claude, Gemini, Llama, Mistral, etc.
- `chat()` and `chat_messages()` methods
- `list_models()` — query available models
- `get_credits()` — check OpenRouter balance
- Response parsing with token counts, cost, model used
- Configurable via `--openrouter --openrouter-key <key>`

### Inference Gateway
- `SYS_LLM_CONFIG` (0xD0) — Get/set LLM configuration
- `SYS_LLM_REPORT` (0xF0) — LLM usage report
- Model allowlists (restrict which models agents can use)
- Provider allowlists (restrict which providers)
- **Cost tracking** — `record_cost()` on every LLM call, `current_cost_usd` tracked
- **Budget enforcement** — `max_cost_usd` hard limit, blocks calls when exceeded
- Hot-reloadable via policy file or `SYS_POLICY_UPDATE`

### LLM Queue
- Multi-threaded worker pool (default 8 workers)
- `SYS_ASYNC_POLL` (0x80) — Non-blocking LLM calls
- Queue tracks `total_requests`, `total_completed`
- PII filter applied before sending to LLM provider

---

## 6. Security & Governance

### Per-Agent Permissions (RBAC)
- `SYS_GET_PERMS` (0x40) — Get agent permissions
- `SYS_SET_PERMS` (0x41) — Set agent permissions
- `SYS_AUTH` (0x42) — Check if agent exists, return permissions
- 6 capability flags: `can_exec`, `can_read`, `can_write`, `can_think`, `can_spawn`, `can_http`
- Path ACLs (allowed file paths with glob matching)
- Command ACLs (allowed commands)
- Domain ACLs (allowed HTTP domains)
- LLM quotas per agent
- Permission levels: `UNRESTRICTED`, `STANDARD`, `SANDBOXED`, `READONLY`, `MINIMAL`

### PII Detection & Redaction
- `SYS_PII_SCAN` (0xD1) — Scan text, return match positions and types
- `SYS_PII_REDACT` (0xD2) — Redact PII from text
- Regex patterns for: SSN, email, phone, credit card, IP address
- Modes: `AUDIT` (log only), `REDACT` (replace with `[REDACTED]`), `BLOCK` (reject entirely)
- Applied automatically before LLM calls when enabled
- 100% detection rate, 0 false positives (benchmarked)

### Audit Logger
- `SYS_GET_AUDIT_LOG` (0x76) — Query audit entries with filters
- `SYS_SET_AUDIT_CONFIG` (0x77) — Enable/disable categories, set max entries
- 8 categories: `SECURITY`, `AGENT_LIFECYCLE`, `IPC`, `STATE_STORE`, `RESOURCE`, `SYSCALL`, `NETWORK`, `WORLD`
- Ring buffer (default 10,000 entries)
- JSONL export for compliance
- Structured entries: id, timestamp, category, event_type, agent_id, agent_name, details, success

### Policy System
- `SYS_POLICY_UPDATE` (0x43) — Hot-update LLM/privacy/agent policies via JSON
- `SYS_POLICY_RECOMMEND` (0xD7) — AI-generated policy recommendations from denial patterns
- File watcher (kqueue/inotify) for hot-reload from policy JSON file
- Denial aggregation → automatic recommendations ("allow domain arxiv.org — denied 15 times")
- Deployment manifests (`manifest.yaml`) for declarative fleet configuration

### Credential Store
- `SYS_CREDS_GET` (0xD8) — Retrieve credentials by name
- Kernel-managed secrets

---

## 7. Execution Replay

### Recording
- `SYS_RECORD_START` (0x70) — Start recording syscalls
- `SYS_RECORD_STOP` (0x71) — Stop recording
- `SYS_RECORD_STATUS` (0x72) — Get recording state and entry count
- Configurable: `max_entries`, `include_think`, `include_http`, `include_exec`
- Per-syscall timing (microsecond precision)
- Opcode filtering (skip NOOP, HELLO, EXIT, recording control ops)

### Playback
- `SYS_REPLAY_START` (0x73) — Load recorded entries, begin replay
- `SYS_REPLAY_STATUS` (0x74) — Report progress (state, total, replayed)
- Steps one entry per reactor tick via `on_tick()`
- Re-injects recorded messages through `SyscallRouter`
- States: `IDLE`, `RUNNING`, `PAUSED`, `COMPLETED`, `ERROR`

---

## 8. Networking

### HTTP Egress
- `SYS_HTTP` (0x50) — Make HTTP requests (controlled by permissions)
- Domain allowlist enforcement
- SSRF guard (blocks private/loopback/metadata IPs)
- Configurable egress proxy

### File I/O
- `SYS_READ` (0x03) — Read file (path checked against permissions)
- `SYS_WRITE` (0x04) — Write file (path checked against permissions)
- `SYS_EXEC` (0x02) — Execute command (sandboxed, command checked against ACL)

---

## 9. Observability

### System Metrics
- `SYS_METRICS_SYSTEM` (0xC0) — Kernel process metrics (RSS, virtual memory, CPU time, max RSS)
- `SYS_METRICS_AGENT` (0xC1) — Per-agent metrics
- `SYS_METRICS_ALL_AGENTS` (0xC2) — All agents metrics
- `SYS_METRICS_CGROUP` (0xC3) — cgroup stats (Linux) / N/A (macOS)
- macOS: `mach_task_basic_info` for memory
- Linux: `/proc/self/status`, `/sys/fs/cgroup/` for cgroup stats

### OpenTelemetry
- `SYS_OTEL_SPAN` (0xD9) — Create OTel spans for distributed tracing
- Export to Datadog, Grafana, New Relic, Jaeger

---

## 10. Integrations

### MCP Bridge (Model Context Protocol)
- `SYS_MCP_LIST` (0xD4) — List available MCP tools
- `SYS_MCP_CALL` (0xD3) — Call an MCP tool
- Subprocess management for MCP servers (stdio transport)
- JSON-RPC communication with MCP protocol
- Agent-level access control (allowed_agents per server)
- Tool discovery, session management, connection pooling

### A2A Bridge (Agent-to-Agent Protocol)
- `SYS_A2A_SEND` (0xD5) — Send message to external A2A agent
- `SYS_A2A_RECV` (0xD6) — Receive inbound A2A messages
- HTTP server for A2A agent card discovery
- Register/unregister CLOVE agents for external visibility
- Cross-platform agent communication

### Tunnel Bridge (Remote Agent Bridging)
- `SYS_TUNNEL_CONNECT` (0xB0) — Connect to relay server
- `SYS_TUNNEL_DISCONNECT` (0xB1) — Disconnect
- `SYS_TUNNEL_STATUS` (0xB2) — Connection state
- `SYS_TUNNEL_LIST_REMOTES` (0xB3) — List remote machines
- `SYS_TUNNEL_CONFIG` (0xB4) — Update tunnel config
- Framework-only (client-side state, ready for real relay server)

---

## 11. Multi-Tenant Worlds

### World Engine
- `SYS_WORLD_CREATE` (0xA0) — Create isolated world (own StateStore + EventBus)
- `SYS_WORLD_DESTROY` (0xA1) — Destroy world
- `SYS_WORLD_LIST` (0xA2) — List all worlds
- `SYS_WORLD_JOIN` (0xA3) — Join agent to world
- `SYS_WORLD_LEAVE` (0xA4) — Remove agent from world
- `SYS_WORLD_EVENT` (0xA5) — World-scoped event emission
- `SYS_WORLD_STATE` (0xA6) — World-scoped state get/set
- `SYS_WORLD_SNAPSHOT` (0xA7) — Serialize entire world to JSON
- `SYS_WORLD_RESTORE` (0xA8) — Restore world from snapshot
- True isolation: each world gets its own StateStore and EventBus (not namespace prefixing)
- Use case: multi-tenant agent fleets, simulation environments, testing

---

## 12. REST API

### 24 Endpoints
| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/health` | Status, version, uptime (no auth required) |
| GET | `/api/agents` | List all agents |
| POST | `/api/agents` | Spawn agent |
| DELETE | `/api/agents/:id` | Kill agent |
| GET | `/api/agents/:id/metrics` | Agent metrics |
| GET | `/api/agents/:id/permissions` | Agent permissions |
| PUT | `/api/agents/:id/permissions` | Update permissions (delta merge) |
| GET | `/api/audit` | Query audit log (filter by category, agent, limit) |
| GET | `/api/audit/export` | JSONL export |
| GET | `/api/replay` | Recording status |
| POST | `/api/replay/start` | Start recording |
| POST | `/api/replay/stop` | Stop recording |
| GET | `/api/inference` | Inference gateway config |
| PUT | `/api/inference` | Update inference config |
| POST | `/api/privacy/scan` | PII scan |
| GET | `/api/policy/recommendations` | Policy recommendations |
| GET | `/api/metrics` | System metrics |
| GET | `/api/mcp/servers` | MCP server status |
| GET | `/api/mcp/tools` | Available MCP tools |
| GET | `/api/worlds` | List worlds |
| POST | `/api/worlds` | Create world |
| DELETE | `/api/worlds/:id` | Destroy world |
| GET | `/dashboard` | HTMX dashboard |
| GET | `/dashboard/agents` | Agent management page |

### Authentication
- Bearer token middleware (`Authorization: Bearer <key>`)
- Skipped for `/api/health` and dashboard routes
- Configurable via `--api-key <key>`

### HTMX Dashboard
- Dark theme (GitHub dark colors)
- Live agent list (polls every 3s)
- System metrics panel (polls every 5s)
- Audit log viewer with category filters
- Agent management (spawn form, kill buttons)
- Embedded as C++ raw string literals (no separate server)

---

## 13. CLI

### 13 Commands
```
clove status                         — kernel health and uptime
clove agents / clove ps              — list running agents (table format)
clove spawn <name> <command>         — spawn agent
clove kill <id>                      — kill agent
clove metrics [agent_id]             — system or per-agent metrics
clove audit [--category X] [--limit] — query audit log
clove record start|stop|status       — execution recording control
clove worlds                         — list worlds
clove worlds create <name>           — create world
clove worlds destroy <id>            — destroy world
clove pii <text>                     — PII scan
clove recommendations                — policy recommendations
clove help                           — usage
```

### Global Options
- `--host <host>` (default: localhost)
- `--port <port>` (default: 8080)
- `--api-key <key>` (or `CLOVE_API_KEY` env var)

---

## 14. Context Layer (Chain of Docs)

### Artifacts
- Typed documents: QUERY, RESEARCH, ANALYSIS, SYNTHESIS, REPORT, NOTE, PLAN
- Lifecycle state machine: DRAFT → IN_REVIEW → APPROVED → FINAL → ARCHIVED
- Forward-only state transitions, author-only mutations
- DAG provenance via `parent_ids` — tracks what was built from what
- JSON metadata (tokens_used, cost_usd, model, duration_ms)
- Thread-safe in-memory store with SQLite write-through persistence

### Chains
- Ordered sequences of artifacts forming provenance DAGs
- Fork support — branch a chain at any artifact point
- Per-chain metadata and creator tracking

### Context Assembly
- Kernel auto-assembles agent context from chain artifacts
- Three layers: L1 (shared FINAL/APPROVED), L2 (chain lineage), L3 (private DRAFTs)
- Token budgeting with truncation reporting
- Position-aware: critical info at context boundaries

### Context-Aware LLM Calls
- `SYS_THINK` accepts `auto_context: true` + `chain_id`
- Kernel prepends assembled context before sending to LLM
- No manual context management needed by agents

### Syscalls (9 opcodes: 0xE0–0xE8)
- DOC_CREATE, DOC_READ, DOC_UPDATE, DOC_LIST, DOC_DELETE
- CHAIN_CREATE, CHAIN_GET, CHAIN_FORK
- CONTEXT_ASSEMBLE

---

## 15. Four-Budget Enforcement

### AgentBudget (kernel-enforced limits)
- **Token budget** — max tokens per agent, checked before/after SYS_THINK
- **Step budget** — max syscall count, checked on every syscall dispatch
- **Time budget** — max wall-clock time, checked every reactor poll (100ms)
- **Cost budget** — max USD spend, tracked from LLM response metadata
- `kill_on_exceeded` flag for automatic agent termination
- Budget middleware in SyscallRouter — zero-overhead when no budget set
- Events: BUDGET_EXCEEDED, BUDGET_WARNING emitted to event bus
- Syscalls: SYS_SET_BUDGET (0xF1), SYS_GET_BUDGET (0xF2)

---

## 16. Agent Scheduler

### Priority-Based Scheduling
- 5 priority levels: CRITICAL, HIGH, NORMAL, LOW, IDLE
- FIFO ordering within same priority level
- State tracking per agent: IDLE, READY, WAITING_LLM, WAITING_TOOL, COMPLETED
- Automatic state transitions:
  - SYS_THINK → mark_waiting_llm → mark_ready on response
  - SYS_HTTP/SYS_EXEC → mark_waiting_tool → mark_ready on response
- Per-agent stats: llm_calls, tool_calls, total_llm_wait_ms, total_tool_wait_ms
- Scheduler stats JSON exposed via SYS_GET_PRIORITY
- Syscalls: SYS_SET_PRIORITY (0xF3), SYS_GET_PRIORITY (0xF4)

---

## 17. Python SDK

### CloveClient (86 syscall methods)
- Binary protocol: `struct.pack('<IIBq', magic, agent_id, opcode, payload_size)`
- Unix domain socket transport
- Context manager (`with CloveClient() as client:`)
- Methods for every syscall category: core, state, IPC, events, LLM, permissions, agents, file I/O, HTTP, PII, metrics, audit, recording, replay, worlds, MCP, context/artifacts/chains

### Agent Class
- High-level `Agent` class with `@agent.tool` decorator
- Auto-registration, polling event loop
- Overridable `_handle_message()` and `_handle_event()`

---

## 15. Deployment

### Docker
- Multi-stage Alpine build (builder + runtime)
- Runtime image: Alpine + libstdc++ + openssl + libcurl + sqlite
- Exposes ports 8080 (API) and 8081 (A2A)
- Volume mount for persistent data

### docker-compose
- Single service with ports, volumes, env vars
- `CLOVE_API_KEY` and `OPENROUTER_API_KEY` from environment
- `restart: unless-stopped`

### systemd
- Hardened service file
- `NoNewPrivileges`, `ProtectSystem=strict`, `ProtectHome`, `PrivateTmp`
- Dedicated `clove` system user
- `RuntimeDirectory=clove`, `StateDirectory=clove`

### Installer
- `install.sh` — Linux/macOS
- Builds from source if needed
- Creates system user, data directories
- Installs systemd service on Linux

---

## 16. Testing

- **165 test cases**, **292 assertions**
- Framework: Catch2 v3.5.2
- 16 test files covering: protocol, state store, mailbox, event bus, permissions, privacy filter, audit log, execution log, inference gateway, policy recommender, MCP bridge, database/persistence, tunnel bridge, world engine, A2A bridge, execution replay

---

## 17. Kernel Boot Configuration

### CLI Flags
```
--socket <path>              Unix socket path (default: /tmp/clove.sock)
--no-sandbox                 Disable sandboxing
--llm-proxy                  Enable inference gateway
--llm-allowed-providers X    Comma-separated provider allowlist
--llm-allowed-models X       Comma-separated model allowlist
--llm-max-cost <usd>         Max LLM spend
--privacy                    Enable PII filter
--privacy-mode <mode>        audit | redact | block
--egress-proxy               Enable HTTP egress proxy
--egress-port <port>         Egress proxy port
--policy-file <path>         Policy JSON file (hot-reloaded)
--manifest <path>            Deployment manifest
--db <path>                  SQLite database path
--api                        Enable REST API
--api-port <port>            API port (default: 8080)
--api-key <key>              API authentication key
--openrouter                 Enable OpenRouter
--openrouter-key <key>       OpenRouter API key
--otel                       Enable OpenTelemetry
--otel-endpoint <url>        OTel collector endpoint
--mcp                        Enable MCP bridge
--a2a                        Enable A2A bridge
```

---

---

# CLOVE vs OpenShell — Detailed Comparison

## Architecture

| Aspect | CLOVE | OpenShell |
|--------|-------|-----------|
| Language | C++23 | Rust |
| Isolation model | Linux namespaces + cgroups (native) | Docker + K3s (containerized) |
| Filesystem restriction | Landlock LSM | Landlock LSM |
| Syscall filtering | seccomp BPF (27 blocked syscalls) | seccomp |
| Network control | Network namespace + egress proxy | Network namespace + egress policies |
| Binary size | ~2 MB | ~3,550 MB (Docker + K3s images) |
| Dependencies | libcurl, openssl, sqlite3 | Docker, K3s, Colima VM |

## Performance

| Metric | CLOVE | OpenShell | Ratio |
|--------|-------|-----------|-------|
| Cold start | 27ms | ~3,300ms | **122x faster** |
| IPC latency | 0.02ms | ~150ms | **7,450x faster** |
| Throughput | 54,000 ops/sec | ~6 ops/sec | **8,750x faster** |
| Idle memory | 2.8 MB | ~907 MB | **324x lighter** |
| Shutdown | 1ms | ~10ms | **10x faster** |

## Multi-Agent Capabilities

| Capability | CLOVE | OpenShell |
|------------|-------|-----------|
| Multi-agent orchestration | Yes (86 syscalls, IPC, event bus, mailboxes) | No (single-agent sandboxing) |
| Agent-to-agent messaging | 0.02ms binary IPC | N/A |
| Shared state store | Built-in KV with TTL, scopes | N/A |
| Pub/sub event bus | 12 event types, per-agent queues | N/A |
| Agent fleet management | Spawn, kill, pause, resume, restart policies | Single sandbox lifecycle |
| World isolation | Multi-tenant worlds with independent state/events | N/A |

## Security Features

| Feature | CLOVE | OpenShell |
|---------|-------|-----------|
| Process isolation | Namespaces (PID/NET/MNT/UTS) | Docker container |
| Resource limits | cgroups v2 (memory, CPU, PIDs) | Container resource limits |
| Filesystem restriction | Landlock | Landlock |
| Syscall filtering | seccomp BPF (27 blocked) | seccomp |
| Per-agent RBAC | 6 capabilities + path/command/domain ACLs | Policy-based (YAML) |
| PII filtering | Regex scanner + redactor (5 patterns) | Privacy Router (differential privacy) |
| Audit trail | 8-category structured audit log + JSONL export | Logging (scope unclear) |
| Execution replay | Full record + playback of all syscalls | No |
| Cost controls | Built-in LLM budget enforcement | No |
| Policy hot-reload | File watcher (kqueue/inotify) + syscall API | Partial (network policies reloadable) |

## LLM Integration

| Feature | CLOVE | OpenShell |
|---------|-------|-----------|
| Models | 300+ via OpenRouter | Nemotron (local) + cloud via Privacy Router |
| Cost tracking | Per-call cost recording, budget enforcement | No |
| PII on prompts | Scan/redact/block before LLM call | Differential privacy stripping |
| Model allowlisting | Per-agent model restrictions | No |

## Observability

| Feature | CLOVE | OpenShell |
|---------|-------|-----------|
| REST API | 24 endpoints | Gateway API |
| Dashboard | HTMX live dashboard (embedded) | No |
| CLI | 13 commands | CLI for sandbox lifecycle |
| OpenTelemetry | Span export to Datadog/Grafana/etc. | No |
| Execution replay | Full syscall-level record/playback | No |
| System metrics | Per-process + per-agent + cgroup metrics | Container metrics |

## Ecosystem

| Feature | CLOVE | OpenShell |
|---------|-------|-----------|
| Python SDK | Full (86 methods, Agent class, @tool decorator) | No native SDK |
| MCP support | Bridge with tool discovery + invocation | No |
| A2A protocol | Bridge for cross-platform agent communication | No |
| Framework support | Any (LangChain, CrewAI, ADK, custom) | Any (runs in container) |
| Deployment | Docker, systemd, install.sh, bare metal | Docker only |

## What OpenShell Has That CLOVE Doesn't

| Feature | Notes |
|---------|-------|
| **NVIDIA ecosystem** | 8 enterprise partners (Adobe, Salesforce, SAP, etc.) |
| **Differential privacy** | Privacy Router uses Gretel tech for statistical PII stripping |
| **GPU passthrough** | NVIDIA GPU access from within sandbox |
| **Enterprise backing** | Jensen Huang endorsement, NVIDIA brand trust |
| **Rust memory safety** | Language-level memory safety guarantees |
| **Production hardening** | Battle-tested Docker/K3s isolation model |

## What CLOVE Has That OpenShell Doesn't

| Feature | Notes |
|---------|-------|
| **Multi-agent coordination** | IPC, mailboxes, event bus, shared state — OpenShell can't do this |
| **Execution replay** | Full record/playback of every syscall — nobody else has this |
| **86 syscall API** | Structured, typed interface for all agent operations |
| **Cost enforcement** | Kernel-level LLM budget limits |
| **World isolation** | Multi-tenant environments with independent state/events |
| **Lightweight** | 2MB binary, 3MB RAM vs 3.5GB images, 900MB RAM |
| **Sub-millisecond IPC** | 0.02ms agent-to-agent, relevant at fleet scale |
| **Policy recommendations** | AI-generated security suggestions from denial patterns |
| **HTMX dashboard** | Built-in live monitoring UI |
| **Python SDK** | Native 86-method SDK with Agent class |

## Summary

**OpenShell** is a well-backed single-agent sandbox built on proven container technology (Docker/K3s) with NVIDIA's brand and enterprise partnerships. Its strength is the container ecosystem and GPU passthrough.

**CLOVE** is a lightweight native kernel that does everything OpenShell does for isolation (at 324x less memory) PLUS multi-agent orchestration, execution replay, cost controls, and a full API/SDK stack. Its weakness is zero users, zero funding, and competing against NVIDIA's distribution.

**The fundamental difference:** OpenShell isolates one agent. CLOVE orchestrates a fleet.
