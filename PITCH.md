# CLOVE — The Control Plane for AI Agents

> Run agents anywhere. Scale to thousands. Sleep at night.

---

## The Problem

You built an AI agent. It works on your laptop. Now what?

- **Where does it run in production?** Not your laptop.
- **What happens when it goes rogue?** Burns $47K in API calls at 3am.
- **How do you run 10 agents that cooperate?** HTTP between Docker containers? Good luck at 150ms per message.
- **What does the auditor see?** Container logs? That's not compliance.
- **Can you scale to 1,000 agents?** Not on a K8s cluster eating 907MB per sandbox.

Every agent framework (LangChain, CrewAI, OpenClaw, OpenAI Agents) builds the brain. **Nobody builds the body** — the infrastructure that runs agents safely, coordinates them, and scales them.

CLOVE is that infrastructure.

---

## What CLOVE Is

**CLOVE is the control plane for AI agents.** One binary. Deploy anywhere — your laptop, a server, edge devices, the cloud. Run any agent from any framework with kernel-level isolation, fleet coordination, and full compliance.

```
Your Agent (Python, Node, any language)
    ↓
CLOVE Kernel (2MB binary, 3MB RAM)
    ├── Isolates it (namespaces, cgroups, Landlock, seccomp)
    ├── Controls its LLM access (model allowlists, cost caps, PII filtering)
    ├── Coordinates it with other agents (IPC at 54,000 ops/sec)
    ├── Records everything (full execution replay for auditors)
    └── Scales it (edge to cloud, 1 to 10,000 agents)
```

---

## For Developers: Run It On Your Machine

```bash
# Install (one command)
curl -fsSL https://get.cloveos.com | sh

# Deploy any agent
clove deploy ./my-langchain-agent
clove deploy ./my-crewai-crew
clove deploy ./my-openclaw-agent

# That's it. Agent is running with:
# ✓ Process isolation
# ✓ LLM cost controls
# ✓ PII filtering on every prompt
# ✓ Full audit trail
# ✓ Execution replay for debugging
```

**Zero config.** CLOVE auto-detects your framework (LangChain, CrewAI, OpenClaw, OpenAI Agents, custom), installs dependencies, and starts the agent with sensible security defaults.

**Works on your MacBook.** Full orchestration runs on macOS. Isolation degrades gracefully (fork + egress proxy instead of namespaces). For production with full isolation, deploy on Linux.

**Dev mode.** The Python SDK includes `LocalTransport` — agents work without running the kernel. Edit code, run tests, no Docker, no ceremony.

```python
from clove_sdk import Agent, Crew, Task, tool

@tool
def search_web(query: str) -> str:
    """Search the web for information."""
    return requests.get(f"https://api.search.com?q={query}").text

researcher = Agent(name="researcher", tools=[search_web])
writer = Agent(name="writer")

crew = Crew(
    agents=[researcher, writer],
    tasks=[
        Task("Research quantum computing", agent=researcher),
        Task("Write a blog post about it", agent=writer),
    ]
)
result = crew.run()
```

---

## For OpenClaw Users: Secure Your Agent in 60 Seconds

OpenClaw is powerful. Gartner called it "insecure by default." CLOVE fixes that.

```bash
# Install CLOVE
curl -fsSL https://get.cloveos.com | sh

# Run OpenClaw inside CLOVE
clove deploy ~/.openclaw --name my-openclaw

# Your OpenClaw agent now has:
# ✓ Kernel-level process isolation
# ✓ Network egress filtering (blocks unauthorized API calls)
# ✓ PII detection on every LLM prompt (SSN, email, phone, CC auto-redacted)
# ✓ LLM cost caps ($X/day limit, auto-kill on breach)
# ✓ Full audit trail (every action logged, queryable, exportable)
# ✓ Execution replay (debug any issue by replaying the exact sequence)
```

