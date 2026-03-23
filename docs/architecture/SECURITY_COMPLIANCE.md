# CLOVE v2 — Security & Compliance Architecture

> Status: Design doc | Date: 2026-03-23
> Based on: OWASP ASI Top 10 (2026), EU AI Act, NIST CAISI (Feb 2026),
> Google Conseca (HotOS 2025), OpenAgentSafety (ICLR 2026)

## Threat Model

### OWASP ASI Top 10 Mapping

| # | Threat | Current CLOVE Status | Gap |
|---|--------|---------------------|-----|
| ASI01 | Agent Goal Hijacking | Partial — audit logging exists | Need: input validation at syscall layer |
| ASI02 | Tool Misuse & Exploitation | Good — MCP ACL, command blocklists | Need: anomaly detection on tool patterns |
| ASI03 | Identity & Privilege Abuse | Good — per-agent permissions, RBAC | Need: JIT elevation, auto-expiry |
| ASI04 | Supply Chain Vulnerabilities | Good — sandboxing (namespaces, seccomp) | Need: dependency scanning |
| ASI05 | Unexpected Code Execution | Good — EXEC gating, command blocklists | Need: output validation |
| ASI06 | Memory & Context Poisoning | Partial — scoped state store | Need: input sanitization on state writes |
| ASI07 | Excessive Agency | Partial — cost caps exist | **Need: four-budget enforcement (P0)** |
| ASI08 | Inadequate Monitoring | Good — audit logger, OTel, replay | Need: real-time alerting |
| ASI09 | Inadequate Failure Handling | Good — restart policies, escalation | Need: dead-letter queue for failed tasks |
| ASI10 | Rogue Agents | Partial — cost auto-kill | Need: behavioral anomaly detection |

### Defense-in-Depth Layers

```
Layer 1: Network Boundary
  - Egress proxy with domain allowlists
  - SSRF guard (blocks private IPs)
  - TLS on all external connections

Layer 2: Kernel Syscall Layer
  - Permission check on every syscall
  - Budget enforcement (token, step, time, cost)
  - Audit logging of every operation
  - PII scan on input/output

Layer 3: Process Isolation
  - Linux namespaces (PID, NET, MNT, UTS)
  - cgroups v2 (memory, CPU, PIDs)
  - Landlock filesystem restrictions
  - seccomp BPF (27 blocked syscalls)

Layer 4: Governance
  - Inference gateway (model allowlists, cost limits)
  - Policy hot-reload (no restart needed)
  - Execution replay (full audit trail)
  - Policy recommendations (learn from denials)
```

## EU AI Act Compliance

### Timeline
- **Feb 2, 2025:** Prohibited practices enforceable
- **Aug 2, 2025:** GPAI model rules applicable
- **Aug 2, 2026:** High-risk AI systems full compliance ← TARGET

### Requirements → CLOVE Features

| EU AI Act Requirement | CLOVE Implementation |
|-----------------------|---------------------|
| Automatic logging for post-hoc review | AuditLogger — syscall-level logging |
| Logs stored ≥6 months | SQLite persistence + configurable retention |
| Audit trail linking output → source data | Artifact chain with parent_ids (provenance DAG) |
| Model version tracking | Metadata on every SYS_THINK (model field) |
| Risk assessment documentation | Execution replay provides operational evidence |
| Human oversight capability | Agent PAUSE/RESUME, policy hot-reload, kill switch |
| Transparency (explain decisions) | Chain of docs IS the explanation |

### Compliance Deliverables

1. **Control Catalog** — list of all security controls (syscall permissions, budget enforcement, isolation)
2. **Compliance Matrix** — mapping each EU AI Act article to CLOVE feature
3. **Risk Register** — threats (OWASP ASI), mitigations (CLOVE features), residual risks

### Implementation Needed

```
1. Audit log retention policy (configurable, default 6 months)
2. Audit log export (JSONL format for regulators)
3. Model version tracking in SYS_THINK response metadata
4. Compliance report generator (maps features → EU AI Act articles)
5. Immutable log guarantee (append-only SQLite table, no DELETE)
```

