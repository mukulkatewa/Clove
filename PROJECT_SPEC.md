# CLOVE v2 — Project Specification

> **Started:** March 20, 2026
> **Language:** C++23
> **Build:** CMake 3.20+
> **Predecessor:** /Users/anixd/Documents/Clove (v1, 15.2K LOC, proof of concept)
> **Goal:** Production-grade agent fleet infrastructure that beats OpenShell

---

## What This Is

CLOVE v2 is a **clean rebuild** of the CLOVE agent kernel. Same architecture (proven), new codebase (modular, persistent, integratable). The v1 kernel proved that kernel-native agent infrastructure works and is 7,450x faster than Docker-based alternatives. v2 makes it a product.

**What changed from v1:**
- Modular build (separate libraries, not one monolith)
- Persistence (SQLite for audit, state, replay, permissions)
- REST API + WebSocket (control plane for dashboards and automation)
- Integrations (OpenRouter, MCP, Presidio PII, OpenTelemetry, A2A)
- Defense-in-depth isolation (Landlock + seccomp ON TOP of namespaces)
- macOS sandbox-exec profiles (partial isolation, not just fork)
- Multi-tenant ready (org/team/user scoping)
- Policy recommendation engine (learn from denials)

**What stays the same:**
- Binary IPC protocol (17-byte header, same opcodes — drop-in for existing SDK)
- epoll/kqueue reactor
- Namespace + cgroup isolation model
- Multi-agent orchestration (IPC, state, events, crew/task)
- Execution replay
- Audit logging
- Python SDK compatibility

---

## Directory Structure