**No NVIDIA lock-in.** CLOVE works with any LLM provider — OpenAI, Anthropic, Google, Groq, Mistral, local models. Use whatever you want through OpenRouter (300+ models, one API key).

**No Docker required.** CLOVE is a 2MB binary. Not a Kubernetes cluster. Not a VM. Not 3.5GB of container images. One binary that runs everywhere.

---

## For Teams: Agents Becoming Edge

AI agents are moving to the edge. Customer service agents on branch servers. Research agents on developer laptops. Monitoring agents on IoT gateways. CLOVE makes this possible because it's **radically lightweight:**

| | CLOVE | Docker-Based (OpenShell, E2B) |
|---|---|---|
| Binary size | **2.1 MB** | 3,500+ MB |
| Idle memory | **2.8 MB** | 907+ MB |
| Cold start | **27 ms** | 3,300+ ms |
| IPC latency | **0.02 ms** | 150+ ms |

**2.1MB fits on a Raspberry Pi.** Run agents at the edge — retail stores, factory floors, field offices — where Docker can't go.

**27ms cold start enables serverless agents.** Spin up per-request, serve, shut down. Like Lambda but for agents.

**0.02ms IPC enables real-time coordination.** Ten agents collaborating on a single customer query in under 5ms total coordination overhead.

---

## For Enterprises: Scale Your Agents

### The Fleet Problem

You don't have one agent. You have:
- 50 customer service agents handling tickets
- 10 research agents monitoring competitors
- 20 data processing agents running ETL pipelines
- 5 DevOps agents managing infrastructure

That's 85 agents. They need to cooperate, share state, respect budgets, and not leak customer data.

**OpenShell sandboxes one agent at a time.** No inter-agent communication. No shared state. No fleet-wide cost controls.

**CLOVE orchestrates the fleet.**

```
┌─────────────────────────────────────────────────────────┐
│                    CLOVE KERNEL                          │
│                                                          │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐           │
│  │ CS Agent 1 │  │ CS Agent 2 │  │ CS Agent 3 │  ...×50 │
│  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘           │
│        │               │               │                 │
│   ─────┴───────────────┴───────────────┴─────            │
│   IPC Bus (0.02ms per message, 54K ops/sec)              │
│   Shared State Store (KV with TTL, scoped access)        │
│   Event Bus (typed pub/sub, real-time coordination)      │
│                                                          │
│  GOVERNANCE:                                             │
│  • Per-agent permissions (CS agents can't access DevOps) │
│  • Fleet-wide cost cap ($500/day across all agents)      │
│  • PII auto-redaction on every LLM call                  │
│  • Full audit trail (who did what, when, why)            │
│  • Execution replay (debug any agent's full history)     │
│                                                          │
│  COMPLIANCE:                                             │
│  • EU AI Act ready (sandbox + audit + replay)            │
│  • HIPAA audit trails (immutable, exportable)            │
│  • SOC2 evidence (structured logs, access controls)      │
└─────────────────────────────────────────────────────────┘
```

### Multi-Agent Coordination (Nobody Else Has This)

```python
# Research crew: 5 agents cooperating on a single deliverable
crew = Crew(
    agents=[researcher, analyst, writer, reviewer, publisher],
    tasks=[
        Task("Find data on Q1 earnings", agent=researcher),
        Task("Analyze trends", agent=analyst),
        Task("Write the report", agent=writer),
        Task("Review for accuracy", agent=reviewer),
        Task("Format and deliver", agent=publisher),
    ]
)

# Under the hood:
# - Each agent runs in its own sandbox
# - They communicate via kernel IPC (0.02ms per message)
# - Shared state store holds intermediate data
# - Event bus coordinates task handoffs
# - Total coordination overhead: <5ms for the entire pipeline
```

### Inference Governance

```bash
# Start kernel with LLM controls
clove_kernel \
  --llm-proxy \
  --llm-allowed-models "gpt-4o,claude-sonnet-4,gemini-2.0-flash" \
  --llm-max-cost 500.0 \
  --privacy --privacy-mode redact \
  --openrouter --openrouter-key sk-or-xxx
```

