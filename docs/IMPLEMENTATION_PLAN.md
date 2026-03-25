# CLOVE v2 — Implementation Plan

> **Date:** 2026-03-25
> **Goal:** A corporate manager opens a dashboard, defines department agents, hits run, and watches a fleet automate their workflows — with cost control, audit trails, and zero risk of agents going rogue.

---

## Architecture (the mental model)

```
CLOVE Kernel (the OS)
  │
  ├── RunEngine (built-in agent runtime)
  │     POST /api/run, /api/fleet
  │     Any goal, any tools, no dependencies
  │     This is the default. Most agents use this.
  │
  ├── OpenClaw Module (optional — chat platform integration)
  │     POST /api/openclaw/spawn, /api/openclaw/fleet
  │     For agents that need WhatsApp, Telegram, Slack, Discord, etc.
  │     Loads only when needed. Not a dependency.
  │
  ├── OpenAI-Compatible Endpoint (LLM proxy for any tool)
  │     POST /api/v1/chat/completions
  │     Cursor, LangChain, LlamaIndex, any OpenAI-speaking tool
  │     gets cost tracking + PII + audit for free
  │
  └── Dashboard (the product surface)
        The manager sees agents, budgets, results
        Doesn't know what a kernel or syscall is
```

**CLOVE is the OS. RunEngine is the built-in shell. OpenClaw is an app that runs on it. The LLM proxy is a system service. The dashboard is the desktop.**

The manager doesn't choose between these. They say: "I need an agent that reads support tickets from Slack and drafts replies." The kernel figures out the rest — if it needs Slack, load OpenClaw module for that agent. If it just needs to research and write, RunEngine handles it natively.

---

## Where we are today

### Done

| What | Status |
|------|--------|
| Kernel compiles, all 86 syscalls, 165 tests passing | Done |
| RunEngine with 10 real tools (file, exec, HTTP, search, MCP, memory) | Done |
| 51 REST API endpoints (think, run, stream, fleet, history, audit, cost, memory) | Done |
| Permission checks on all RunEngine tools (read/write/exec/http) | Done |
| Context assembly wired into RunEngine | Done |
| PII filtering on every LLM call | Done |
| OpenClaw plugin scaffold (TypeScript, 970 LOC, builds, tested) | Done |
| API spec for frontend developer (1,404 lines) | Done |
| Docs updated and verified | Done |
| Live tested: fleet 3 agents, SSE streaming, cost tracking, audit | Done |

### Not done

| What | Why it matters |
|------|---------------|
| macOS sandbox (sandbox-exec) | Fork-only on Mac = no file/network restrictions |
| OpenAI-compatible LLM endpoint | OpenClaw + every other tool speaks OpenAI format |
| OpenClaw spawn endpoint | Can't spawn real OpenClaw instances inside kernel |
| Agent-to-agent IPC API | Fleet agents can't coordinate |
| Scheduling + webhooks | Can't automate recurring work or notify on completion |
| Dashboard frontend | No UI — everything is curl |
| Multi-tenancy + auth | Single user, no auth |
| Hosted service | Users must self-host |

---

## The final goal

A corporate manager at a 200-person company opens `app.cloveos.com`:

```
┌─────────────────────────────────────────────────────────────┐
│  CLOVE — Department Automation                               │
│                                                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ Sales Intel   │  │ Support Bot  │  │ Code Reviewer │       │
│  │              │  │              │  │              │       │
│  │ Researches   │  │ Triages      │  │ Reviews PRs  │       │
│  │ competitors  │  │ tickets from │  │ for security │       │
│  │ weekly       │  │ Slack 24/7   │  │ + quality    │       │
│  │              │  │              │  │              │       │
│  │ RunEngine    │  │ OpenClaw+    │  │ RunEngine    │       │
│  │              │  │ Slack module │  │              │       │
│  │ Budget: $20/w│  │ Budget: $50/w│  │ Budget: $30/w│       │
│  │              │  │              │  │              │       │
│  │ ● Running    │  │ ● Running    │  │ ○ Mon 9am    │       │
│  │ $4.20/$20    │  │ $12.30/$50   │  │ Next run: 3d │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│                                                               │
│  This week: $16.50 spent · 847 tasks · 0 failures            │
│  PII blocked: 23 instances · Audit: 3,241 entries            │
│                                                               │
│  [+ Add Agent]  [View Audit]  [Export Report]                │
└─────────────────────────────────────────────────────────────┘
```

Notice: the Support Bot uses OpenClaw (needs Slack channel). Sales Intel and Code Reviewer use RunEngine directly. The manager doesn't know or care about this distinction. They just see agents.

---

## Implementation phases

### Phase 1: macOS Sandbox (NOW)

**Problem:** On macOS, spawned processes have full system access.

**Solution:** `sandbox-exec` (Apple Seatbelt) profiles — kernel-enforced, no root needed.