```
clove-v2/
├── CMakeLists.txt                    # Top-level workspace build
├── PROJECT_SPEC.md                   # This file
├── CLAUDE.md                         # Build instructions + context
│
├── libs/                             # Modular libraries
│   ├── core/                         # Shared types, protocol, config
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── protocol.hpp          # Binary IPC (17-byte header, 60+ opcodes)
│   │   │   ├── config.hpp            # KernelConfig struct
│   │   │   ├── types.hpp             # AgentConfig, ResourceLimits, etc.
│   │   │   ├── error.hpp             # Error types + Result<T>
│   │   │   └── version.hpp           # Version constants
│   │   └── src/
│   │       ├── protocol.cpp
│   │       ├── config.cpp
│   │       └── types.cpp
│   │
│   ├── reactor/                      # Event loop (epoll/kqueue)
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   └── reactor.hpp
│   │   └── src/
│   │       ├── reactor.cpp
│   │       ├── epoll_backend.cpp     # Linux
│   │       └── kqueue_backend.cpp    # macOS
│   │
│   ├── ipc/                          # Socket server + client
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── socket_server.hpp     # Unix domain socket server
│   │   │   ├── socket_client.hpp     # For control plane → kernel
│   │   │   └── message.hpp           # Message builder/parser
│   │   └── src/
│   │       ├── socket_server.cpp
│   │       ├── socket_client.cpp
│   │       └── message.cpp
│   │
│   ├── sandbox/                      # Process isolation
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── sandbox.hpp           # Sandbox interface
│   │   │   ├── isolation_status.hpp
│   │   │   └── resource_limits.hpp
│   │   └── src/
│   │       ├── sandbox.cpp           # Common logic
│   │       ├── linux/
│   │       │   ├── namespace.cpp     # clone(NEWPID|NET|MNT|UTS)
│   │       │   ├── cgroup.cpp        # cgroups v2 (memory, cpu, pids)
│   │       │   ├── landlock.cpp      # Landlock filesystem ACLs (NEW)
│   │       │   └── seccomp.cpp       # seccomp BPF filtering (NEW)
│   │       └── macos/
│   │           ├── fork_sandbox.cpp  # fork() fallback
│   │           └── sandbox_exec.cpp  # sandbox-exec profiles (NEW)
│   │
│   ├── agents/                       # Agent lifecycle management
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── agent_manager.hpp
│   │   │   ├── agent_process.hpp
│   │   │   └── restart_policy.hpp
│   │   └── src/
│   │       ├── agent_manager.cpp
│   │       ├── agent_process.cpp
│   │       └── restart_policy.cpp
│   │
│   ├── orchestration/                # Multi-agent coordination (THE MOAT)
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── mailbox.hpp           # Per-agent IPC queues
│   │   │   ├── state_store.hpp       # KV with TTL + scoped access
│   │   │   ├── event_bus.hpp         # Typed pub/sub (12+ events)
│   │   │   ├── llm_queue.hpp         # Multi-threaded LLM dispatch
│   │   │   └── async_tasks.hpp       # Generic async executor
│   │   └── src/
│   │       ├── mailbox.cpp
│   │       ├── state_store.cpp
│   │       ├── event_bus.cpp
│   │       ├── llm_queue.cpp
│   │       └── async_tasks.cpp
│   │
│   ├── governance/                   # Security + compliance
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── permissions.hpp       # Per-agent RBAC
│   │   │   ├── permissions_store.hpp
│   │   │   ├── inference_gateway.hpp # Model/provider/cost enforcement
│   │   │   ├── privacy_filter.hpp    # PII detection + redaction (NEW)
│   │   │   ├── audit_log.hpp         # 8-category structured audit
│   │   │   ├── execution_log.hpp     # Record/replay engine
│   │   │   ├── policy_watcher.hpp    # File watcher (kqueue/inotify)
│   │   │   ├── policy_recommender.hpp # Learn from denials (NEW)
│   │   │   └── manifest.hpp          # Deployment manifest parser
│   │   └── src/
│   │       ├── permissions.cpp
│   │       ├── permissions_store.cpp
│   │       ├── inference_gateway.cpp
│   │       ├── privacy_filter.cpp    # Regex: SSN, email, phone, CC, IP
│   │       ├── audit_log.cpp
│   │       ├── execution_log.cpp
│   │       ├── policy_watcher.cpp
│   │       ├── policy_recommender.cpp
│   │       └── manifest.cpp
│   │
│   ├── persistence/                  # SQLite storage (NEW)
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── db.hpp               # Database connection + migrations
│   │   │   ├── audit_store.hpp      # Persistent audit log
│   │   │   ├── state_store_db.hpp   # Persistent KV (survives restart)
│   │   │   ├── replay_store.hpp     # Persistent execution recordings
│   │   │   └── perm_store_db.hpp    # Persistent permissions
│   │   └── src/
│   │       ├── db.cpp
│   │       ├── audit_store.cpp
│   │       ├── state_store_db.cpp
│   │       ├── replay_store.cpp
│   │       ├── perm_store_db.cpp
│   │       └── migrations/
│   │           ├── 001_initial.sql
│   │           ├── 002_audit.sql
│   │           ├── 003_state.sql
│   │           └── 004_replay.sql
│   │
│   ├── network/                      # Egress + proxy
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── egress_proxy.hpp     # HTTP CONNECT proxy
│   │   │   ├── http_client.hpp      # libcurl wrapper for SYS_HTTP
│   │   │   └── ssrf_guard.hpp       # Block private IPs (NEW)
│   │   └── src/
│   │       ├── egress_proxy.cpp
│   │       ├── http_client.cpp
│   │       └── ssrf_guard.cpp
│   │
│   ├── integrations/                 # External system connectors (NEW)
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── openrouter.hpp       # OpenRouter LLM routing
│   │   │   ├── mcp_bridge.hpp       # MCP protocol bridge
│   │   │   ├── a2a_bridge.hpp       # Agent2Agent protocol bridge
│   │   │   ├── otel_exporter.hpp    # OpenTelemetry span/metric export
│   │   │   └── langfuse.hpp         # Langfuse trace integration
│   │   └── src/
│   │       ├── openrouter.cpp
│   │       ├── mcp_bridge.cpp
│   │       ├── a2a_bridge.cpp
│   │       ├── otel_exporter.cpp
│   │       └── langfuse.cpp
│   │
│   ├── api/                          # REST API + WebSocket (NEW)
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   ├── api_server.hpp       # HTTP server (cpp-httplib or beast)
│   │   │   ├── routes.hpp           # Route definitions
│   │   │   ├── auth.hpp             # API key auth + org scoping
│   │   │   └── websocket.hpp        # Real-time log/event streaming
│   │   └── src/
│   │       ├── api_server.cpp
│   │       ├── routes/
│   │       │   ├── agents.cpp       # GET/POST/DELETE /api/agents
│   │       │   ├── policies.cpp     # GET/PUT /api/policies
│   │       │   ├── inference.cpp    # GET/PUT /api/inference
│   │       │   ├── audit.cpp        # GET /api/audit
│   │       │   ├── replay.cpp       # GET/POST /api/replay
│   │       │   ├── metrics.cpp      # GET /api/metrics
│   │       │   └── health.cpp       # GET /health
│   │       ├── auth.cpp
│   │       └── websocket.cpp
│   │
│   ├── metrics/                      # System + agent metrics
│   │   ├── CMakeLists.txt
│   │   ├── include/clove/
│   │   │   └── metrics.hpp
│   │   └── src/
│   │       ├── metrics.cpp
│   │       ├── linux_metrics.cpp
│   │       └── macos_metrics.cpp
│   │
│   └── worlds/                       # Simulation engine
│       ├── CMakeLists.txt
│       ├── include/clove/
│       │   ├── world_engine.hpp
│       │   ├── virtual_fs.hpp
│       │   └── chaos.hpp
│       └── src/
│           ├── world_engine.cpp
│           ├── virtual_fs.cpp
│           └── chaos.cpp
│
├── kernel/                           # The kernel binary
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp                 # Entry point, CLI parsing
│   │   ├── kernel.hpp               # Kernel class
│   │   ├── kernel.cpp               # Init, run, shutdown
│   │   ├── context.hpp              # KernelContext (reference bundle)
│   │   ├── syscall_router.hpp       # Opcode → handler dispatch
│   │   ├── syscall_router.cpp
│   │   └── syscalls/                # Handler modules
│   │       ├── mod.hpp              # All handler declarations
│   │       ├── agent_syscalls.cpp
│   │       ├── llm_syscalls.cpp
│   │       ├── exec_syscalls.cpp
│   │       ├── file_syscalls.cpp
│   │       ├── ipc_syscalls.cpp
│   │       ├── state_syscalls.cpp
│   │       ├── network_syscalls.cpp
│   │       ├── event_syscalls.cpp
│   │       ├── permission_syscalls.cpp
│   │       ├── audit_syscalls.cpp
│   │       ├── replay_syscalls.cpp
│   │       ├── metrics_syscalls.cpp
│   │       ├── tunnel_syscalls.cpp
│   │       ├── world_syscalls.cpp
│   │       └── async_syscalls.cpp
│   └── tests/
│       ├── test_kernel.cpp
│       └── test_syscalls.cpp
│
├── server/                           # Control plane binary (NEW)
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp                 # REST API + WebSocket server
│   │   └── dashboard/               # Static HTMX dashboard files
│   │       ├── index.html
│   │       ├── agents.html
│   │       ├── audit.html
│   │       └── style.css
│   └── tests/
│       └── test_api.cpp
│
├── cli/                              # CLI binary (C++ or keep Python)
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp
│       └── commands/
│           ├── deploy.cpp
│           ├── run.cpp
│           ├── ps.cpp
│           ├── logs.cpp
│           ├── stop.cpp
│           └── cloud.cpp
│
├── sdk/                              # Agent SDKs
│   ├── python/                       # Port from v1 (same API)
│   │   ├── clove_sdk/
│   │   │   ├── __init__.py
│   │   │   ├── client.py
│   │   │   ├── framework.py
│   │   │   └── local_transport.py
│   │   └── pyproject.toml
│   └── typescript/                   # Future
│       └── package.json
│
├── deploy/                           # Distribution
│   ├── Dockerfile                    # Alpine + kernel binary (~15MB)
│   ├── docker-compose.yml
│   ├── systemd/
│   │   └── clove.service
│   └── install.sh                    # curl get.cloveos.com | sh
│
├── tests/                            # Integration tests
│   ├── CMakeLists.txt
│   ├── test_ipc_protocol.cpp
│   ├── test_sandbox_linux.cpp
│   ├── test_multi_agent.cpp
│   ├── test_persistence.cpp
│   ├── test_privacy_filter.cpp
│   └── test_openrouter.cpp
│
├── benchmark/                        # Performance benchmarks
│   ├── run_benchmarks.py
│   └── results/
│
├── docs/                             # Documentation
│   ├── architecture.md
│   ├── syscalls.md
│   ├── deployment.md
│   └── integrations.md
│
└── third_party/                      # Vendored deps (if needed)
    └── sqlite3/
```