**What this does:**
- Only GPT-4o, Claude Sonnet, and Gemini Flash are allowed (everything else blocked)
- Fleet-wide cost cap of $500 (agents killed when limit hit)
- PII auto-redacted from every prompt before it reaches the LLM
- All requests routed through OpenRouter (300+ models, automatic failover, zero data retention option)

### Hot-Reload Policies (No Downtime)

Security incident at 3am? Update the policy file:

```json
{
  "llm": { "allowed_providers": ["gemini"], "max_cost_usd": 0 },
  "agents": {
    "compromised-agent": {
      "permissions": { "can_http": false, "can_exec": false, "can_think": false }
    }
  }
}
```

Save the file. CLOVE detects the change instantly (kqueue/inotify). Agent permissions updated in-memory. No restart. No downtime. The compromised agent can't do anything until you investigate.

---

## How It Compares

### vs OpenShell (NVIDIA)

| | CLOVE | OpenShell |
|---|---|---|
| **Multi-agent** | Native IPC + state + events | Single agent per sandbox |
| **Cold start** | 27ms | 3,300ms |
| **IPC speed** | 0.02ms (54K ops/sec) | ~150ms (6 ops/sec) |
| **Memory** | 2.8 MB | 907 MB |
| **Size** | 2.1 MB binary | 3.5 GB container stack |
| **PII filtering** | Shipping (regex + patterns) | Announced (not in code) |
| **Execution replay** | Full syscall recording | Not available |
| **Cost controls** | Per-agent USD limits | Not available |
| **Requires** | Nothing | Docker + K3s + Colima VM |
| **Runs on** | Laptop, edge, cloud, Pi | Servers with 4GB+ RAM |

### vs E2B / Modal / Daytona

| | CLOVE | E2B/Modal/Daytona |
|---|---|---|
| **Multi-agent** | Native fleet orchestration | Single sandbox per task |
| **IPC** | 0.02ms kernel-native | HTTP between containers |
| **Execution replay** | Built-in | Not available |
| **Edge deployment** | 2.1 MB, runs anywhere | Cloud-only |
| **Cost** | Free + self-hosted | Usage-based pricing |

### vs Running Raw (No Sandbox)

