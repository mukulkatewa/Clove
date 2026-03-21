# CLOVE v2 — Roadmap & Progress Tracker

> Last updated: 2026-03-21

## Status: 46/66 syscall handlers registered (70%)
## Remaining: World (9), Tunnel (5), Auth/Policy/Audit config (3), Replay playback (2), Metrics cgroup (1)

### Completed

| Feature | Syscalls | Performance |
|---------|----------|-------------|
| Core IPC | NOOP, HELLO, EXIT | 95K+ ops/s sustained |
| Agent Lifecycle | SPAWN, KILL, LIST, PAUSE, RESUME | LIST: 0.03ms |
| State Store | STORE, FETCH, DELETE, KEYS | 16K+ writes/s, TTL eviction works |
| Mailbox IPC | SEND, RECV, BROADCAST, REGISTER | P2P round-trip: 0.1ms |
| Event Bus | SUBSCRIBE, UNSUBSCRIBE, POLL_EVENTS, EMIT | Emit+Poll: 0.09ms, 50-sub fanout: 0.03ms |
| Permissions | GET_PERMS, SET_PERMS | +0.05ms overhead vs NOOP |
| Audit Logger | GET_AUDIT_LOG | 0.03ms query after 2K entries |
| PII Filter | PII_SCAN, PII_REDACT | 100% detection, 0 false positives, 0.2ms redact |
| Inference Gateway | THINK, LLM_CONFIG, LLM_REPORT | Blocked call: +0.005ms overhead |
| Policy Engine | POLICY_RECOMMEND | 0.1ms for recommendations |
| Execution Replay | RECORD_START, RECORD_STOP, RECORD_STATUS | ~2% recording overhead |
| OpenRouter LLM | Full libcurl HTTP client | chat(), list_models(), get_credits() |
| File I/O | READ, WRITE, EXEC | Permission-gated with path/command ACLs |
| HTTP Proxy | HTTP | Permission-gated with domain allowlist |
| Async Tasks | ASYNC_POLL | Polls completed async task results |
| Metrics | METRICS_SYSTEM, METRICS_AGENT, METRICS_ALL_AGENTS | RSS, CPU, subsystem stats |
| Persistence | SQLite (Database, AuditStore, StateStoreDb) | WAL mode, auto-migration, state restore on boot |
| Sandbox | (process-level) | Linux namespaces + cgroups v2 |
| MCP Bridge | MCP_LIST, MCP_CALL | JSON-RPC over stdio, tool discovery, per-agent ACL |
| A2A Protocol | A2A_SEND, A2A_RECV | HTTP server (inbound) + curl client (outbound), agent card |
| OTel Spans | OTEL_SPAN | Span recording, audit log integration, 10K span buffer |
| Credentials | CREDS_GET | Permission-gated secret retrieval from state store |
| Benchmarks | v1-vs-v2 (14 tests), control plane (21 tests) | Full governance pipeline: 0.12ms |

### Stubbed (infrastructure exists, needs finishing)

| Feature | What's Done | What's Missing |
|---------|------------|----------------|
| Landlock | `apply_landlock()` function signature | Actual Landlock syscalls for filesystem MAC |
| seccomp | `apply_seccomp()` function signature | BPF filter for syscall whitelist |
| Replay Playback | REPLAY_START/STATUS opcodes defined | Handler to load log + re-inject messages |

### Not Started

| Feature | Opcodes | Priority | Notes |
|---------|---------|----------|-------|
| REST API | — | P2 | HTTP fleet management. `--api` flag exists |
| World (Multi-tenant) | 0xA0-0xA8 | P3 | Isolated realms (9 opcodes) |
| Tunnel (Remote) | 0xB0-0xB4 | P3 | Bridge agents across machines (5 opcodes) |
| Metrics (cgroup) | 0xC3 | P3 | cgroup-level resource monitoring |
| Landlock + seccomp | — | P3 | Full Linux sandbox hardening |

### Build Order

```
DONE  — OpenRouter, ASYNC_POLL, HTTP, File I/O, Metrics, SQLite, MCP, A2A, OTel, Creds, Tests (93 cases)

Next  — REST API → Replay playback → Auth/Policy syscalls

Future — World (9 opcodes) → Tunnel (5 opcodes) → Landlock → seccomp
```
