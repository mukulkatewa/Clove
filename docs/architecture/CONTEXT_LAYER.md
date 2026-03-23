# CLOVE v2 — Context Layer Architecture

> Version: 2.0 | Date: 2026-03-23

## Overview

The Context Layer provides document/artifact management, chain-of-docs coordination,
hierarchical memory, and context assembly for LLM agent fleets. It sits between the
orchestration layer (IPC, events, state) and the LLM inference layer.

```
┌────────────────────────────────────────────────┐
│  Agent Process                                  │
│  ┌──────────────────────────────────────────┐  │
│  │  Working Memory (Tier 1)                  │  │
│  │  - Current prompt context                 │  │
│  │  - Assembled from Tiers 2+3               │  │
│  └──────────────────────────────────────────┘  │
│           ↕ SYS_THINK (auto_context)            │
├────────────────────────────────────────────────┤
│  CLOVE Kernel                                   │
│  ┌──────────────────────────────────────────┐  │
│  │  Session Memory (Tier 2) — in-memory      │  │
│  │  - ArtifactStore (current chain docs)     │  │
│  │  - Memory Blocks (agent self-edited)      │  │
│  │  - StateStore (shared KV)                 │  │
│  └──────────────────────────────────────────┘  │
│           ↕ write-through                       │
│  ┌──────────────────────────────────────────┐  │
│  │  Persistent Memory (Tier 3) — SQLite      │  │
│  │  - ArtifactStoreDb                        │  │
│  │  - StateStoreDb                           │  │
│  │  - AuditStore                             │  │
│  └──────────────────────────────────────────┘  │
└────────────────────────────────────────────────┘
```

---

## Current Implementation (v2.0)

### Artifacts

Typed documents with lifecycle state machines.

```cpp
enum class ArtifactType  : uint8_t { QUERY, RESEARCH, ANALYSIS, SYNTHESIS, REPORT, NOTE, PLAN };
enum class ArtifactState : uint8_t { DRAFT, IN_REVIEW, APPROVED, FINAL, ARCHIVED };

struct Artifact {
    std::string id;                      // "art_" + 12-char hex
    std::string chain_id;                // which chain this belongs to
    uint32_t author_agent_id;            // who created it
    ArtifactType type;
    ArtifactState state;                 // forward-only transitions
    std::string title;
    std::string content;                 // markdown
    std::vector<std::string> parent_ids; // DAG edges (provenance)
    nlohmann::json metadata;             // {tokens_used, cost_usd, model, ...}
    uint64_t created_at_ms;
    uint64_t updated_at_ms;
};
```

**State transitions** (forward-only, author-only):
```
DRAFT → IN_REVIEW → APPROVED → FINAL → ARCHIVED
```

### Chains

Ordered sequences of artifacts forming a DAG.

```cpp
struct Chain {
    std::string id;                        // "chain_" + 12-char hex
    std::string name;
    std::string description;
    uint32_t creator_agent_id;
    std::vector<std::string> artifact_ids; // ordered
    nlohmann::json metadata;
    uint64_t created_at_ms;
};
```

Chains support **forking** — create a new chain with artifacts up to a fork point.

### Context Assembly

The `ContextAssembler` builds agent prompts from chain artifacts:

```
L1 (Shared):  FINAL/APPROVED artifacts from the chain — visible to all agents
L2 (Chain):   Non-draft artifacts authored by this agent's lineage
L3 (Private): DRAFT artifacts authored by this agent only
```

Assembly respects a token budget (chars/4 heuristic) and reports truncation.

### Syscalls (9 opcodes: 0xE0–0xE8)

| Opcode | Name | Description |
|--------|------|-------------|
| 0xE0 | SYS_DOC_CREATE | Create artifact in a chain |
| 0xE1 | SYS_DOC_READ | Read artifact by ID |
| 0xE2 | SYS_DOC_UPDATE | Update content and/or state |
| 0xE3 | SYS_DOC_LIST | List with filters (chain, type, state, author) |
| 0xE4 | SYS_DOC_DELETE | Delete DRAFT artifact (author only) |
| 0xE5 | SYS_CHAIN_CREATE | Create new chain |
| 0xE6 | SYS_CHAIN_GET | Get chain + artifacts + DAG |
| 0xE7 | SYS_CHAIN_FORK | Fork chain at artifact |
| 0xE8 | SYS_CONTEXT_ASSEMBLE | Assemble context from chain |