---

## Module Dependency Graph

```
                    ┌──────────┐
                    │   core   │  (types, protocol, config)
                    └────┬─────┘
                         │
          ┌──────────────┼──────────────────────────┐
          │              │                           │
     ┌────▼────┐   ┌────▼────┐              ┌──────▼──────┐
     │ reactor │   │   ipc   │              │ governance  │
     └────┬────┘   └────┬────┘              └──────┬──────┘
          │              │                          │
     ┌────▼────┐   ┌────▼──────┐            ┌─────▼───────┐
     │ sandbox │   │  agents   │            │ persistence │
     └────┬────┘   └────┬──────┘            └─────────────┘
          │              │
     ┌────▼────┐   ┌────▼──────────┐
     │ network │   │ orchestration │
     └─────────┘   └───────────────┘

                    ┌──────────────┐
                    │ integrations │  (openrouter, mcp, a2a, otel)
                    └──────────────┘

                    ┌──────────┐
                    │   api    │  (REST server, websocket, auth)
                    └──────────┘

                    ┌──────────┐
                    │ metrics  │
                    └──────────┘

                    ┌──────────┐
                    │  worlds  │
                    └──────────┘

    ┌────────┐   ┌────────┐   ┌─────┐
    │ kernel │   │ server │   │ cli │   (binaries — link against libs)
    └────────┘   └────────┘   └─────┘
```

