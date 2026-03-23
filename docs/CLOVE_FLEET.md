# Clove Fleet CLI

> `clove fleet up` — docker-compose for AI agents

## What It Is

A CLI tool that lets you define agent fleets in YAML and run them on CLOVE. You declare agents, their roles, budgets, permissions, shared memory, and coordination — then one command boots the whole fleet.

```bash
clove fleet up --file fleet.yaml
```

Every agent runs as a sandboxed CLOVE process with kernel-enforced budgets, priority scheduling, shared memory blocks, and a full audit trail.

---

## Why It Exists

Running multiple AI agents today requires duct-taping together:
- LangGraph or CrewAI for orchestration
- Redis or a database for shared state
- Manual cost tracking (or surprise $47K bills)
- No isolation between agents
- No audit trail
- No execution replay for debugging

Clove Fleet replaces all of that with a single YAML file and one command.

---

## Fleet YAML Spec

```yaml
fleet:
  name: code-review-fleet
  description: "Parallel code review with security, performance, and style agents"

  # Fleet-wide budget cap (all agents combined)
  budget:
    max_cost_usd: 5.0
    max_time_ms: 300000  # 5 minutes

  # Default model for all agents (overridable per agent)
  model: anthropic/claude-sonnet-4

  # Default permissions (overridable per agent)
  defaults:
    permissions:
      can_think: true
      can_read: true
      can_write: false
      can_exec: false
      can_http: false

agents:
  # ── Security reviewer ──────────────────────────
  security:
    script: agents/security_reviewer.py
    priority: critical
    budget:
      max_tokens: 100000
      max_cost_usd: 2.0
      max_steps: 200
    permissions:
      can_http: true  # needs to check CVE databases
      allowed_domains: ["cve.mitre.org", "nvd.nist.gov"]
    memory:
      - name: findings
        type: core
        access: shared_read
        content: ""
      - name: cve_cache
        type: recall
        access: private

  # ── Performance reviewer ───────────────────────
  performance:
    script: agents/perf_reviewer.py
    priority: normal
    budget:
      max_tokens: 80000
      max_cost_usd: 1.5
      max_steps: 150
    memory:
      - name: findings
        type: core
        access: shared_read

  # ── Style reviewer ────────────────────────────
  style:
    script: agents/style_reviewer.py
    priority: low
    model: google/gemini-2.0-flash-001  # cheaper model for style
    budget:
      max_tokens: 50000
      max_cost_usd: 0.5
      max_steps: 100
    memory:
      - name: findings
        type: core
        access: shared_read

  # ── Synthesizer (runs after all reviewers) ─────
  synthesizer:
    script: agents/synthesizer.py
    priority: high
    depends_on: [security, performance, style]
    budget:
      max_tokens: 50000
      max_cost_usd: 1.0
      max_steps: 50
    memory:
      - name: final_review
        type: core
        access: shared_read

# ── Chain (optional) ──────────────────────────────
chain:
  name: "review-{{timestamp}}"
  description: "Code review run"
```

---

## CLI Commands

```bash
# Boot a fleet from YAML
clove fleet up --file fleet.yaml

# Boot with file/directory watch (re-runs on changes)
clove fleet up --file fleet.yaml --watch ./src

# Check fleet status
clove fleet status

# View agent budgets and usage
clove fleet budget

# View live logs from all agents
clove fleet logs

# View logs from one agent
clove fleet logs security

# Stop the fleet
clove fleet down

# Replay a past fleet run
clove fleet replay --run-id 20260324_143022

# List past runs
clove fleet history

# Export audit trail
clove fleet audit --format jsonl --output audit.jsonl

# Dry run (validate YAML without starting agents)
clove fleet check --file fleet.yaml
```

---

## How It Works Under the Hood

```
clove fleet up --file fleet.yaml
       │
       ▼
  Parse fleet.yaml
       │
       ▼
  Start CLOVE kernel (if not running)
  └─ clove_kernel --no-sandbox --openrouter --api --db fleet.db
       │
       ▼
  Create chain for this run
  └─ SYS_CHAIN_CREATE("review-20260324_143022")
       │
       ▼
  For each agent (respecting depends_on order):
  │
  ├─ Set budget
  │  └─ SYS_SET_BUDGET(max_tokens, max_cost_usd, max_steps, max_time_ms)
  │
  ├─ Set priority
  │  └─ SYS_SET_PRIORITY(critical|high|normal|low|idle)
  │
  ├─ Create memory blocks
  │  └─ SYS_MEM_CREATE(name, type, access, content)
  │  └─ SYS_MEM_SHARE(block_id, target_agent_id) for shared blocks
  │
  ├─ Set permissions
  │  └─ SYS_SET_PERMS(can_think, can_read, can_http, allowed_domains...)
  │
  ├─ Start recording
  │  └─ SYS_RECORD_START()
  │
  └─ Spawn agent
     └─ SYS_SPAWN(name, script)
       │
       ▼
  Agents run in parallel (unless depends_on specified)
  │
  ├─ Each agent connects via Python SDK
  ├─ Each agent reads/writes its memory blocks
  ├─ Each agent creates artifacts in the chain
  ├─ Each agent's LLM calls go through budget enforcement
  ├─ Scheduler manages priority ordering
  └─ Event bus notifies when agents complete
       │
       ▼
  depends_on agents wait for dependencies via events
  └─ subscribe(AGENT_EXITED) → check if dependency finished
       │
       ▼
  All agents done → fleet complete
  │
  ├─ Stop recording
  ├─ Print summary (costs, tokens, artifacts created)
  ├─ Final chain available for review
  └─ Audit trail in fleet.db
```

---

