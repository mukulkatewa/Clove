# CLOVE v2 — Memory Architecture Design

> Status: **Implemented** | Date: 2026-03-23
> Based on: MemoryOS (EMNLP 2025), Letta Memory Blocks, HiAgent (ACL 2025),
> A-MEM (NeurIPS 2025), Mem0, Lost-in-the-Middle (Stanford)

## Problem

Agents on long-horizon tasks (50+ steps, 10+ tool calls, multi-agent coordination)
face three memory problems:

1. **Context overflow** — history grows beyond the context window
2. **Distractor interference** — irrelevant context degrades accuracy by 30%+
3. **Cross-session amnesia** — agents forget everything between runs

Current CLOVE has StateStore (flat KV) and ArtifactStore (typed documents).
Neither provides structured memory management.

## Design: Three-Tier Hierarchical Memory

```
┌─────────────────────────────────────────────────────┐
│ Tier 1: Working Memory                               │
│ Location: LLM context window                         │
│ Size: model-dependent (4K-200K tokens)               │
│ Latency: 0ms (already in prompt)                     │
│ Contents:                                            │
│   - System prompt                                    │
│   - CORE memory blocks (always present)              │
│   - Assembled context from Tier 2                    │
│   - Current task instruction                         │
│ Managed by: ContextAssembler                         │
├─────────────────────────────────────────────────────┤
│ Tier 2: Session Memory                               │
│ Location: Kernel in-memory stores                    │
│ Size: unlimited (RAM-bounded)                        │
│ Latency: <1ms                                        │
│ Contents:                                            │
│   - Memory blocks (named, agent-editable)            │
│   - Recent artifacts (current chain)                 │
│   - Recent tool outputs (observation history)        │
│   - Other agents' shared blocks                      │
│ Managed by: MemoryBlockStore + ArtifactStore         │
├─────────────────────────────────────────────────────┤
│ Tier 3: Persistent Memory                            │
│ Location: SQLite                                     │
│ Size: unlimited (disk-bounded)                       │
│ Latency: 1-10ms                                      │
│ Contents:                                            │
│   - Completed chains (past research runs)            │
│   - Case library (successful task trajectories)      │
│   - Agent profiles (learned preferences)             │
│   - Archived artifacts                               │
│ Managed by: ArtifactStoreDb + CaseStore (future)     │
└─────────────────────────────────────────────────────┘
```

## Promotion & Demotion Policies

```
Tier 3 → Tier 2: On relevance match
  Trigger: Agent starts task, kernel retrieves relevant cases/artifacts
  Method:  Key lookup or prefix search (future: embedding similarity)

Tier 2 → Tier 1: On context assembly
  Trigger: SYS_THINK with auto_context, or SYS_CONTEXT_ASSEMBLE
  Method:  ContextAssembler selects by scope (L1/L2/L3) and budget

Tier 1 → Tier 2: After LLM call
  Trigger: SYS_THINK completes
  Method:  Agent creates artifacts (doc_create) or updates memory blocks

Tier 2 → Tier 3: After task completes
  Trigger: Chain marked complete, or agent exits
  Method:  Write-through persistence (already implemented for artifacts)
```

## Memory Blocks

Named, typed context sections that agents can self-edit.

### Types

| Type | Behavior | Example |
|------|----------|---------|
| SYSTEM | Always pinned at top of context. Cannot be edited by agent. | Agent role/persona |
| CORE | Always included in context assembly. Agent can edit. | Task state, findings so far |
| RECALL | Included only when relevant. Agent can read/write. | Reference data, past learnings |

### Access Control

| Level | Meaning |
|-------|---------|
| PRIVATE | Only the owning agent can read/write |
| SHARED_READ | Other agents can read, only owner can write |
| SHARED_READWRITE | Multiple agents can read and write |

### Proposed Syscalls