---

## New Syscall Opcodes (v2 additions)

```
// Keep all 57 v1 opcodes for backward compatibility
// Add new opcodes for v2 features:

SYS_PII_SCAN        = 0xD1   // Scan text for PII, return matches
SYS_PII_REDACT      = 0xD2   // Redact PII from text, return cleaned
SYS_MCP_CALL        = 0xD3   // Call an MCP tool through kernel (mediated)
SYS_MCP_LIST        = 0xD4   // List available MCP servers/tools
SYS_A2A_SEND        = 0xD5   // Send message to external agent via A2A
SYS_A2A_RECV        = 0xD6   // Receive message from external agent
SYS_POLICY_RECOMMEND = 0xD7  // Get recommended policy changes from denials
SYS_CREDS_GET       = 0xD8   // Get credentials for a provider (kernel-managed)
SYS_OTEL_SPAN       = 0xD9   // Emit an OpenTelemetry span
```

Total: 86 syscalls (57 original + 9 new integrations + 20 context/memory/world/daemon)

---

## Integration Specs

### OpenRouter (LLM Routing)

```
Config:
  openrouter_enabled: true
  openrouter_api_key: "sk-or-..."
  openrouter_base_url: "https://openrouter.ai/api/v1"

Flow:
  Agent → SYS_THINK → InferenceGateway (check model/cost)
       → PrivacyFilter (scan/redact PII if enabled)
       → OpenRouter API (route to best provider)
       → Response → audit log → agent

Benefits:
  - 300+ models instantly
  - Automatic failover
  - Zero Data Retention option
  - EU routing (eu.openrouter.ai)
  - Per-key spend limits (stacks with our per-agent limits)
```

### MCP Bridge (Tool Access)

```
Config:
  mcp_servers:
    - name: "filesystem"
      command: "npx @modelcontextprotocol/server-filesystem /tmp"
    - name: "github"
      command: "npx @modelcontextprotocol/server-github"

Flow:
  Agent → SYS_MCP_CALL(server="github", tool="search", args={...})
       → Kernel checks permissions (agent allowed to use this MCP server?)
       → Kernel spawns/connects to MCP server subprocess
       → JSON-RPC call to MCP server
       → Response → audit log → agent

New:
  Agents get tool access to the entire MCP ecosystem
  Kernel mediates + audits every tool call
  Per-agent MCP server allowlists
```

### A2A Bridge (Cross-System Agents)

```
Config:
  a2a_enabled: true
  a2a_listen_port: 8081

Flow (inbound):
  External agent → HTTP POST /a2a/message → CLOVE kernel
       → Route to internal agent by name
       → Deliver via IPC mailbox (SYS_SEND)
       → Response → A2A response

Flow (outbound):
  Agent → SYS_A2A_SEND(target="external-agent.example.com", msg={...})
       → Kernel sends HTTP to target A2A endpoint
       → Response → agent

New:
  CLOVE agents can coordinate with agents on OTHER platforms
  Internal IPC (0.02ms) for co-located, A2A (HTTP) for remote
  Best of both worlds
```

### OpenTelemetry Export

```
Config:
  otel_enabled: true
  otel_endpoint: "http://localhost:4317"  # OTLP gRPC
  otel_service_name: "clove-kernel"

Flow:
  Every syscall → OTel span
    span.name = opcode_to_string(op)
    span.attributes = {agent_id, duration_ms, success}
    parent_span = agent's trace context

  LLM calls → additional attributes:
    model, tokens_input, tokens_output, cost_usd

  Multi-agent → trace propagation:
    SYS_SEND creates child span linked to receiver's trace

Export:
  OTLP → Jaeger, Grafana Tempo, Datadog, New Relic, etc.
```