## Agent Script Template

Each agent is a Python script using the CLOVE SDK:

```python
#!/usr/bin/env python3
"""Security reviewer agent for Clove Fleet."""

from clove_sdk.client import CloveClient
import sys, os

def main():
    socket = os.environ.get("CLOVE_SOCKET", "/tmp/clove.sock")
    agent_id = int(os.environ.get("CLOVE_AGENT_ID", "0"))
    chain_id = os.environ.get("CLOVE_CHAIN_ID", "")

    with CloveClient(socket, agent_id=agent_id) as client:
        # Read the code to review (passed via memory block or argument)
        code = client.mem_read(name="source_code")

        # Think about security issues
        result = client.think_with_context(
            prompt=f"""Review this code for security vulnerabilities:

{code['block']['content']}

Focus on: injection, auth bypass, data exposure, dependency risks.
Return a structured list of findings with severity.""",
            chain_id=chain_id
        )

        # Write findings to shared memory block
        client.mem_write(
            id=os.environ.get("CLOVE_MEM_FINDINGS"),
            content=result["content"]
        )

        # Create artifact in chain for audit trail
        client.doc_create(
            type="analysis",
            title="Security Review",
            content=result["content"],
            chain_id=chain_id,
            metadata={
                "tokens": result.get("tokens", 0),
                "cost_usd": result.get("cost_usd", 0),
                "model": result.get("model", "")
            }
        )

        # Transition to final
        art_id = # ... from doc_create response
        client.doc_update(art_id, state="in_review")
        client.doc_update(art_id, state="approved")
        client.doc_update(art_id, state="final")

if __name__ == "__main__":
    main()
```

Environment variables injected by fleet CLI:
- `CLOVE_SOCKET` — kernel socket path
- `CLOVE_AGENT_ID` — this agent's ID
- `CLOVE_CHAIN_ID` — run's chain ID
- `CLOVE_FLEET_NAME` — fleet name
- `CLOVE_MEM_*` — memory block IDs for configured blocks

---

## Fleet Templates

Pre-built fleet configurations for common workflows:

### `code-review` — Parallel code review
3 reviewers (security, performance, style) + 1 synthesizer. Reviews a PR and produces a unified report.

### `research` — Deep research station
N parallel researchers + synthesizer + editor. Takes a question, produces a research report with full provenance chain.

### `docs-sync` — Documentation updater
Watches source code, detects changes, updates docs. Coordinator agent + N writer agents.

### `test-gen` — Test generator
Reads source code, generates test cases. Analyzer agent + N test-writer agents + validator agent.

### `monitor` — Continuous monitoring
Long-running agents that watch for events (log errors, metric spikes, security alerts) and coordinate responses.

```bash
# Use a template
clove fleet up --template code-review --input ./src --output review.md

# List available templates
clove fleet templates
```

---

## What CLOVE Features Fleet Uses

| Feature | How Fleet Uses It |
|---|---|
| SYS_SPAWN / SYS_KILL | Start and stop agent processes |
| SYS_SET_BUDGET / GET_BUDGET | Per-agent cost/token/step/time limits |
| SYS_SET_PRIORITY / GET_PRIORITY | Critical agents run first |
| SYS_MEM_CREATE / WRITE / SHARE | Agents share findings via named blocks |
| SYS_DOC_CREATE / UPDATE | Every agent writes artifacts to the chain |
| SYS_CHAIN_CREATE | Each run gets its own provenance chain |
| SYS_CONTEXT_ASSEMBLE | Synthesizer reads all context automatically |
| SYS_THINK (auto_context) | LLM calls include relevant chain context |
| SYS_SEND / RECV | Agent-to-agent notifications |
| SYS_SUBSCRIBE / POLL_EVENTS | depends_on waits for AGENT_EXITED events |
| SYS_RECORD_START / STOP | Full execution recording per run |
| SYS_REPLAY_START | Debug any past run |
| SYS_GET_AUDIT_LOG | Export compliance trail |
| SYS_SET_PERMS | Per-agent capability restrictions |
| SYS_GET_BUDGET | Fleet-wide cost reporting |

This is not theater — every syscall above is implemented and working.

---

## Implementation Plan

### Phase 1: Core CLI (1-2 weeks)
- YAML parser (fleet spec → kernel config)
- `clove fleet up` (parse, boot kernel, spawn agents)
- `clove fleet down` (stop all agents, shut down kernel)
- `clove fleet status` (query agent states + budgets)
- `clove fleet logs` (tail agent output)
- Environment variable injection for agent scripts

### Phase 2: Coordination (1 week)
- `depends_on` ordering (topological sort + event-based waiting)
- Fleet-wide budget aggregation
- `--watch` mode (re-run on file changes)

### Phase 3: Templates + Polish (1 week)
- `code-review` template with agent scripts
- `research` template
- `clove fleet replay` (load past run, replay syscalls)
- `clove fleet audit` (export JSONL)
- `clove fleet history` (list past runs)
- `clove fleet check` (dry-run validation)

### Phase 4: Distribution
- `brew install clove` (Homebrew formula)
- PyPI package for SDK
- Template marketplace (community-contributed fleet YAMLs)

---

## Positioning

**For developers:** "docker-compose for AI agents. Define your fleet in YAML, set budgets, let them coordinate. One command."

**For teams:** "Run your AI agent pipeline with per-agent cost caps, priority scheduling, and a full audit trail. No surprise bills. No black boxes."

**vs LangGraph/CrewAI:** "They orchestrate in Python. We orchestrate at the kernel level — real isolation, real budgets, real replay."

**vs OpenShell:** "They sandbox one agent. We schedule and coordinate hundreds."
