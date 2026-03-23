# CLOVE

**A kernel for AI agents.** One binary. 86 syscalls. Agents get process isolation, shared memory, inter-agent messaging, cost enforcement, and execution replay.

```
1.4 MB binary  |  6.7 MB idle RAM  |  27ms boot  |  86 syscalls  |  C++23
```

---

## Quick Start (macOS)

### Prerequisites

```bash
# Xcode command line tools (C++23 compiler)
xcode-select --install

# CMake + system dependencies
brew install cmake openssl curl sqlite3
```

### Build

```bash
git clone https://github.com/aniiiiXD/Clove.git
cd Clove
git checkout v2

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
```

This produces two binaries:
- `build/kernel/clove_kernel` — the kernel
- `build/cli/clove_cli` — the CLI

### Run

```bash
# Get an OpenRouter API key from https://openrouter.ai/keys
export OPENROUTER_API_KEY="sk-or-v1-your-key-here"

# Start the kernel
./build/kernel/clove_kernel --no-sandbox --openrouter --api --db clove.db
```

You should see:

```
    ╔═══════════════════════════════════════════╗
    ║   ██████╗██╗      ██████╗ ██╗   ██╗███████╗  ║
    ║  ██╔════╝██║     ██╔═══██╗██║   ██║██╔════╝  ║
    ║  ██║     ██║     ██║   ██║██║   ██║█████╗    ║
    ║  ██║     ██║     ██║   ██║╚██╗ ██╔╝██╔══╝    ║
    ║  ╚██████╗███████╗╚██████╔╝ ╚████╔╝ ███████╗  ║
    ║   ╚═════╝╚══════╝ ╚═════╝   ╚═══╝  ╚══════╝  ║
    ║  Agent Fleet Infrastructure v2               ║
    ╚═══════════════════════════════════════════╝

    ✓  Starting CLOVE v2.0.0

    ═══════════════════════════════════════
      CLOVE READY  ·  Ctrl+C to shutdown
    ═══════════════════════════════════════
```

> **Note:** `--no-sandbox` is required on macOS because Linux namespace isolation is not available. The kernel still provides all other features (IPC, budgets, memory blocks, context assembly, etc).

### Connect with Python

```bash
pip install -e sdk/python/
```

```python
from clove_sdk.client import CloveClient

with CloveClient("/tmp/clove.sock", agent_id=1) as client:
    # Say hello
    print(client.hello())

    # Think with an LLM
    result = client.think("What is the capital of France?")
    print(result["content"])

    # Store some state
    client.store("my_key", {"data": "hello"})
    print(client.fetch("my_key"))

    # Create a memory block
    block = client.mem_create("notes", type="core", content="My first note")
    print(block)

    # Set a budget (max 10 LLM calls worth of tokens)
    client.set_budget(max_tokens=50000, max_cost_usd=1.0, max_steps=100)
    print(client.get_budget())
```

### Dashboard

Open `http://localhost:8080` in your browser for the live HTMX dashboard (requires `--api` flag).

---

## What CLOVE Does

CLOVE manages AI agents the way Linux manages processes.

| What You Need | How CLOVE Does It |
|---|---|
| Run agents | `SYS_SPAWN` / `SYS_KILL` / `SYS_PAUSE` / `SYS_RESUME` |
| Agents talk to each other | Mailbox IPC (`SYS_SEND` / `SYS_RECV`) + Event Bus |
| Agents share data | State Store (`SYS_STORE` / `SYS_FETCH`) + Memory Blocks |
| Agents remember things | 3-tier memory: working (context) / session (RAM) / persistent (SQLite) |
| Control LLM costs | Four budgets: token, step, time, cost — kernel-enforced |
| Isolate agents | Linux namespaces, cgroups v2, Landlock, seccomp (Linux only) |
| Debug agent behavior | Execution replay: record every syscall, play it back |
| Audit everything | 8-category audit log with full provenance |
| Use any LLM | OpenRouter: 300+ models (GPT-4o, Claude, Gemini, Llama, Mistral...) |
| Connect to tools | MCP bridge (databases, APIs, browsers) |
| Schedule work | 5 priority levels, state-aware scheduling |
| Filter PII | Scan/redact/block personal data before it reaches LLMs |