**What to build:**
- `libs/sandbox/src/macos/sandbox_exec.cpp` — generate Seatbelt profile string, apply via `sandbox_init()` before `execvp()` in child process
- Update macOS `#else` branch in `sandbox.cpp` to call it

Profile restricts:
- File: deny all except allowed paths (configurable)
- Network: deny all except allowed domains (configurable)
- Process: deny fork-bomb (max PIDs)
- Allow: system libs, Node.js runtime, temp dirs

**Scope:** ~200 LOC C++.

---

### Phase 2: OpenAI-Compatible LLM Endpoint

**Problem:** OpenClaw and every other AI tool speaks OpenAI chat completions format. Our `/api/think` uses a custom format.

**Solution:** `POST /api/v1/chat/completions` — accepts OpenAI format, routes through kernel pipeline (PII filter → budget check → inference gateway → OpenRouter), returns OpenAI format.

```
Any AI tool → POST /api/v1/chat/completions → Kernel → OpenRouter → Response
                                                ├── PII filtered
                                                ├── Cost tracked
                                                ├── Budget enforced
                                                └── Audit logged
```

**Why it's Phase 2:** This unlocks Phase 3 (OpenClaw spawn) because OpenClaw needs an OpenAI-compatible endpoint to route its LLM calls through. It also unlocks every other AI tool (Cursor, LangChain, etc.) using CLOVE as a governed LLM proxy.

**Scope:** ~150 LOC C++ in api_server.cpp.

---

### Phase 3: OpenClaw Spawn Endpoint

**Problem:** Plugin starts the kernel but doesn't spawn actual OpenClaw instances.

**Solution:** `POST /api/openclaw/spawn` — kernel spawns `openclaw gateway --headless` inside a sandbox.

**How it works:**

```
1. Manager creates agent with channel: "slack"
2. Dashboard calls POST /api/openclaw/spawn
3. Kernel:
   a. Creates temp dir with SOUL.md + openclaw.json
   b. openclaw.json points LLM at localhost:8080/api/v1 (Phase 2 endpoint)
   c. SandboxManager.create_sandbox() + Sandbox.start("openclaw", ["gateway", "--headless"])
   d. macOS: sandbox-exec restricts file/network (Phase 1)
   e. Sets budget via permissions store
   f. OpenClaw starts, connects to Slack, routes all LLM through kernel
4. Agent appears on dashboard, cost tracked, audit logged
```

**What to build:**
- `libs/api/src/openclaw_manager.cpp` + `.hpp` — instance lifecycle
- API routes: `spawn`, `fleet`, `status`, `stop`, `logs`
- Config writer: generates `openclaw.json` per instance
- SOUL writer: generates `SOUL.md` from agent description

**Scope:** ~500 LOC C++.

---

### Phase 4: Agent-to-Agent IPC API

**Problem:** Fleet agents work independently. The sales researcher can't pass findings to the report writer.

**Solution:** Expose kernel IPC (SYS_SEND/SYS_RECV) and shared memory (SYS_MEM_SHARE) via REST:

```
POST /api/agents/:id/message       — send message to an agent
GET  /api/agents/:id/messages      — read agent's inbox
POST /api/broadcast                — message all agents
POST /api/memory/:id/share/:agent  — share memory block with agent
```

**Coordination patterns:**

Pipeline:
```
Researcher → findings → Shared Memory → Writer → draft → Reviewer → approved
```

Delegation:
```
Coordinator (RunEngine) → delegates tasks → Workers (OpenClaw/RunEngine)
                        ← receives results ←
```

**Scope:** ~150 LOC C++ (4 routes wrapping existing syscalls).

---

### Phase 5: Scheduling + Webhooks

**Problem:** Manager wants "every Monday at 9am" and "notify me on Slack when done."

**Solution:**

Scheduling:
```json
POST /api/schedules
{
  "name": "weekly-competitor-report",
  "cron": "0 9 * * MON",
  "run": { "goal": "Research our top 5 competitors", "budget": 5.00, "agents": 3 }
}
```

Webhooks:
```json
POST /api/webhooks
{
  "url": "https://hooks.slack.com/services/...",
  "events": ["run_complete", "budget_exceeded", "pii_detected", "agent_error"]
}
```

**Scope:** ~300 LOC C++ (cron parser + scheduler thread + webhook dispatcher).

---

### Phase 6: Dashboard Frontend

**Problem:** Everything is curl. The manager needs a UI.

**Solution:** React/Next.js frontend. The kernel already has every API endpoint needed (51+). Pure UI work.

Pages:
1. **Home** — active agents, total cost, recent runs, live event stream
2. **New Agent** — form: name, description, tools, budget, schedule, channel (if OpenClaw)
3. **Agent Detail** — live logs, cost graph, audit, pause/resume/kill
4. **Fleet** — create fleet, watch all agents work in parallel
5. **Audit** — searchable log, filter by agent/category/time, JSONL export
6. **Cost** — graph over time, per-agent breakdown, budget alerts
7. **Settings** — LLM provider config, global budget, PII mode, webhooks