```
SYS_MEM_CREATE   = 0xE9   — Create a named memory block
SYS_MEM_READ     = 0xEA   — Read block content
SYS_MEM_WRITE    = 0xEB   — Overwrite block content
SYS_MEM_APPEND   = 0xEC   — Append to block
SYS_MEM_DELETE    = 0xED   — Delete a block
SYS_MEM_LIST     = 0xEE   — List agent's blocks
SYS_MEM_SHARE    = 0xEF   — Share block with another agent
```

### Data Structure

```cpp
enum class MemoryBlockType : uint8_t { SYSTEM = 0, CORE = 1, RECALL = 2 };
enum class MemoryAccess    : uint8_t { PRIVATE = 0, SHARED_READ = 1, SHARED_READWRITE = 2 };

struct MemoryBlock {
    std::string id;                     // "mem_" + 12-char hex
    std::string name;                   // human-readable name
    uint32_t owner_agent_id;
    MemoryBlockType type;
    MemoryAccess access;
    std::string content;                // markdown/text
    std::set<uint32_t> shared_with;     // agent IDs with access
    size_t max_tokens = 0;              // 0 = unlimited
    uint64_t created_at_ms;
    uint64_t updated_at_ms;
};
```

## Context Assembly v2

Updated assembly incorporating memory blocks:

```
1. SYSTEM blocks (pinned, not counted against budget)
2. CORE blocks (always included, counted)
3. L1 shared artifacts (FINAL/APPROVED, sorted by created_at)
4. RECALL blocks (if relevant, counted)
5. L3 private artifacts (DRAFT by this agent)
6. Task instruction (the actual prompt)
```

**Position-aware placement** (from Lost-in-the-Middle research):
- Positions 1-2: System prompt + most important CORE block
- Position last: Task instruction (the actual question)
- Middle: Less critical context (RECALL blocks, older artifacts)

This ensures the model attends most to the instructions and key context.

## Context Compression Pipeline

Applied during assembly when budget is tight:

```
Step 1: Observation Masking (free, ~50% savings)
  - Replace verbose tool outputs with "[tool_name returned N chars]"
  - Keep action descriptions and error messages
  - Applies to artifacts with type=NOTE or metadata.tool_output=true

Step 2: Subgoal Compression (cheap, ~35% savings)
  - When a subtask's artifacts are all FINAL, compress the chain:
    "Subtask: [title] → Result: [last artifact summary, 200 chars]"
  - Replaces N artifacts with 1 compressed record

Step 3: Position-Aware Reordering (free)
  - Move highest-relevance artifacts to positions 1-3 and last
  - Move lowest-relevance to middle positions

Step 4: LLM Summarization (expensive, only if needed)
  - If budget still exceeded after steps 1-3
  - Summarize oldest artifacts using a fast/cheap model
  - Cache summaries for reuse
```

## Integration with Existing Systems

| Existing System | Integration |
|-----------------|-------------|
| ArtifactStore | Memory blocks stored alongside artifacts. Same persistence layer. |
| StateStore | Memory blocks replace some KV usage (structured > unstructured). |
| ContextAssembler | Extended to include memory blocks in assembly. |
| SYS_THINK | Already supports auto_context. Will include blocks automatically. |
| Permissions | Memory block access checked via PermissionsStore. |
| Audit | Memory mutations logged to AuditLogger. |

## Implementation Status

All items implemented (2026-03-23):
- MemoryBlockStore class (libs/context/src/memory_block_store.cpp)
- 7 syscalls (0xE9-0xEF) in kernel/src/syscalls/memory_syscalls.cpp
- ContextAssembler v2 includes SYSTEM/CORE/RECALL blocks
- SQLite persistence (libs/persistence/src/memory_block_db.cpp) with write-through
- Python SDK: mem_create, mem_read, mem_write, mem_append, mem_delete, mem_list, mem_share
- Observation masking compression in ContextAssembler

### Remaining
- Subgoal compression (Step 2 of compression pipeline)
- LLM-based summarization (Step 4, expensive, deferred)