### Context-Aware LLM Calls

`SYS_THINK` accepts `auto_context: true` + `chain_id` to automatically prepend
assembled context to the prompt before sending to the LLM.

### Persistence

Write-through to SQLite on all mutations (create, update, delete, chain_create, fork).
Loaded from DB on kernel boot.

---

## Planned Improvements

### Phase 2A: Memory Blocks (from Letta research)

Named, typed, shareable context blocks that agents can self-edit.

```cpp
struct MemoryBlock {
    std::string id;
    std::string name;           // "persona", "task_state", "findings"
    uint32_t owner_agent_id;
    MemoryBlockType type;       // SYSTEM (pinned), CORE (always in context), RECALL (on-demand)
    std::string content;
    AccessLevel access;         // PRIVATE, SHARED_READ, SHARED_READWRITE
    std::set<uint32_t> shared_with; // agent IDs with access
    uint64_t updated_at_ms;
};
```

New syscalls:
```
SYS_MEM_READ    — read a named memory block
SYS_MEM_WRITE   — overwrite a block
SYS_MEM_APPEND  — append to a block
SYS_MEM_LIST    — list agent's memory blocks
SYS_MEM_SHARE   — share a block with another agent
```

### Phase 2B: Context Compression Pipeline

Applied before context assembly:

```
Raw artifacts
  │
  ├─ Step 1: Observation masking (free)
  │   Hide verbose tool outputs, keep action history
  │
  ├─ Step 2: Subgoal compression (cheap)
  │   Summarize completed subtask traces into compact records
  │
  ├─ Step 3: Position-aware placement (free)
  │   Critical info at prompt boundaries (beginning/end)
  │   Less important info in middle
  │
  └─ Step 4: Optional LLM summarization (expensive)
      Only if steps 1-3 insufficient to fit budget
```

### Phase 2C: Relevance Filtering

Score artifacts by relevance to current task before injecting into context.
Prevents "distractor interference" from the Lost-in-the-Middle research.

Options:
1. **Keyword overlap** — simple, fast, no LLM call
2. **Embedding similarity** — moderate, requires embedding model
3. **Attention-guided** — best quality, requires 2-pass LLM (expensive)

Start with (1), graduate to (2) when embedding support exists.

---

## File Map

```
libs/context/
├── CMakeLists.txt
├── include/clove/
│   ├── artifact.hpp           — Artifact, Chain structs, enums, serialization
│   ├── artifact_store.hpp     — Thread-safe in-memory artifact CRUD
│   ├── chain_store.hpp        — Thread-safe in-memory chain CRUD
│   └── context_assembler.hpp  — Context assembly with truncation reporting
└── src/
    ├── artifact_store.cpp
    ├── chain_store.cpp
    └── context_assembler.cpp

libs/persistence/
├── include/clove/
│   └── artifact_store_db.hpp  — SQLite persistence for artifacts/chains
└── src/
    └── artifact_store_db.cpp

kernel/src/syscalls/
└── context_syscalls.cpp       — 9 syscall handlers with write-through persistence
```

---

## Design Decisions

1. **Why artifacts not just KV state?** — Artifacts have types, states, provenance (parent_ids),
   and lifecycle. KV is too unstructured for document-centric coordination.

2. **Why forward-only state transitions?** — Prevents agents from un-approving finalized work.
   Matches academic consensus (MetaGPT, artifact-centric coordination papers).

3. **Why author-only updates?** — Security. Agents should only modify their own artifacts.
   Other agents read FINAL artifacts via context assembly.

4. **Why chars/4 for token estimation?** — Good enough for budgeting. A tiktoken C++ binding
   would be more accurate but adds a heavy dependency. Can upgrade later.

5. **Why write-through persistence?** — Artifacts are the research chain. Losing them on
   restart defeats the entire purpose. Unlike ephemeral KV state, artifacts must be durable.
