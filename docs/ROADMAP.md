# CLOVE v2 — Roadmap

> Last updated: 2026-03-25

## Status: 86 syscalls defined, all handler files implemented

---

## Implemented

### Core Kernel
| Feature | Syscalls | Notes |
|---------|----------|-------|
| Core IPC | NOOP, HELLO, EXIT | 95K+ ops/s |
| Agent Lifecycle | SPAWN, KILL, LIST, PAUSE, RESUME | Full lifecycle + restart policies |
| State Store | STORE, FETCH, DELETE, KEYS | KV with TTL, scopes, SQLite persistence |
| Mailbox IPC | SEND, RECV, BROADCAST, REGISTER | P2P + broadcast, name resolution |
| Event Bus | SUBSCRIBE, UNSUBSCRIBE, POLL_EVENTS, EMIT | 14 event types, per-agent queues |
| Permissions | GET_PERMS, SET_PERMS, AUTH, POLICY_UPDATE | RBAC, 5 levels, path/command/domain ACLs |
| File I/O | READ, WRITE, EXEC | Permission-gated |
| HTTP Proxy | HTTP | CURL client, SSRF guard, domain allowlist |
| Async Tasks | ASYNC_POLL | Worker pool for background jobs |

### Security & Isolation
| Feature | Notes |
|---------|-------|
| Linux Namespaces | PID, NET, MNT, UTS via clone() |
| cgroups v2 | Memory, CPU, PIDs limits |
| Landlock LSM | Filesystem ACLs (read/write paths) |
| seccomp BPF | 27 blocked syscalls |
| PII Filter | PII_SCAN, PII_REDACT — 5 patterns + custom regex |
| Audit Logger | GET_AUDIT_LOG, SET_AUDIT_CONFIG — 8 categories |
| Execution Replay | RECORD_START/STOP/STATUS, REPLAY_START/STATUS — full record + playback |
| Policy Hot-Reload | File watcher (kqueue/inotify), no restart |
| Policy Recommendations | POLICY_RECOMMEND — learns from denial patterns |

### LLM & Integrations
| Feature | Notes |
|---------|-------|
| OpenRouter | 300+ models, THINK + LLM_CONFIG + LLM_REPORT |
| MCP Bridge | MCP_LIST, MCP_CALL — JSON-RPC over stdio |
| A2A Protocol | A2A_SEND, A2A_RECV — HTTP server/client, agent cards |
| OTel Tracing | OTEL_SPAN — span export |
| Credentials | CREDS_GET — permission-gated secret retrieval |
| Metrics | METRICS_SYSTEM, METRICS_AGENT, METRICS_ALL_AGENTS, METRICS_CGROUP |

### Context & Memory (new)
| Feature | Syscalls | Notes |
|---------|----------|-------|
| Artifacts | DOC_CREATE/READ/UPDATE/LIST/DELETE | Typed docs with state machine, DAG provenance |
| Chains | CHAIN_CREATE/GET/FORK | Ordered artifact sequences with fork |
| Context Assembly | CONTEXT_ASSEMBLE | L1/L2/L3 layers, token budgeting, truncation reporting |
| Context-Aware LLM | SYS_THINK + auto_context | Kernel-assembled context prepended to prompts |
| Memory Blocks | MEM_CREATE/READ/WRITE/APPEND/DELETE/LIST/SHARE | Named, typed (SYSTEM/CORE/RECALL), shareable |
| Observation Masking | Built into ContextAssembler | Compresses tool outputs ~50% savings |

### Scheduling & Budgets (new)
| Feature | Syscalls | Notes |
|---------|----------|-------|
| Four-Budget Enforcement | SET_BUDGET, GET_BUDGET | Token, step, time, cost — kernel-enforced |
| Agent Scheduler | SET_PRIORITY, GET_PRIORITY | 5 priority levels, state tracking |
| Budget Middleware | — | Step count + time check on every syscall |
| LLM Cost Tracking | — | Tokens + USD recorded after every SYS_THINK |

### Multi-Tenancy & Networking
| Feature | Syscalls | Notes |
|---------|----------|-------|
| World Engine | WORLD_CREATE/DESTROY/LIST/JOIN/LEAVE/EVENT/STATE/SNAPSHOT/RESTORE | Isolated realms |
| Tunnel Bridge | TUNNEL_CONNECT/DISCONNECT/STATUS/LIST_REMOTES/CONFIG | Remote agent bridging |