| | CLOVE | No Sandbox |
|---|---|---|
| **Agent goes rogue** | Killed by cost cap in <1s | $47K bill at 3am |
| **Agent leaks PII** | Auto-redacted, never reaches LLM | SSNs sent to OpenAI |
| **Agent crashes** | Auto-restart with backoff | Manual restart |
| **Auditor asks "what happened"** | Full execution replay | "We don't know" |
| **Compliance** | EU AI Act, HIPAA, SOC2 ready | Legal risk |

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        CLOVE KERNEL                           │
│                                                               │
│  LAYER 4: CONTROL PLANE                                      │
│  REST API · WebSocket · Dashboard · Multi-Tenant Auth         │
│                                                               │
│  LAYER 3: GOVERNANCE                                          │
│  Inference Gateway · PII Filter · Audit · Execution Replay    │
│  Cost Controls · Policy Hot-Reload · Policy Recommendations   │
│                                                               │
│  LAYER 2: ORCHESTRATION (the moat)                            │
│  Agent IPC (54K ops/sec) · Shared State · Event Bus           │
│  LLM Queue (8 workers) · Async Tasks · Crew/Task SDK          │
│                                                               │
│  LAYER 1: ISOLATION                                           │
│  Linux: Namespaces + cgroups + Landlock + seccomp             │
│  macOS: Fork + Egress Proxy (dev mode)                        │
│  All: Per-agent permissions, domain allowlists, cmd blocklists│
└──────────────────────────────────────────────────────────────┘
```

**66 kernel syscalls.** Agents talk to the kernel via a binary IPC protocol (17-byte header + JSON payload). Every action — LLM call, HTTP request, file access, agent-to-agent message — goes through the kernel. The kernel enforces permissions, logs everything, and coordinates the fleet.

---

## Integration Ecosystem

CLOVE plugs into everything:

| Integration | What It Does | Status |
|---|---|---|
| **OpenRouter** | 300+ LLM models through one API | Ready |
| **MCP** | Model Context Protocol — agents access any tool | Planned |
| **A2A** | Google's Agent-to-Agent protocol — cross-system coordination | Planned |
| **OpenTelemetry** | Export traces to Datadog, Grafana, New Relic | Planned |
| **Langfuse** | LLM observability and debugging | Planned |
| **LangChain** | Framework adapter — deploy LangChain agents | Ready |
| **CrewAI** | Framework adapter — deploy CrewAI crews | Ready |
| **OpenAI Agents** | Framework adapter — deploy OpenAI agents | Ready |
| **OpenClaw** | Deploy OpenClaw with full isolation | Ready |
| **Presidio** | Microsoft's PII detection engine | Planned |

---

## Pricing

| Tier | Price | What You Get |
|---|---|---|
| **Open Source** | Free forever | Full kernel, all features, self-hosted |
| **Cloud** | $99/mo | Managed hosting, 10 agents, dashboard, 10K LLM calls |
| **Pro** | $499/mo | 100 agents, priority support, SLA, advanced analytics |
| **Enterprise** | Custom | Unlimited agents, SSO, audit export, dedicated support, SOC2 |

---

## The Numbers

```
Cold start:        27ms (122x faster than OpenShell)
IPC latency:       0.02ms (7,450x faster than container exec)
Throughput:        54,279 ops/sec (8,750x more than SSH-based)
Memory:            2.8 MB idle (324x lighter than Docker stack)
Binary:            2.1 MB (1,690x smaller than container images)
Shutdown:          1ms (instant graceful stop)

Syscalls:          66 kernel operations
Event types:       12 typed pub/sub events
Audit categories:  8 structured log categories
PII patterns:      5 built-in + custom regex
LLM providers:     300+ via OpenRouter
```

---

## Getting Started

### 1. Install

```bash
curl -fsSL https://get.cloveos.com | sh
```

### 2. Deploy an Agent

```bash
# Any framework
clove deploy ./my-agent

# With controls
clove deploy ./my-agent \
  --llm-proxy --llm-allowed-models "gpt-4o" \
  --privacy --privacy-mode redact \
  --restart on-failure
```

### 3. Monitor

```bash
# See running agents
clove ps

# Stream logs
clove logs my-agent -f

# View audit trail
clove audit --category SECURITY --last 1h
```

### 4. Scale

```bash
# Deploy to cloud
clove deploy ./my-agent --cloud

# Deploy a fleet from manifest
clove_kernel --manifest fleet.json
```

---

## Why Now

1. **250K developers** are running OpenClaw with zero security. They need a control plane.
2. **EU AI Act** takes effect August 2026. Sandbox + audit + replay becomes legally required.
3. **Multi-agent is the next wave.** Every real workflow needs 5-10 cooperating agents. Nobody has the infrastructure for this.
4. **Edge AI is happening.** Agents running on branch servers, IoT gateways, field laptops. Docker doesn't fit. CLOVE does.
5. **NVIDIA just validated the market.** OpenShell proves agent infrastructure is a category. But they built for one agent at a time. We built for fleets.

---

## Who We Are

**CLOVE (Clove Lightweight Operating Virtualization Environment)** — agent fleet infrastructure for the real world.

- **Started:** Late 2025 (months before OpenShell launched)
- **Architecture:** C++23 microkernel, kernel-native isolation
- **Open source:** Apache 2.0
- **Website:** [cloveos.com](https://cloveos.com)

---

*The agents are coming. The question isn't whether to deploy them — it's whether you can control them when you do. That's what CLOVE is for.*