**Scope:** ~3,000 LOC React/TypeScript. Existing website repo at `/Users/anixd/Documents/clove-website/`.

---

### Phase 7: Multi-Tenancy + Auth

**Problem:** One kernel, one user.

**Solution:**
- Bearer token auth on all API routes (kernel already has `--api-key` flag)
- Kernel Worlds = tenant isolation (already built: SYS_WORLD_*)
- Each user → one World → isolated agents, state, memory, artifacts
- JWT auth for dashboard

**Scope:** ~400 LOC C++ (middleware + world-per-user) + frontend auth flow.

---

### Phase 8: Hosted Service

**Problem:** Users must self-host.

**Solution:** `api.cloveos.com` — hosted kernel. API key + URL.

```
Free:       10 runs/day, 1 agent, $0.50/run cap
Pro $29:    1000 runs/day, 10 agents, $50/month cap
Team $99:   Unlimited, 50 agents, audit export, webhooks
Enterprise: Custom, on-prem, SSO, compliance package
```

**Scope:** Infrastructure (VPS + Nginx + Stripe + landing page).

---

## Build priority

| # | What | Effort | Builds on | Opens up |
|---|------|--------|-----------|----------|
| **1** | macOS sandbox-exec | 1 day | — | Real sandboxing for Phase 3 |
| **2** | OpenAI-compatible endpoint | 0.5 day | — | Phase 3 + any AI tool as LLM proxy |
| **3** | OpenClaw spawn endpoint | 2 days | #1, #2 | Real OpenClaw inside CLOVE |
| **4** | IPC API routes | 0.5 day | — | Fleet coordination |
| **5** | Scheduling + webhooks | 2 days | — | Recurring automation |
| **6** | Dashboard frontend | 5 days | — | Product surface, non-technical users |
| **7** | Multi-tenancy | 2 days | #6 | Multiple users |
| **8** | Hosted service | 3 days | #6, #7 | Revenue |

**Critical path:** #1 → #2 → #3 (sandbox → LLM proxy → OpenClaw spawn)

**Revenue path:** #6 → #7 → #8 (dashboard → auth → hosted)

**Both paths are independent.** Build #1-#3 for the OpenClaw integration. Build #6 for the product surface. They merge at Phase 7.

---

## The corporate manager's journey

### Week 1: Discovery
Manager hears about CLOVE. Opens the dashboard. Types: "Research our competitor Acme Corp and write a one-page summary." Agent runs for 30 seconds. Cost: $0.02. Report is good. Thinks "this is useful."

### Week 2: Team trial
Creates 3 department agents:
- **Sales Intel** — researches competitors weekly ($4.50/week)
- **Support Triage** — monitors Slack, triages tickets, drafts replies ($12/week, uses OpenClaw Slack module)
- **Meeting Prep** — compiles board briefing doc before each meeting ($2/week)

### Month 2: Department rollout
8 agents across departments:
- Contract Reviewer, Code Reviewer, Content Writer, Data Analyst, Compliance Monitor
- Some use RunEngine (research, analysis, writing)
- Some use OpenClaw module (Slack, email, calendar integration)
- Total: ~$40/week, saving 60+ hours of human work

### Month 6: Enterprise
IT deploys CLOVE on-prem. 50 agents, 8 departments. Full audit trail for EU AI Act. PII filtering on every call. Cost: $500/month. Saving: $50K/month in human hours.

---

## Numbers to hit

| Metric | Target | Why |
|--------|--------|-----|
| First 10 users | 2 weeks after dashboard ships | PMF validation |
| First paying customer | 4 weeks after hosted service | Revenue proof |
| 100 users | 3 months | OpenClaw plugin drives organic growth |
| $1K MRR | 3 months | 35 Pro or 10 Team users |
| $10K MRR | 6 months | Enterprise pilot |
| EU AI Act compliance package | Before Aug 2, 2026 | Legal deadline = enterprise buying trigger |

---

## What makes this defensible

1. **The kernel.** 16,800 LOC C++, 86 syscalls, 165 tests. Can't replicate in Python.
2. **Module architecture.** OpenClaw is one module. Tomorrow it's MCP, A2A, LangChain, CrewAI. The kernel runs them all.
3. **Governance at kernel level.** Budgets, PII, audit, replay — not app-level middleware, kernel-level enforcement. Can't bypass it.
4. **Performance moat.** 3MB/agent, 27ms startup. Physics, not software.
5. **The LLM proxy play.** Any AI tool points at CLOVE → instant governance. Cursor, Continue, LangChain, LlamaIndex — all get cost tracking and PII filtering for free.
6. **The hosted API.** Once tools point at `api.cloveos.com`, switching cost is high.
