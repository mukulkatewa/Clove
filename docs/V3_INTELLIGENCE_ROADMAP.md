# CLOVE v3 — Intelligence Layer Roadmap

> **Date:** 2026-03-26
> **Goal:** Make agents genuinely good, not just functional. Every technique below is research-proven with published benchmarks.

---

## The Problem

v2 agents work. They call tools, track cost, filter PII, log audit. But they're dumb:

- One LLM call per step, no reflection on failure
- All memories recalled equally regardless of relevance
- Context assembled by concatenation, not optimized for attention
- Same expensive model for "what is 2+2" and "analyze this 10K line codebase"
- No planning — agents stumble forward step by step
- Tool outputs bloat the context window with irrelevant data

These are solved problems in the research. We just need to implement them.

---

## Priority 1: Model Routing

**What:** Route easy queries to cheap models, hard queries to expensive ones. A lightweight classifier decides.

**Paper:** RouteLLM (LMSYS, 2024)
- https://lmsys.org/blog/2024-07-01-routellm/
- Open source: github.com/lm-sys/routellm

**Benchmarks:**
- MT-Bench: **85% cost reduction** at 95% of GPT-4 quality
- MMLU: **45% cost reduction** at 95% quality
- Only 26% of queries actually needed GPT-4. The rest were fine on smaller models.

**Implementation in CLOVE:**

```
InferenceGateway receives LLM request
    ↓
Router classifies difficulty:
    - Prompt length < 100 tokens + no code → easy
    - Simple factual question → easy
    - Multi-step reasoning, code, analysis → hard
    ↓
Easy → Gemini Flash / Haiku ($0.00001/call)
Hard → Claude Sonnet / GPT-4o ($0.001/call)
```

Where to build: `libs/inference/src/inference_gateway.cpp` — add a `route()` method before the LLM call.

**Impact:** 50-85% cost reduction across real workloads. A fleet that costs $40/week drops to $6-20/week.

---

## Priority 2: Observation Masking

**What:** Instead of keeping full tool outputs in context, truncate old ones. Don't even bother summarizing — research proves simple truncation works as well as expensive LLM summarization.

**Paper:** "The Complexity Trap: Simple Observation Masking Is as Efficient as LLM Summarization for Agent Context Management" (JetBrains Research, NeurIPS DL4Code Workshop 2025)
- https://arxiv.org/abs/2508.21433

**Benchmarks:**
- SWE-bench Verified: **halves cost**, equal or slightly better solve rate
- With Qwen3-Coder: masking **improves** solve rate from 53.8% to 54.8%
- LLM summarization does NOT consistently outperform simple masking

**Why summarization hurts:** Summaries smooth over failure signals. When an agent's tool call fails, the raw error message ("permission denied") is more useful than a summary ("the operation was unsuccessful"). Truncation preserves sharp signals.

**Implementation in CLOVE:**

```
ContextAssembler.assemble():
    for each step in chain:
        if step.age > 3 steps:
            replace step.tool_output with "[output: {first_line}... truncated {N} lines]"
        else:
            keep full output
```

Where to build: `libs/context/src/context_assembler.cpp` — modify the observation inclusion logic.

**Impact:** 50% context reduction → 50% cost reduction on long runs. Agents that ran 15 steps now fit in context that previously only held 8.

---

## Priority 3: Reflexion (Self-Retry on Failure)

**What:** When an agent fails, it generates a natural-language reflection about what went wrong, stores it, and retries with the reflection in context. Verbal reinforcement learning.

**Paper:** "Reflexion: Language Agents with Verbal Reinforcement Learning" (Shinn et al., 2023)
- https://arxiv.org/abs/2303.11366

**Benchmarks:**
- HumanEval coding: **80% → 91%** pass rate (11 point jump)
- HotPotQA: **+20 points** exact-match accuracy over baseline ReAct
- Confirmed statistically significant (p < 0.001) across 9 LLMs

**The pattern:**

```
attempt 1: agent tries task → fails
    ↓
reflection: "I failed because I tried to read a file that doesn't exist.
             I should search for the file first."
    ↓
attempt 2: agent retries with reflection in context → succeeds
    ↓
(max 3 attempts, then give up with best result)
```

**Implementation in CLOVE:**

```
RunEngine.execute():
    for attempt in 1..3:
        result = run_agent_loop(cfg, reflections)

        if result.success:
            return result

        // Generate reflection
        reflection = llm_call(
            "You attempted this task and failed. "
            "Goal: {goal}\n"
            "What you did: {step_log}\n"
            "Result: {error}\n"
            "What went wrong and what should you do differently?"
        )

        reflections.push(reflection)
        audit.log("REFLEXION", {attempt, reflection})

    return best_result
```

Where to build: `libs/api/src/run_engine.cpp` — wrap `execute()` in a retry loop with reflection generation.

**Impact:** The difference between "toy demo" and "actually useful." 11-20% improvement on real tasks.

---

## Priority 4: Memory Importance Scoring

**What:** Score memories on three axes instead of simple name lookup:
1. **Recency** — exponential decay (0.995 per hour since last access)
2. **Importance** — LLM-scored 1-10 at write time ("is this mundane or critical?")
3. **Relevance** — cosine similarity of embeddings to current query

**Paper:** "Generative Agents: Interactive Simulacra of Human Behavior" (Park et al., Stanford UIST 2023)
- https://dl.acm.org/doi/10.1145/3586183.3606763

**The scoring formula:**

