# CLOVE v2 — Research Synthesis

> Compiled: 2026-03-23 | Sources: ~50 papers across 3 domains | Period: 2024-2026

## Purpose

This document synthesizes findings from academic papers, industry reports, and incident analyses
to guide CLOVE v2 development. Each finding includes the source, key insight, and how it
applies to CLOVE.

---

## 1. Agent OS Architecture

The academic community is independently converging on the "agents need an OS" thesis.

### Key Papers

| Paper | Venue | Core Idea | CLOVE Relevance |
|-------|-------|-----------|-----------------|
| **AIOS** (Mei et al.) | COLM 2025 | Full agent OS with scheduling, memory, tool mgmt | Validates entire CLOVE architecture |
| **Cerebrum** (AIOS SDK) | NAACL 2025 | Developer SDK for AIOS agent creation | Model for CLOVE Python SDK |
| **Agent-OS Blueprint** | Preprint 2025 | 5-layer architecture, latency classes (HRT/SRT/DT), Agent Contracts | Formalize CLOVE's architecture in similar terms |
| **MemGPT/Letta** | 2023→2026 | LLM as OS kernel managing virtual memory | Closest analogue to CLOVE's context layer |
| **MemoryOS** | EMNLP 2025 Oral | 3-tier hierarchical memory: short/mid/long | Direct template for CLOVE memory tiers |
| **MemOS** | July 2025 | Memory as first-class OS resource with scheduling + permissions | Validates memory access control in CLOVE |

### Architectural Insight

The Agent-OS Blueprint defines 5 layers:
1. **Hardware/Infra** — GPUs, network
2. **Kernel** — scheduling, IPC, memory management
3. **Services** — tool registry, model registry, storage
4. **Agent Runtime** — lifecycle, context, planning
5. **Application** — user-facing agents

CLOVE maps to layers 2-4. The Blueprint also introduces **latency classes**:
- **HRT (Hard Real-Time):** IPC, syscall dispatch — must be <1ms
- **SRT (Soft Real-Time):** Scheduling, context assembly — should be <100ms
- **DT (Deferred-Time):** Persistence, analytics — can be async

**Action:** Classify all CLOVE operations into HRT/SRT/DT and optimize accordingly.

---

## 2. Scheduling

CLOVE's current LlmQueue is a simple 8-worker thread pool. The research shows
this is a major bottleneck.

### Key Papers

| Paper | Improvement | Technique |
|-------|-------------|-----------|
| **Autellix** | 4-15x throughput | Program-aware scheduling — batch similar agent requests |
| **Astraea** | 25.5% JCT reduction | State-aware: classify agents as waiting-LLM/waiting-tool/computing |
| **Helium** | 1.56x speedup | Database query-plan metaphor for agent workflows |
| **Cortex** | Workflow-aware | Per-stage resource isolation in multi-step workflows |
| **Continuum** | KV cache savings | TTL-based KV cache management for multi-turn agents |
| **Throughput-Optimal** | Theoretical proof | Work-conserving scheduling insufficient for agent networks |

### Key Insight: State-Aware Scheduling

From Astraea: agents have 3 states that require different scheduling:
1. **Waiting-for-LLM** — blocked on inference. Schedule another agent on this GPU.
2. **Waiting-for-Tool** — blocked on HTTP/exec. Don't hold LLM resources.
3. **Computing** — actively reasoning. Don't preempt.

Classifying agent I/O state and scheduling accordingly yields 25.5% improvement.

### Key Insight: KV Cache Awareness

From Continuum: when an agent pauses between turns, its KV cache (model attention state)
can be preserved. Scheduling agents whose cache is still warm avoids recomputation.

**Action:** Replace CLOVE's simple worker pool with a state-aware scheduler.

---

## 3. Context & Memory Management

The single most impactful area for improvement. Multiple independent papers confirm
that naive context management causes 30%+ accuracy drops.

### The Problem: Lost-in-the-Middle

Stanford/TACL 2024 + Chroma 2025: every frontier LLM suffers accuracy degradation
when relevant information is placed in the middle of long contexts. Three compounding
mechanisms:
- **Positional bias** — models attend more to beginning and end
- **Attention dilution** — quadratic attention spreads across more tokens
- **Distractor interference** — irrelevant context actively hurts accuracy

**Implication:** More context is NOT always better. Compression and placement matter.

### Hierarchical Memory (consensus across 5+ papers)

Every memory paper converges on a 3-tier model:

```
Tier 1: Working Memory (in-context window)
  - What the agent is currently thinking about
  - Size: 4K-128K tokens
  - Latency: 0ms (already in prompt)

Tier 2: Session Memory (in-memory store)
  - Recent history, current task state, other agents' outputs
  - Size: unlimited (RAM-bounded)
  - Latency: <1ms (fetch from StateStore)

Tier 3: Persistent Memory (SQLite/knowledge graph)
  - Past task trajectories, learned strategies, long-term facts
  - Size: unlimited (disk-bounded)
  - Latency: 1-10ms (DB query)
```