### Infrastructure
| Feature | Notes |
|---------|-------|
| REST API | 71 HTTP routes, Bearer auth, HTMX dashboard |
| RunEngine | Built-in agent with tool-calling loop — `POST /api/run` |
| SSE Streaming | Real-time event streaming — `POST /api/run/stream` |
| Fleet | Parallel multi-agent execution — `POST /api/fleet` |
| CLI | 13 commands (C++ CLI) |
| Python SDK | 86 syscall methods, Agent class with @tool decorator |
| TypeScript SDK | Full port, 1,305 LOC, all 86 syscalls |
| SQLite Persistence | WAL mode, write-through for state/artifacts/chains/memory blocks |
| Docker | Multi-stage Alpine build, docker-compose |
| systemd | Hardened service file |

### RunEngine Tools (real kernel operations, not stubs)
| Tool | Kernel operation | Notes |
|------|-----------------|-------|
| `read_file` | `ifstream` — real file read | Audited |
| `write_file` | `ofstream` — real file write | Audited, stored as artifact |
| `exec` | `popen` — real shell execution | Audited, output capped at 512KB |
| `http` | CURL — real HTTP requests | Audited, 30s timeout |
| `search` | CURL to DuckDuckGo + LLM extraction | Real web search |
| `store`/`fetch` | Kernel state store | KV with TTL |
| `remember`/`recall` | Memory blocks | Persistent across runs |
| `mcp_*` | MCP bridge | Any connected MCP server |

---

## Not Yet Done

### API Gaps (kernel has it, API doesn't expose it yet)
| Feature | Syscalls | Effort | Why |
|---------|----------|--------|-----|
| Pause/Resume agents | SYS_PAUSE, SYS_RESUME | Small | Throttle runaway agents |
| IPC messaging API | SYS_SEND, SYS_RECV | Small | Agent-to-agent coordination |
| KV delete + list keys | SYS_DELETE, SYS_KEYS | Small | Data management |
| Memory CRUD API | SYS_MEM_WRITE/DELETE/LIST/SHARE | Small | Full memory control |
| Context assembly in RunEngine | SYS_CONTEXT_ASSEMBLE | Medium | Research optimizations active |
| World snapshot/restore | SYS_WORLD_SNAPSHOT/RESTORE | Small | Save/fork agent environments |
| Replay via API | SYS_REPLAY_START/STATUS | Small | Debug past runs |
| Event streaming | SYS_POLL_EVENTS | Medium | Real-time kernel event SSE |
| Tunnel API | SYS_TUNNEL_* | Medium | Cross-kernel federation |
| A2A API | SYS_A2A_SEND/RECV | Small | External agent interop |
| OTel export | SYS_OTEL_SPAN | Small | Production observability |

### Engineering
| Feature | Effort | Why |
|---------|--------|-----|
| End-to-end integration test | Small | Verify kernel + SDK + API work together |
| macOS sandbox improvement | Medium | Currently fork-only, no isolation |
| Subgoal compression in ContextAssembler | Small | Summarize completed subtasks |
| Multi-model cascading in RunEngine | Medium | Cheap model for simple, expensive for hard |

### Compliance (deadline: Aug 2, 2026)
| Feature | Why |
|---------|-----|
| Audit log retention policy + JSONL export | EU AI Act |
| OWASP ASI Top 10 mapping doc | Enterprise sales |
| Immutable audit log guarantee | Compliance requirement |

---

## Architecture Docs

| Doc | Purpose |
|-----|---------|
| [CONTEXT_LAYER.md](architecture/CONTEXT_LAYER.md) | Artifacts, chains, context assembly |
| [MEMORY_ARCHITECTURE.md](architecture/MEMORY_ARCHITECTURE.md) | 3-tier memory, memory blocks, compression |
| [SCHEDULER_DESIGN.md](architecture/SCHEDULER_DESIGN.md) | Priority scheduler, state tracking |
| [SECURITY_COMPLIANCE.md](architecture/SECURITY_COMPLIANCE.md) | OWASP mapping, EU AI Act, budgets |
| [RESEARCH_SYNTHESIS.md](research/RESEARCH_SYNTHESIS.md) | ~50 paper survey driving design decisions |
