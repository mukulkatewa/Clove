# CLOVE Dashboard — Product Plan v3

> Last updated: 2026-04-02
> Status: Planning → Ready to build

---

## Core Mental Model

**Build agents → Attach jobs with triggers → Deploy into worlds → Worlds contain swarms → Scale and observe worlds**

CLOVE is not a prompt runner. It's a platform where you build always-on agent workers, wire them into pipelines with triggers, deploy them into isolated worlds, and scale/observe the fleet.

---

## Dashboard Structure

### ZONE 1: BUILD (Create, design, wire)

#### 1. Agents (`/dashboard/agents`)
The heart of the product. An agent is a **worker with a job description**.

**Agent List View:**
- Grid/list of all defined agents
- Each card shows: name, role, runtime, LLM, active jobs count, last run, status (active/idle/error)
- Quick actions: enable/disable, run now, duplicate, delete
- Filter by: runtime, status, MCP connection
- "Create Agent" button → agent builder

**Agent Detail View (click into an agent):**

| Tab | Contents |
|-----|----------|
| **Config** | Name, role/system prompt, LLM provider + model, runtime (CLOVE/Claude Code/Codex/OpenClaw), tools, MCP connections, budget per run, sandbox limits |
| **Jobs** | List of jobs attached to this agent. Each job has: trigger, pipeline steps, output destination, budget, status toggle. "Add Job" button. |
| **Runs** | History of every execution — timestamp, trigger source, steps taken, result, cost, duration. Click to expand full log. |
| **Logs** | Live streaming output when running. Historical logs searchable. |

**Agent Builder (create/edit):**
- Step 1: Identity — name, role description, avatar
- Step 2: Brain — LLM provider (native Claude/Gemini/GPT/Llama/Qwen or OpenRouter) + model picker
- Step 3: Runtime — CLOVE RunEngine / Claude Code / Codex / OpenClaw
- Step 4: Tools — toggle tools (read_file, write_file, exec, search, http, etc.) + MCP tools from connected services
- Step 5: Connections — which MCP services this agent can access (GitHub, Slack, Sentry, etc.)
- Step 6: Guardrails — budget per run, max runs per day, sandbox memory/CPU limits, permission scope
- Step 7: Review & Deploy

#### 2. Jobs & Pipelines (`/dashboard/pipelines`)
Where you wire multi-step workflows and recurring tasks.

**Pipeline List View:**
- All defined pipelines with: name, step count, trigger type, last run, status
- Visual mini-flow preview (Step 1 → Step 2 → Step 3)

**Pipeline Builder:**
- Visual canvas: drag agent nodes, connect them with arrows
- Each node = one step: pick agent (or create inline), set runtime, set input/output keys
- Context flows between steps via world state (output_key → input_keys)
- Set trigger for the whole pipeline:
  - **Manual** — run from dashboard or CLI
  - **Cron** — schedule (every hour, daily at 6AM, weekly Monday)
  - **Webhook** — external event (GitHub push, Sentry alert, Slack message)
  - **Agent output** — triggered when another agent/pipeline completes
  - **MCP event** — triggered by connected service event
- Set pipeline-level budget cap
- Test run button (runs once with live output)

**Job = Agent + Trigger + Pipeline + Output**
```
Agent: Security Auditor
Job: "Nightly Scan"
  Trigger:  cron — 06:00 UTC daily
  Pipeline: [scan_deps] → [check_cves] → [open_fix_prs]
  Output:   Slack #security via MCP
  Budget:   $0.15/run
  Status:   active
```

#### 3. Connections (`/dashboard/connections`)
MCP services + LLM providers in one place.

**Services tab:**
- Grid of available services (GitHub, Slack, Linear, Sentry, PagerDuty, Notion, PostgreSQL, etc.)
- Connected services show green badge + tool count
- Click to connect: paste token, one-click setup, hot-reload (no kernel restart)
- Click connected service: see available tools, test a tool, disconnect

**LLM Providers tab:**
- Native providers: Anthropic (Claude), Google (Gemini), OpenAI (GPT), Meta (Llama), Alibaba (Qwen)
- For each: API key input, model selector, test connection, cost tracking
- OpenRouter: fallback/universal option — one key for all models
- Model catalog: browse all available models across providers, see pricing, context window, capabilities
- Default model selector: which model agents use if none specified

#### 4. Templates (`/dashboard/templates`)
Pre-built agents and pipelines to start from.