---

## Python SDK

```python
from clove_sdk.client import CloveClient

client = CloveClient("/tmp/clove.sock", agent_id=1)
client.connect()

# ── LLM ──────────────────────────────────────────
client.think("summarize this document")
client.think_with_context("analyze", chain_id="chain_abc")

# ── State ────────────────────────────────────────
client.store("key", {"any": "json"}, scope="global")
client.fetch("key")
client.keys(prefix="agent_")

# ── IPC ──────────────────────────────────────────
client.send_message(target_id=2, content="hello agent 2")
client.recv_messages()
client.broadcast("announcement to all")

# ── Events ───────────────────────────────────────
client.subscribe("AGENT_SPAWNED")
client.emit("CUSTOM", {"detail": "something happened"})
client.poll_events()

# ── Memory Blocks ────────────────────────────────
client.mem_create("persona", type="system", content="You are a researcher")
client.mem_create("findings", type="core", content="")
client.mem_write(block_id, "Updated findings...")
client.mem_append(block_id, "\nMore findings")
client.mem_share(block_id, target_agent_id=2)
client.mem_list()

# ── Artifacts & Chains ───────────────────────────
chain = client.chain_create("research-project")
art = client.doc_create(type="research", title="Market Analysis",
                        content="...", chain_id=chain["chain"]["id"])
client.doc_update(art["artifact"]["id"], state="final")
context = client.context_assemble(chain_id=chain["chain"]["id"])

# ── Budget ───────────────────────────────────────
client.set_budget(max_tokens=100000, max_steps=500,
                  max_time_ms=60000, max_cost_usd=2.0,
                  kill_on_exceeded=True)
client.get_budget()

# ── Priority ─────────────────────────────────────
client.set_priority("critical")
client.get_priority()

# ── Agents ───────────────────────────────────────
client.spawn("worker", "python worker.py")
client.list_agents()
client.kill(agent_id=2)

# ── HTTP / Exec ──────────────────────────────────
client.http("https://api.example.com/data")
client.exec_command("ls -la /tmp")

# ── Audit / Replay ───────────────────────────────
client.record_start()
# ... do things ...
client.record_stop()
client.replay_start()
client.get_audit_log(category="SECURITY")

client.disconnect()
```

---

## CLI Flags

```
./clove_kernel [OPTIONS]

Core:
  --socket <path>             Unix socket path (default: /tmp/clove.sock)
  --no-sandbox                Disable process sandboxing (required on macOS)
  --db <path>                 SQLite database path (default: clove.db)

LLM:
  --openrouter                Enable OpenRouter (300+ models)
  --openrouter-key <key>      OpenRouter API key (or set OPENROUTER_API_KEY)
  --llm-model <model>         Default model (default: google/gemini-2.0-flash-001)
  --llm-proxy                 Enable inference gateway (cost limits, model allowlist)
  --llm-max-cost <usd>        System-wide LLM cost cap
  --llm-allowed-models <list> Comma-separated model allowlist

API:
  --api                       Enable REST API + dashboard
  --api-port <port>           API port (default: 8080)
  --api-key <key>             API auth key (or set CLOVE_API_KEY)

Privacy:
  --privacy                   Enable PII filter
  --privacy-mode <mode>       audit | redact | block

Integrations:
  --mcp                       Enable MCP bridge (tool ecosystem)
  --a2a                       Enable A2A bridge (agent-to-agent protocol)
  --otel                      Enable OpenTelemetry export

Advanced:
  --manifest <path>           Load agent fleet from manifest YAML
  --policy-file <path>        Hot-reloadable policy JSON
  --egress-proxy              Enable HTTP egress proxy
```