### Presidio PII Filter

```
Config:
  privacy_enabled: true
  privacy_mode: "redact"  # audit | redact | block
  privacy_patterns: ["ssn", "email", "phone", "credit_card", "ip_address"]
  privacy_custom_patterns:
    - name: "employee_id"
      pattern: "EMP-\\d{6}"

Flow (SYS_THINK):
  Agent → SYS_THINK("Call John at 555-123-4567 about SSN 123-45-6789")
       → PrivacyFilter.scan() → finds [phone, ssn]
       → mode=redact: "Call [REDACTED:NAME] at [REDACTED:PHONE] about [REDACTED:SSN]"
       → mode=block: DENY "PII detected: phone, ssn"
       → mode=audit: pass through, but log detected PII
       → Audit log: {type: "PII_DETECTED", entities: ["phone", "ssn"], action: "redacted"}

Implementation:
  Regex patterns (fast, zero-dependency):
    SSN:         \b\d{3}-\d{2}-\d{4}\b
    Email:       \b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Z]{2,}\b
    Phone:       \b(\+?\d{1,3}[-.\s]?)?\(?\d{3}\)?[-.\s]?\d{3}[-.\s]?\d{4}\b
    Credit Card: \b\d{4}[-\s]?\d{4}[-\s]?\d{4}[-\s]?\d{4}\b
    IP:          \b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b
    Custom:      user-defined regex patterns from config
```

---

## Build Order (What to Implement First)

### Phase 1: Foundation (Week 1)
```
1. core/          — protocol.hpp, config.hpp, types.hpp (port from v1)
2. reactor/       — epoll + kqueue backends (port from v1)
3. ipc/           — socket server + message parser (port from v1)
4. sandbox/       — namespaces + cgroups + NEW landlock + seccomp
5. agents/        — agent manager + process lifecycle (port from v1)
```
**Deliverable:** Kernel boots, accepts connections, spawns agents with isolation. Same binary protocol as v1 — Python SDK works unchanged.

### Phase 2: Orchestration (Week 2)
```
6. orchestration/ — mailbox, state store, event bus, llm queue, async tasks
7. governance/    — permissions, inference gateway, audit, execution log, policy watcher
8. kernel/        — syscall router + all handler modules
```
**Deliverable:** Full v1 feature parity. All 57 syscalls work. Multi-agent crews run.

### Phase 3: New Capabilities (Week 3)
```
9.  governance/privacy_filter    — PII detection + redaction
10. governance/policy_recommender — learn from denials
11. persistence/                  — SQLite for audit, state, replay, perms
12. network/ssrf_guard           — block private IPs
13. sandbox/linux/landlock       — filesystem ACLs
14. sandbox/linux/seccomp        — syscall BPF filtering
15. sandbox/macos/sandbox_exec   — sandbox-exec profiles
```
**Deliverable:** Production-ready security. PII filter. Persistent data. Defense-in-depth.

### Phase 4: Integrations (Week 4)
```
16. integrations/openrouter      — 300+ models via single API
17. integrations/mcp_bridge      — MCP tool access
18. integrations/otel_exporter   — OpenTelemetry spans + metrics
19. integrations/langfuse        — LLM observability
20. integrations/a2a_bridge      — cross-system agent communication
```
**Deliverable:** Connected to the entire open-source agent ecosystem.

### Phase 5: Control Plane (Week 5)
```
21. api/                         — REST API + WebSocket server
22. server/                      — Control plane binary + dashboard
23. cli/                         — C++ CLI (or keep Python CLI)
```
**Deliverable:** Web dashboard, API access, full operator visibility.

### Phase 6: Distribution (Week 6)
```
24. deploy/Dockerfile            — Alpine + kernel (~15MB)
25. deploy/install.sh            — curl installer
26. deploy/systemd/              — service file
27. benchmark/                   — v2 benchmark suite
```
**Deliverable:** Ship it. `curl get.cloveos.com | sh`

---

## Dependencies