- Agent templates: Code Reviewer, Research Analyst, Security Auditor, Doc Generator, Incident Responder, etc.
- Pipeline templates: PR Review Flow, Nightly Security Scan, Research & Brief, Incident Response, etc.
- Community templates (future): shared by other CLOVE users
- One-click deploy: pick template → customize → deploy

---

### ZONE 2: OPERATE (Run, observe, scale)

#### 5. Worlds (`/dashboard/worlds`)
**The primary operating view.** This is the "home" of operate.

A World = an isolated environment where agents run. Think of it as a project workspace.

**Worlds List:**
- All worlds: name, agent count, status (active/idle/stopped), cost today, created date
- Quick actions: launch, stop, delete
- "Create World" → name, description, select agents to deploy

**World Detail View (click into a world):**

| Tab | Contents |
|-----|----------|
| **Overview** | Live status: running agents, active jobs, current cost, shared state entries, recent activity feed |
| **Agents** | Agents deployed in this world. Add/remove agents. See per-agent status, last action, cost. |
| **Swarm** | Visualization of agents communicating — the IPC diagram from the landing page but live, showing real messages between agents in this world |
| **State** | Key-value state store for this world. Agents read/write here. View, edit, delete entries. This is how pipeline context flows between steps. |
| **Logs** | Unified log stream from all agents in this world. Filter by agent, level, time. |
| **Cost** | Cost breakdown per agent, per job, per day. Budget remaining. |

#### 6. Fleet & Swarms (`/dashboard/fleet`)
Launch and manage multi-agent deployments.

**Launch Fleet:**
- Pick a world (or create new)
- Add agents: select from defined agents, set count per agent (scale!)
- Set fleet goal (shared objective)
- Set fleet budget
- Launch → live execution view with per-agent progress lanes

**Swarm View:**
- Live visualization of N agents working together
- Node graph showing inter-agent communication
- Per-agent: status, current task, cost, runtime
- Scale controls: add more instances, pause agents, kill agents
- This is the "scaling dashboard" — you see the swarm and control it

#### 7. Runs (`/dashboard/runs`)
Single-shot agent execution for quick tasks.

- Chat-style input: describe what you want
- Pick agent (or let CLOVE auto-select)
- Pick model, tools, budget
- Execute → see live tool calls, reasoning, result
- History of all runs with results, cost, duration

#### 8. Audit (`/dashboard/audit`)
Full trail of everything.

- Every agent action: file reads, writes, exec calls, API calls, MCP tool calls
- Filter by: agent, world, time range, action type, cost
- Export to JSON/CSV
- Compliance view: EU AI Act checklist, data processing log
- Cost breakdown: per agent, per world, per provider, per day/week/month

#### 9. Settings (`/dashboard/settings`)
System configuration.

- **API Keys** — kernel API token management
- **Permissions** — global permission policies for agents
- **Privacy** — PII scanning config, data retention rules
- **Billing** — cost limits, alerts, usage quotas
- **Team** (future) — invite members, roles, access control

---

## LLM Provider Architecture

**Current:** Everything through OpenRouter (one API key, one format)

**Target:** Native multi-provider support

| Provider | Models | API Format | Status |
|----------|--------|------------|--------|
| Anthropic | Claude Sonnet 4, Claude Opus 4, Haiku | Anthropic Messages API | To build |
| Google | Gemini 2.5 Pro, Gemini 2.5 Flash | Google AI API | To build |
| OpenAI | GPT-4o, GPT-4o Mini, o3-mini | OpenAI Chat API | To build |
| Meta | Llama 3.3, Llama 4 | Via OpenRouter or local | OpenRouter works |
| Alibaba | Qwen 2.5 | Via OpenRouter or local | OpenRouter works |
| OpenRouter | All of the above | OpenAI-compatible | Working ✓ |

**How it works:**
- User adds API keys per provider in Connections → LLM Providers
- When an agent runs, kernel checks: does this model's provider have a direct key?
  - Yes → call provider API directly (cheaper, faster, no middleman)
  - No → fall back to OpenRouter
- Dashboard model picker shows all available models across all configured providers
- Cost tracked per provider

---

## Containerization / Sandbox Story

**CLOVE sandbox vs Docker:**