```
score(memory, query) =
    α * recency(memory.last_accessed) +      // α = 1.0
    β * importance(memory.importance_score) +  // β = 1.0
    γ * relevance(embed(memory), embed(query)) // γ = 1.0

// All normalized to [0, 1] before combining
// Top-K memories returned to agent
```

**Ablation results:** Removing any one component degraded agent behavior measurably. All three axes are necessary.

**Implementation in CLOVE:**

```
MemoryBlockStore:
    // At write time:
    create(content):
        block.importance = llm_score("Rate importance 1-10: {content}")
        block.embedding = embed(content)
        block.last_accessed = now()

    // At recall time:
    recall(query, top_k=5):
        query_embedding = embed(query)
        for block in all_blocks:
            recency = pow(0.995, hours_since(block.last_accessed))
            importance = block.importance / 10.0
            relevance = cosine_sim(query_embedding, block.embedding)
            block.score = recency + importance + relevance
        return top_k(blocks, by=score)
```

Where to build: `libs/orchestration/src/memory_block_store.cpp` — add importance field, embedding store, and scoring function.

**Impact:** Agents that remember the right things instead of drowning in noise. Qualitative improvement that compounds over time.

---

## Priority 5: Context Positioning (Lost in the Middle)

**What:** LLMs pay much more attention to the beginning and end of their context window. Information in the middle gets up to 20% less attention.

**Paper:** "Lost in the Middle: How Language Models Use Long Contexts" (Liu et al., Stanford, 2023)
- https://arxiv.org/abs/2307.03172
- Published in Transactions of the ACL

**Benchmarks:**
- GPT-3.5-Turbo: **>20% performance drop** when relevant info is in the middle vs. beginning/end
- Tested across 20- and 30-document settings on multi-document QA

**The rule:**

```
Position 1 (beginning):  HIGH attention  → put SYSTEM instructions, agent identity
Position 2-N (middle):   LOW attention   → put tool outputs, historical context
Position N (end):        HIGH attention  → put the GOAL, current task, recent results
```

**Implementation in CLOVE:**

```
ContextAssembler.assemble():
    context = []

    // HIGH ATTENTION ZONE (beginning)
    context += system_blocks     // Agent identity, rules, personality
    context += core_memories     // Critical persistent knowledge

    // LOW ATTENTION ZONE (middle)
    context += chain_artifacts   // Historical outputs, reference data
    context += old_tool_outputs  // Masked/truncated per Priority 2
    context += episodic_memories // Lower-scored memories

    // HIGH ATTENTION ZONE (end)
    context += recent_results    // Last 2-3 tool outputs (full)
    context += current_goal      // What the agent should do NOW
    context += reflections       // What went wrong last time (Priority 3)
```

Where to build: `libs/context/src/context_assembler.cpp` — enforce ordering in `assemble()`.

**Impact:** Free 10-20% accuracy improvement. Zero cost. Just reorder what's already there.

---

## Priority 6: Plan-then-Execute

**What:** Before executing any tools, have the agent write a complete plan. Then execute the plan step by step.

**Papers:**
- "Plan-and-Solve Prompting" (Wang et al., 2023)
- "Tree of Thoughts" (Yao et al., NeurIPS 2023)

**Benchmarks:**
- Game of 24: **4% → 74%** success rate (Plan-and-Solve vs. direct)
- Creative writing and crosswords: significant improvements
- GSM8K: slight improvement (model already strong)

**Implementation in CLOVE:**

```
RunEngine system prompt:
    "You are an AI agent. Before taking any action:
     1. Analyze the goal
     2. Write a numbered plan (3-7 steps)
     3. Execute each step in order
     4. After each step, check if the plan needs adjustment

     Format your plan as:
     PLAN:
     1. [step]
     2. [step]
     ...

     Then begin execution."
```

Where to build: `libs/api/src/run_engine.cpp` — modify the system prompt in `execute()`. Store the plan as a chain artifact.

**Impact:** Massive on complex tasks (4% → 74%). Minimal on simple tasks. One extra LLM call.

---

## What NOT to Implement

| Technique | Why Not |
|---|---|
| Multi-agent debate | Inconsistent results. 3-5x cost. Doesn't beat majority vote. (ICML 2024) |
| Tree of Thoughts (full) | Too expensive — many LLM calls per decision. Only helps puzzles. Plan-and-Solve gets 80% of the benefit. |
| HippoRAG / PageRank | Overkill. We're an agent OS, not a RAG system. If we need KG later, add it then. |
| Full RL tool selection | Research-grade complexity. Practical version: just prune irrelevant tools from prompt. |

---

## Implementation Order

| # | Technique | Effort | Cost Impact | Quality Impact | Where |
|---|---|---|---|---|---|
| 1 | Model routing | 3 days | **-50-85%** | ~same | inference_gateway.cpp |
| 2 | Observation masking | 1 day | **-50%** | ~same or better | context_assembler.cpp |
| 3 | Reflexion | 2 days | +2-3x per retry | **+11-20%** | run_engine.cpp |
| 4 | Memory scoring | 3 days | minimal | **qualitative** | memory_block_store.cpp |
| 5 | Context positioning | 0.5 day | free | **+10-20%** | context_assembler.cpp |
| 6 | Plan-then-execute | 0.5 day | +1 LLM call | **+10-70%** (task dependent) | run_engine.cpp |

**Total: ~10 days of work for transformative improvement.**

After these 6, a CLOVE agent will:
- Use the cheapest model that works (routing)
- Not waste context on old tool outputs (masking)
- Retry intelligently when it fails (reflexion)
- Remember the right things (memory scoring)
- Put important info where the LLM actually reads it (positioning)
- Think before acting (planning)

That's not a toy. That's a production agent.