```
REQUIRED:
  nlohmann/json    — JSON parsing (header-only)
  spdlog + fmt     — Logging
  OpenSSL          — TLS, SHA-256 for manifests
  sqlite3          — Persistence (vendored or system)

OPTIONAL (for integrations):
  libcurl          — HTTP client (OpenRouter, MCP, A2A, OTel)
  cpp-httplib      — REST API server (header-only, lightweight)
                     OR Boost.Beast (heavier but more capable)

LINUX-ONLY:
  landlock headers — Filesystem ACLs (kernel 5.13+)
  seccomp headers  — BPF syscall filtering
  liburing         — io_uring (optional perf optimization)

BUILD:
  CMake 3.20+
  C++23 compiler (GCC 13+ or Clang 16+)
  Threads (pthreads)
```

---

## Testing Strategy

```
Unit tests:     Per-library (libs/*/tests/)
Integration:    tests/ directory (multi-component)
Benchmark:      benchmark/ (performance regression)
E2E:            Python script spawning agents on running kernel

Test pyramid:
  Unit:         Fast, isolated, mock dependencies
  Integration:  Real socket, real IPC, in-memory stores
  E2E:          Full kernel + agents + SDK
  Benchmark:    Performance comparison vs v1 and OpenShell
```

---

## Migration Path From v1

```
v1 (Clove/)                          v2 (clove-v2/)
═══════════                          ═══════════════

src/kernel/reactor.cpp          →    libs/reactor/src/
src/kernel/kernel.cpp           →    kernel/src/kernel.cpp
src/ipc/protocol.hpp            →    libs/core/include/clove/protocol.hpp
src/ipc/transport/socket_server →    libs/ipc/src/socket_server.cpp
src/runtime/sandbox/            →    libs/sandbox/src/
src/runtime/agent/              →    libs/agents/src/
src/kernel/syscalls/            →    kernel/src/syscalls/
src/kernel/permissions.hpp      →    libs/governance/include/
src/kernel/audit_log.hpp        →    libs/governance/include/
src/kernel/execution_log.hpp    →    libs/governance/include/
src/kernel/state_store.hpp      →    libs/orchestration/include/
src/kernel/event_bus.hpp        →    libs/orchestration/include/
src/kernel/llm_queue.hpp        →    libs/orchestration/include/
src/kernel/inference_gateway.*  →    libs/governance/src/
src/kernel/egress_proxy.*       →    libs/network/src/
src/kernel/policy_watcher.*     →    libs/governance/src/
src/kernel/manifest.*           →    libs/governance/src/
src/metrics/                    →    libs/metrics/src/
src/worlds/                     →    libs/worlds/src/

NEW (doesn't exist in v1):
  libs/persistence/             ←    SQLite storage
  libs/integrations/            ←    OpenRouter, MCP, A2A, OTel, Langfuse
  libs/governance/privacy_*     ←    PII filter
  libs/governance/policy_rec*   ←    Policy recommendations
  libs/sandbox/linux/landlock   ←    Landlock isolation
  libs/sandbox/linux/seccomp    ←    seccomp filtering
  libs/sandbox/macos/sandbox_*  ←    macOS sandbox-exec profiles
  libs/network/ssrf_guard       ←    SSRF protection
  libs/api/                     ←    REST API + WebSocket
  server/                       ←    Control plane binary
```

---

## Why This Beats OpenShell

| Dimension | clove-v2 | OpenShell |
|-----------|----------|-----------|
| **Multi-agent** | Native IPC + state + events | Nothing (single agent) |
| **Performance** | 0.02ms IPC, 54K ops/sec | 150ms, 6 ops/sec |
| **Footprint** | ~5MB binary, ~5MB RAM | 3.5GB images, 907MB RAM |
| **Isolation** | namespaces + cgroups + Landlock + seccomp | Landlock + seccomp (in Docker) |
| **PII** | Presidio-style regex + audit | Not shipped (marketing only) |
| **Policy** | Hot-reload + recommendations | Hot-reload (OPA, heavier) |
| **Persistence** | SQLite (lightweight) | SQLite/Postgres (K8s-grade) |
| **API** | REST + WebSocket | gRPC + mTLS |
| **Integrations** | OpenRouter + MCP + A2A + OTel | Inference routing only |
| **Replay** | Full syscall record/replay | Nothing |
| **Dev mode** | LocalTransport (zero infra) | Always needs Docker+K3s |
| **Deploy** | Single binary, 15MB Docker | K8s cluster, 3.5GB+ |
| **Cost control** | Per-agent USD limits | Nothing |
| **Standards** | MCP + A2A + OTel | Proprietary |