**Promotion/demotion policies** (from MemoryOS):
- Tier 3 → Tier 2: On relevance match (semantic search or key lookup)
- Tier 2 → Tier 1: On context assembly (kernel selects what goes in prompt)
- Tier 1 → Tier 2: After LLM call completes (working memory flushed to session)
- Tier 2 → Tier 3: After task completes (session summarized and persisted)

### Agent Self-Editing Memory (Letta Memory Blocks)

From Letta/MemGPT: agents should have explicit tools to manage their own memory:
- `memory_read(block_name)` — read a named memory block
- `memory_write(block_name, content)` — overwrite a block
- `memory_append(block_name, content)` — append to a block
- `archival_search(query)` — search persistent memory

Memory blocks are:
- **Named** — "persona", "human", "task_state", "findings"
- **Typed** — system (pinned), core (always in context), recall (on-demand)
- **Shareable** — multiple agents can reference the same block (read or read/write)
- **Persistent** — backed by database, survive restarts

### Context Compression Techniques

| Technique | Paper | Token Savings | Accuracy Impact |
|-----------|-------|---------------|-----------------|
| **Observation masking** | JetBrains/NeurIPS 2025 | ~50% | None |
| **Subgoal compression** | HiAgent/ACL 2025 | 35% | +100% success rate |
| **Token classification** | LLMLingua-2/ACL 2024 | 14x | Minimal |
| **Attention-guided pruning** | AttentionRAG 2025 | 6.3x | +10% vs baselines |
| **Adaptive compression** | ACON 2025 | 26-54% | <5% accuracy loss |

**Recommended pipeline for CLOVE:**
1. **Observation masking** (simple, free) — hide verbose tool outputs
2. **Subgoal compression** (medium) — summarize completed subtasks
3. **Position-aware placement** (simple, free) — critical info at context boundaries
4. **Optional LLM summarization** (expensive) — only when masking insufficient

### Case-Based Experience Memory

From Memento (NeurIPS 2025): store successful task trajectories as "cases."
When a similar task arrives, retrieve the closest case and use it to guide planning.
Achieves 87.88% on GAIA benchmark without any model fine-tuning.

**Action:** CLOVE should store completed chains as reusable templates.

---

## 4. Security & Governance

Enterprise buying trigger. EU AI Act deadline Aug 2, 2026.

### Threat Landscape: OWASP ASI Top 10 (Dec 2025)

| # | Threat | CLOVE Mitigation |
|---|--------|-----------------|
| ASI01 | Agent Goal Hijacking | Syscall-level audit + execution replay |
| ASI02 | Tool Misuse | Kernel-enforced tool permissions (MCP ACL) |
| ASI03 | Identity & Privilege Abuse | Per-agent permissions, zero-trust identity |
| ASI04 | Supply Chain Vulnerabilities | Sandboxing (namespaces, seccomp) |
| ASI05 | Unexpected Code Execution | EXEC syscall gating, command blocklists |
| ASI06 | Memory & Context Poisoning | Scoped state store, permission-gated memory |
| ASI07 | Excessive Agency | Four-budget enforcement (token/step/time/cost) |
| ASI08 | Inadequate Monitoring | Audit logger, execution replay, OTel export |
| ASI09 | Inadequate Failure Handling | Restart policies, escalation, dead-letter queues |
| ASI10 | Rogue Agents | Cost caps with auto-kill, policy hot-reload |

### Four-Budget Enforcement (industry consensus)

Every cost incident analysis recommends four hard limits:

1. **Token budget** — max tokens per agent per task (prevents prompt explosion)
2. **Step budget** — max tool calls / syscalls per task (prevents infinite loops)
3. **Time budget** — max wall-clock time per task (prevents stuck agents)
4. **Cost budget** — max USD per agent (prevents runaway spending)

All must be kernel-enforced. Agent cannot override its own budget.

### EU AI Act Requirements

- Automatic logging for post-hoc review (6-month retention minimum)
- Audit trails linking outputs to source data, model versions, prompts
- Operational evidence (not just documentation)
- Full compliance deadline: **August 2, 2026**
- Penalties: up to 35M EUR or 7% of global revenue

### NIST CAISI (Feb 2026)

New US standards initiative for agent identity, least privilege, JIT access,
and action-level approvals. Aligning with this early is a competitive advantage
for government and enterprise sales.

### Real-World Cost Incidents

| Incident | Date | Cost |
|----------|------|------|
| LangChain infinite agent loop | Nov 2025 | $47,000 |
| API retry storm (2.3M calls) | Feb 2026 | $47,000 |
| Average AI-related data breach | 2025 | $5,200,000 |