Environment variables: `OPENROUTER_API_KEY`, `CLOVE_API_KEY` (also reads `.env` file).

---

## Project Structure

```
clove-v2/
├── kernel/src/           # Kernel binary
│   ├── kernel.cpp        # Wires 22 syscall modules
│   ├── syscall_router.cpp# Dispatch + budget middleware
│   └── syscalls/         # 22 handler files (86 opcodes)
├── libs/
│   ├── core/             # Protocol (86 opcodes), config
│   ├── reactor/          # Event loop (kqueue on macOS, epoll on Linux)
│   ├── ipc/              # Unix socket server/client
│   ├── agents/           # Process management + restart policies
│   ├── sandbox/          # Namespaces, cgroups, Landlock, seccomp (Linux)
│   ├── orchestration/    # Scheduler, LLM queue, state store, events, mailbox
│   ├── context/          # Artifacts, chains, memory blocks, context assembler
│   ├── governance/       # Permissions, audit, PII, budget, policy, replay
│   ├── integrations/     # OpenRouter, MCP, A2A, tunnels, OTel
│   ├── persistence/      # SQLite (5 tables, WAL mode, write-through)
│   ├── worlds/           # Multi-tenant isolation
│   └── api/              # REST API (34 routes) + HTMX dashboard
├── cli/src/              # CLI binary
├── sdk/python/           # Python SDK (86 methods + Agent class)
├── tests/                # 19 test files (Catch2)
├── benchmark/            # Performance benchmarks
├── deploy/               # Dockerfile, docker-compose, systemd
└── docs/                 # Architecture, research, capabilities
```

---

## Running Tests

```bash
cd build
ctest --output-on-failure
```

Or run the test binary directly:

```bash
./build/tests/clove_tests
```

---

## macOS Limitations

On macOS, CLOVE runs with reduced isolation:
- No Linux namespaces (PID/NET/MNT/UTS)
- No cgroups v2 resource limits
- No Landlock filesystem ACLs
- No seccomp BPF syscall filtering
- Process isolation via fork only

All other features work fully: IPC, events, state store, memory blocks, context assembly, budgets, scheduling, LLM integration, audit logging, execution replay, REST API, persistence.

For full isolation, deploy on Linux (bare metal, VM, or Docker).

---

## Linux (Full Isolation)

```bash
# Build
sudo apt install cmake g++ libcurl4-openssl-dev libssl-dev libsqlite3-dev
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Run with sandboxing
sudo ./kernel/clove_kernel --openrouter --api --db /var/lib/clove/clove.db
```

Root (or `CAP_SYS_ADMIN`) is needed for namespace isolation. Without it, falls back to fork (like macOS).

---

## Docker

```bash
cd deploy
docker-compose up -d
```

Exposes:
- Port 8080: REST API + dashboard
- Port 8081: A2A bridge

---

## Docs

| Document | What It Covers |
|---|---|
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | System diagrams, data flows, dependency graph |
| [CAPABILITIES.md](docs/CAPABILITIES.md) | All 86 syscalls, all features, OpenShell comparison |
| [ROADMAP.md](docs/ROADMAP.md) | What's done, what's next |
| [PITCH.md](PITCH.md) | Market positioning and competitive analysis |

Architecture docs in `docs/architecture/`:
- [Context Layer](docs/architecture/CONTEXT_LAYER.md) — artifacts, chains, context assembly
- [Memory Architecture](docs/architecture/MEMORY_ARCHITECTURE.md) — 3-tier memory, memory blocks
- [Scheduler Design](docs/architecture/SCHEDULER_DESIGN.md) — priority queue, state tracking
- [Security & Compliance](docs/architecture/SECURITY_COMPLIANCE.md) — OWASP mapping, EU AI Act, budgets

---

## License

MIT