## Four-Budget Enforcement

### Budget Types

| Budget | Unit | Default | Enforcement Point |
|--------|------|---------|-------------------|
| Token | tokens | 0 (unlimited) | Before SYS_THINK (estimate) + after (actual) |
| Step | syscall count | 0 (unlimited) | Before every syscall dispatch |
| Time | milliseconds | 0 (unlimited) | Scheduler tick (100ms interval) |
| Cost | USD | 0 (unlimited) | After SYS_THINK (from LLM response) |

### Enforcement Behavior

```
if budget_exceeded:
    1. Log to audit (category: RESOURCE, event: BUDGET_EXCEEDED)
    2. Emit event (type: AGENT_BUDGET_EXCEEDED)
    3. Set agent state to BUDGET_EXCEEDED
    4. Return error response to agent
    5. If kill_on_budget_exceeded: send SIGTERM to agent process
```

### Syscalls

```
SYS_SET_BUDGET = 0xF1
  Payload: {agent_id?, max_tokens?, max_steps?, max_time_ms?, max_cost_usd?}
  Response: {success, budget}

SYS_GET_BUDGET = 0xF2
  Payload: {agent_id?}
  Response: {success, budget: {max_tokens, used_tokens, max_steps, steps_taken, ...}}
```

## Context-Aware Permissions (Future)

From Google's Conseca paper (HotOS 2025): static allow/deny policies cannot handle
generalist agents. The same action may be appropriate or not depending on context.

### Current: Static Permissions

```json
{
  "can_exec": false,
  "can_http": true,
  "allowed_domains": ["api.example.com"]
}
```

### Future: Context-Aware Policies

```json
{
  "rules": [
    {
      "action": "SYS_HTTP",
      "condition": "chain.metadata.type == 'research'",
      "domains": ["*.arxiv.org", "*.scholar.google.com"],
      "effect": "allow"
    },
    {
      "action": "SYS_HTTP",
      "condition": "chain.metadata.type == 'customer_service'",
      "domains": ["api.internal.com"],
      "effect": "allow"
    },
    {
      "action": "SYS_EXEC",
      "condition": "agent.budget.cost_usd < 1.0",
      "effect": "deny",
      "reason": "exec requires minimum $1 budget headroom"
    }
  ]
}
```

Evaluation at syscall time: check rules in order, first match wins. This is the
direction for CLOVE v3 but can be incrementally added to the existing permission system.

## NIST CAISI Alignment

NIST's AI Agent Standards Initiative (Feb 2026) focuses on:

| NIST Focus Area | CLOVE Feature |
|-----------------|---------------|
| Agent identity/authentication | Per-agent ID, permissions, registered names |
| Least privilege | Permission levels (UNRESTRICTED→MINIMAL), capability flags |
| JIT access | Future: temporary permission elevation with auto-expiry |
| Task-scoped privileges | Future: permissions tied to chain/task context |
| Action-level approvals | Future: human-in-the-loop syscall gating |

## Real-World Incident Defenses

| Incident | CLOVE Defense |
|----------|---------------|
| $47K infinite agent loop | Step budget (max 1000 syscalls) + time budget (max 1 hour) |
| $47K retry storm (2.3M API calls) | Step budget + cost budget ($50 cap) |
| Slack AI data exfiltration | Scoped state store + PII redaction on output |
| Agent code execution exploit | Sandboxing + EXEC command blocklist + seccomp |
| Cross-tenant data leak | World isolation (separate StateStore per world) |

## Implementation Priority

1. **Four-budget enforcement** — the single most impactful security feature
2. **Audit log improvements** — retention policy, export, immutability
3. **OWASP ASI mapping doc** — marketing + compliance positioning
4. **Budget syscalls** — SYS_SET_BUDGET, SYS_GET_BUDGET
5. **EU AI Act compliance report** — automated feature→article mapping
6. **Context-aware permissions** — rule-based policy evaluation
7. **NIST alignment tracking** — document alignment, prepare for certification