---

## 5. Multi-Agent Coordination

### Key Patterns from Research

**Blackboard pattern** (2025 papers, 13-57% improvement):
- Agents post to shared space, self-select tasks based on capability
- No central coordinator bottleneck
- Maps to: CLOVE event bus + artifact store

**MetaGPT document passing** (ICLR 2024):
- Agents communicate through structured documents, not chat
- Each document type has a schema; agents subscribe to types they consume
- Maps to: CLOVE artifact system with typed artifacts

**Anthropic's production system** (June 2025):
- Lead agent spawns 3-5 subagents in parallel
- Subagents store work externally, pass lightweight references back
- Multi-agent outperforms single-agent by 90.2%
- Maps to: CLOVE chain of docs with artifact references

**Chain of Agents** (Google, ICLR 2025):
- Sequential agents process different segments of long input
- Each receives previous agent's summary + its own segment
- Maps to: CLOVE chains with parent_ids forming a DAG

---

## 6. Consolidated Action Items

### P0 — Must Build (blocks production readiness)

1. Four-budget enforcement syscalls (token, step, time, cost)
2. Hierarchical memory (3-tier with promotion/demotion)
3. Agent memory blocks (named, typed, shareable, self-editable)
4. State-aware scheduler (replace simple worker pool)

### P1 — Should Build (significant competitive advantage)

5. Context compression pipeline (observation masking → subgoal compression)
6. Position-aware context assembly (critical info at boundaries)
7. EU AI Act compliance packaging (immutable logs, 6-month retention, export)
8. OWASP ASI mapping documentation
9. Context-aware permissions (dynamic policy evaluation)

### P2 — Nice to Have (research frontier)

10. Case-based experience memory (store/retrieve successful trajectories)
11. Temporal knowledge graph (handle evolving facts)
12. Attention-guided context pruning (model's own attention as signal)
13. Multi-model cascading (cheap model + expensive fallback)
14. KV cache-aware scheduling

---

## References

### Agent OS
- [AIOS](https://arxiv.org/abs/2403.16971) | [Cerebrum](https://arxiv.org/abs/2503.11444)
- [MemGPT](https://arxiv.org/abs/2310.08560) | [Letta](https://docs.letta.com)
- [MemoryOS](https://arxiv.org/abs/2506.06326) | [MemOS](https://arxiv.org/abs/2507.03724)
- [Agent-OS Blueprint](https://www.preprints.org/manuscript/202509.0077)

### Scheduling
- [Autellix](https://arxiv.org/abs/2502.13965) | [Astraea](https://arxiv.org/abs/2512.14142)
- [Helium](https://arxiv.org/abs/2603.16104) | [Cortex](https://arxiv.org/abs/2510.14126)
- [Continuum](https://arxiv.org/abs/2511.02230)

### Context & Memory
- [HiAgent](https://arxiv.org/abs/2408.09559) | [ACON](https://arxiv.org/abs/2510.00615)
- [LLMLingua-2](https://arxiv.org/abs/2403.12968) | [AttentionRAG](https://arxiv.org/abs/2503.10720)
- [Zep/Graphiti](https://arxiv.org/abs/2501.13956) | [A-MEM](https://arxiv.org/abs/2502.12110)
- [Memento](https://arxiv.org/abs/2508.16153) | [Mem0](https://arxiv.org/abs/2504.19413)
- [Lost-in-the-Middle](https://arxiv.org/pdf/2509.21361)
- [JetBrains Complexity Trap](https://blog.jetbrains.com/research/2025/12/efficient-context-management/)
- [Memory Survey](https://arxiv.org/abs/2512.13564)

### Security & Governance
- [OWASP ASI Top 10](https://genai.owasp.org/resource/owasp-top-10-for-agentic-applications-for-2026/)
- [NIST CAISI](https://www.nist.gov/caisi/ai-agent-standards-initiative)
- [Conseca (Google HotOS)](https://arxiv.org/abs/2501.17070)
- [EU AI Act Timeline](https://secureprivacy.ai/blog/eu-ai-act-2026-compliance)
- [OpenAgentSafety](https://arxiv.org/abs/2507.06134)
- [Zero-Trust Agent Identity](https://arxiv.org/abs/2505.19301)

### Multi-Agent
- [MetaGPT](https://arxiv.org/abs/2308.00352)
- [Anthropic Multi-Agent](https://www.anthropic.com/engineering/multi-agent-research-system)
- [Chain of Agents](https://openreview.net/forum?id=LuCLf4BJsr)
- [Blackboard Systems](https://arxiv.org/abs/2510.01285)
- [Collaborative Memory](https://arxiv.org/abs/2505.18279)