| | CLOVE Sandbox | Docker |
|--|--------------|--------|
| Spawn time | <5ms (kernel namespaces) | 500ms-2s (daemon + image) |
| Memory overhead | ~10MB per agent | ~58MB per container |
| Isolation | PID + mount + network namespace + seccomp + Seatbelt (macOS) | Full container with cgroup |
| Image pulls | None (native binary) | Yes (layer downloads) |
| Agent density | 500+ on a single machine | ~30-80 before OOM |
| Daemon required | No | Yes (dockerd) |
| Startup dependency | None | Docker daemon must be running |

**Key claim:** "OS-level isolation without container overhead. 10x more agents per machine."

This is NOT Docker. This is lighter, faster, and native. The kernel IS the container runtime.

---

## Scaling (Future — Zone 2 expansion)

Not built yet. The vision:

**Single machine scaling (Phase 1):**
- "Scale to N" button on any agent in a world
- Kernel spawns N instances of the same agent definition
- Load balancer distributes incoming work across instances
- Dashboard shows: instance count, per-instance CPU/memory, queue depth

**Multi-machine scaling (Phase 2):**
- CLOVE kernel runs on multiple machines
- Central coordinator distributes worlds across machines
- Agent instances can span machines
- Dashboard: cluster view, per-machine health, agent placement

**Auto-scaling (Phase 3):**
- Rules: "if queue depth > 10, add 2 instances"
- "if cost > $X/hour, cap at N instances"
- "if error rate > 5%, pause and alert"
- Dashboard: scaling policies, scaling events log

---

## Build Order

### Phase 1: Builder (Now)
1. Redesign `/dashboard/agents` — agent builder with config + jobs tabs
2. Redesign `/dashboard/pipelines` — visual pipeline builder with triggers
3. Redesign `/dashboard/connections` — MCP + LLM providers unified
4. Clean up navigation — BUILD zone (Agents, Pipelines, Connections, Templates) + OPERATE zone (Worlds, Fleet, Runs, Audit, Settings)

### Phase 2: Operator (Next)
5. Redesign `/dashboard/worlds` — primary operating view with swarm visualization
6. Redesign `/dashboard/fleet` — launch fleets with scale controls
7. Redesign `/dashboard/runs` — clean single-shot execution
8. Native LLM provider support in kernel (Anthropic, Google, OpenAI APIs)

### Phase 3: Scaler (Future)
9. Single-machine scaling: N instances per agent
10. Scaling dashboard: instance count, queue depth, per-instance metrics
11. Auto-scaling rules
12. Multi-machine coordination

---

## What Gets Cut

These pages merge into the structure above and no longer exist as separate pages:

| Old Page | Merges Into |
|----------|-------------|
| `/dashboard/swarm` | Worlds → Swarm tab |
| `/dashboard/memory` | Worlds → State tab |
| `/dashboard/mcp` | Connections page |
| `/dashboard/providers` | Connections → LLM Providers tab |
| `/dashboard/openclaw` | Agents (OpenClaw is a runtime option) |
| `/dashboard/ipc` | Worlds → Swarm tab (shows messages) |
| `/dashboard/inference` | Connections → LLM Providers tab |
| `/dashboard/permissions` | Settings → Permissions |
| `/dashboard/privacy` | Settings → Privacy |
| `/dashboard/replay` | Runs → click a run → replay |
| `/dashboard/governance` | Audit page |
| `/dashboard/activity` | Worlds → Overview tab |
| `/dashboard/cost` | Audit → Cost tab |

**Result: 22 pages → 9 pages.** Every feature preserved, zero duplication.

---

## Competitive Moat

Why nobody can copy this easily:

1. **C++ kernel** — not a Python wrapper. 86 syscalls, 16.8K LOC, real OS primitives. Takes years to build.
2. **Native sandbox** — lighter than Docker, no daemon dependency. Unique to CLOVE.
3. **Multi-runtime** — Claude Code + Codex + OpenClaw + native RunEngine in one orchestrator. Nobody else does this.
4. **Builder + Runner + Scaler** — one product, not three tools duct-taped together.
5. **MCP-native** — 15 services, hot-reload, one-click. Not bolted on.
6. **Jobs & Triggers** — agents aren't one-shot prompts, they're always-on workers with schedules.

**Claude Code** = just a runtime. No builder, no scaler, no worlds, no governance.
**Codex** = just a runtime. Same limitations.
**LangChain** = just glue. No kernel, no sandbox, no scaling.
**CrewAI/AutoGen** = Python frameworks. No isolation, no persistence, no production scaling.
**Docker** = just containers. No agent intelligence, no LLM routing, no orchestration.

CLOVE = **the operating system**. Everything else is an app that runs on top.
