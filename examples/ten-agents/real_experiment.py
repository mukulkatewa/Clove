#!/usr/bin/env python3
"""
CLOVE 10-Agent Real Experiment

This runs ON the CLOVE kernel. Every LLM call goes through SYS_THINK.
Every state operation goes through SYS_STORE/SYS_FETCH. PII filtering
is kernel-enforced. Execution replay records everything.

REQUIRES the kernel to be running:
    ./build/kernel/clove_kernel --api --api-port 8080 \
        --openrouter --openrouter-key $OPENROUTER_API_KEY \
        --privacy --db /tmp/clove-experiment.db

Then run:
    python3 examples/ten-agents/real_experiment.py
"""

import json
import os
import sys
import time
import threading
import datetime
from pathlib import Path
from dataclasses import dataclass, field

sys.path.insert(0, str(Path(__file__).parent.parent.parent / "sdk" / "python"))
from clove_sdk.client import CloveClient

REPORTS_DIR = Path(__file__).parent / "reports"
REPORTS_DIR.mkdir(exist_ok=True)

TOPICS = [
    "How AI agents are changing software development in 2026",
    "The security challenges of autonomous AI agents",
    "Multi-agent coordination: patterns and architectures",
    "AI agent cost optimization strategies",
    "The role of sandboxing in trustworthy AI deployment",
    "Open source AI agent frameworks compared",
    "Execution replay and audit trails for AI compliance",
    "Edge deployment of AI agents",
    "The economics of running large AI agent fleets",
    "Privacy-preserving AI agents and data governance",
]


@dataclass
class AgentStats:
    name: str
    role: str
    duration_ms: float = 0
    tokens: int = 0
    cost_usd: float = 0
    success: bool = False
    error: str = ""


def make_client() -> CloveClient:
    """Create a new kernel connection."""
    c = CloveClient()
    c.connect()
    return c


def agent_researcher(topic: str, agent_num: int) -> AgentStats:
    """Research agent — uses kernel SYS_THINK + SYS_STORE."""
    stats = AgentStats(name=f"researcher-{agent_num}", role="researcher")
    client = make_client()

    try:
        start = time.monotonic()
        resp = client.think(
            f"Research this topic concisely: {topic}\n\n"
            f"Provide 3-4 key findings as bullet points. Be specific and factual. Max 200 words."
        )
        stats.duration_ms = (time.monotonic() - start) * 1000

        if resp.get("success"):
            stats.success = True
            stats.tokens = resp.get("tokens", 0)
            stats.cost_usd = resp.get("cost_usd", 0)
            # Store findings in kernel state store
            client.store(f"research:{topic}", {
                "content": resp["content"],
                "tokens": stats.tokens,
                "cost_usd": stats.cost_usd,
            })
        else:
            stats.error = resp.get("error", "unknown")
    except Exception as e:
        stats.error = str(e)
    finally:
        client.disconnect()

    return stats


def agent_writer(topic: str, agent_num: int) -> AgentStats:
    """Writer agent — reads research from kernel state, writes draft via SYS_THINK."""
    stats = AgentStats(name=f"writer-{agent_num}", role="writer")
    client = make_client()

    try:
        # Fetch research from kernel state store
        research_data = client.fetch(f"research:{topic}")
        if not research_data:
            stats.error = "no research found in state store"
            client.disconnect()
            return stats

        findings = research_data.get("content", "") if isinstance(research_data, dict) else str(research_data)

        start = time.monotonic()
        resp = client.think(
            f"Write a professional report section on: {topic}\n\n"
            f"Based on these findings:\n{findings}\n\n"
            f"Write 1-2 clear paragraphs. Professional tone. Max 200 words."
        )
        stats.duration_ms = (time.monotonic() - start) * 1000

        if resp.get("success"):
            stats.success = True
            stats.tokens = resp.get("tokens", 0)
            stats.cost_usd = resp.get("cost_usd", 0)
            client.store(f"draft:{topic}", {
                "content": resp["content"],
                "tokens": stats.tokens,
                "cost_usd": stats.cost_usd,
            })
        else:
            stats.error = resp.get("error", "unknown")
    except Exception as e:
        stats.error = str(e)
    finally:
        client.disconnect()

    return stats


def agent_reviewer(topic: str, agent_num: int) -> AgentStats:
    """Reviewer agent — reads draft from kernel state, reviews via SYS_THINK."""
    stats = AgentStats(name=f"reviewer-{agent_num}", role="reviewer")
    client = make_client()

    try:
        draft_data = client.fetch(f"draft:{topic}")
        if not draft_data:
            stats.error = "no draft found in state store"
            client.disconnect()
            return stats

        draft = draft_data.get("content", "") if isinstance(draft_data, dict) else str(draft_data)

        # PII scan the draft before review (kernel-enforced)
        pii = client.pii_scan(draft)
        pii_count = pii.get("count", 0) if isinstance(pii, dict) else 0

        start = time.monotonic()
        resp = client.think(
            f"Review this report section:\n\n{draft}\n\n"
            f"Rate 1-10. Note strengths and weaknesses. 2-3 sentences max."
        )
        stats.duration_ms = (time.monotonic() - start) * 1000

        if resp.get("success"):
            stats.success = True
            stats.tokens = resp.get("tokens", 0)
            stats.cost_usd = resp.get("cost_usd", 0)
            client.store(f"review:{topic}", {
                "content": resp["content"],
                "tokens": stats.tokens,
                "pii_detected": pii_count,
            })
        else:
            stats.error = resp.get("error", "unknown")
    except Exception as e:
        stats.error = str(e)
    finally:
        client.disconnect()

    return stats


def agent_synthesizer(topics: list) -> AgentStats:
    """Synthesizer — combines all drafts + reviews into final report."""
    stats = AgentStats(name="synthesizer", role="synthesizer")
    client = make_client()

    try:
        sections = ""
        for topic in topics:
            draft = client.fetch(f"draft:{topic}")
            review = client.fetch(f"review:{topic}")
            d = draft.get("content", "") if isinstance(draft, dict) else str(draft or "")
            r = review.get("content", "") if isinstance(review, dict) else str(review or "")
            sections += f"\n## {topic}\n{d}\n\nReview: {r}\n"

        start = time.monotonic()
        resp = client.think(
            f"Combine these sections into a cohesive final report:\n{sections}\n\n"
            f"Write an introduction, the sections, and a conclusion. Max 500 words."
        )
        stats.duration_ms = (time.monotonic() - start) * 1000

        if resp.get("success"):
            stats.success = True
            stats.tokens = resp.get("tokens", 0)
            stats.cost_usd = resp.get("cost_usd", 0)
            client.store("final_report", {"content": resp["content"]})
        else:
            stats.error = resp.get("error", "unknown")
    except Exception as e:
        stats.error = str(e)
    finally:
        client.disconnect()

    return stats


def run():
    print("=" * 65)
    print("  CLOVE 10-Agent Real Experiment")
    print("  Everything runs through the kernel")
    print("=" * 65)

    # Connect to kernel and verify
    client = make_client()
    hello = client.hello()
    print(f"\n  Kernel:      v{hello.get('kernel_version', '?')}")
    print(f"  Protocol:    v{hello.get('protocol_version', '?')}")
    print(f"  Features:    {hello.get('features', {})}")

    health = {}
    try:
        import urllib.request
        health = json.loads(urllib.request.urlopen("http://localhost:8080/api/health").read())
    except Exception:
        pass
    print(f"  API:         {health.get('status', 'not available')}")
    print(f"  Uptime:      {health.get('uptime_s', '?')}s")

    # Start execution recording
    client.record_start()
    print(f"\n  Execution recording: ON")

    # Pick 3 topics for this run
    topics = TOPICS[:3]
    print(f"  Topics: {len(topics)}")
    for t in topics:
        print(f"    - {t}")

    client.disconnect()

    all_stats: list[AgentStats] = []
    experiment_start = time.monotonic()

    # ── Phase 1: Research (3 agents in parallel) ──
    print(f"\n  [Phase 1] Research — 3 agents in parallel")
    t1 = time.monotonic()
    threads = []
    results = [None] * 3

    for i, topic in enumerate(topics):
        def worker(idx=i, t=topic):
            results[idx] = agent_researcher(t, idx + 1)
        threads.append(threading.Thread(target=worker))

    for t in threads:
        t.start()
    for t in threads:
        t.join()

    for r in results:
        all_stats.append(r)
        mark = "OK" if r.success else f"FAIL: {r.error}"
        print(f"    {r.name}: {r.duration_ms:.0f}ms, {r.tokens} tokens, ${r.cost_usd:.6f} [{mark}]")
    print(f"    Phase total: {(time.monotonic() - t1)*1000:.0f}ms (parallel)")

    # ── Phase 2: Write (3 agents in parallel) ──
    print(f"\n  [Phase 2] Write — 3 agents in parallel")
    t2 = time.monotonic()
    threads = []
    results = [None] * 3

    for i, topic in enumerate(topics):
        def worker(idx=i, t=topic):
            results[idx] = agent_writer(t, idx + 1)
        threads.append(threading.Thread(target=worker))

    for t in threads:
        t.start()
    for t in threads:
        t.join()

    for r in results:
        all_stats.append(r)
        mark = "OK" if r.success else f"FAIL: {r.error}"
        print(f"    {r.name}: {r.duration_ms:.0f}ms, {r.tokens} tokens, ${r.cost_usd:.6f} [{mark}]")
    print(f"    Phase total: {(time.monotonic() - t2)*1000:.0f}ms (parallel)")

    # ── Phase 3: Review (2 agents — reviewer-1 gets 2, reviewer-2 gets 1) ──
    print(f"\n  [Phase 3] Review — 2 agents in parallel + PII scanning")
    t3 = time.monotonic()
    threads = []
    review_results = []

    def review_worker_1():
        review_results.append(agent_reviewer(topics[0], 1))
        review_results.append(agent_reviewer(topics[1], 1))

    def review_worker_2():
        review_results.append(agent_reviewer(topics[2], 2))

    t_r1 = threading.Thread(target=review_worker_1)
    t_r2 = threading.Thread(target=review_worker_2)
    t_r1.start()
    t_r2.start()
    t_r1.join()
    t_r2.join()

    for r in review_results:
        all_stats.append(r)
        mark = "OK" if r.success else f"FAIL: {r.error}"
        print(f"    {r.name}: {r.duration_ms:.0f}ms, {r.tokens} tokens, ${r.cost_usd:.6f} [{mark}]")
    print(f"    Phase total: {(time.monotonic() - t3)*1000:.0f}ms (parallel)")

    # ── Phase 4: Synthesize ──
    print(f"\n  [Phase 4] Synthesize — 1 agent combines everything")
    t4 = time.monotonic()
    synth = agent_synthesizer(topics)
    all_stats.append(synth)
    mark = "OK" if synth.success else f"FAIL: {synth.error}"
    print(f"    synthesizer: {synth.duration_ms:.0f}ms, {synth.tokens} tokens, ${synth.cost_usd:.6f} [{mark}]")
    print(f"    Phase total: {(time.monotonic() - t4)*1000:.0f}ms")

    # ── Phase 5: Monitor — collect kernel metrics ──
    print(f"\n  [Phase 5] Monitor — collecting kernel metrics")
    monitor_client = make_client()

    # Stop recording
    monitor_client.record_stop()
    rec_status = monitor_client.record_status()
    print(f"    Execution entries recorded: {rec_status.get('entry_count', 0)}")

    # System metrics
    sys_metrics = monitor_client.system_metrics()
    print(f"    Kernel RSS: {sys_metrics.get('rss_bytes', 0) / 1024 / 1024:.1f} MB")
    print(f"    State store: {sys_metrics.get('state_store_size', 0)} entries")
    print(f"    Audit entries: {sys_metrics.get('audit_entry_count', 0)}")
    print(f"    LLM requests: {sys_metrics.get('llm_total_requests', 0)}")
    print(f"    LLM completed: {sys_metrics.get('llm_total_completed', 0)}")

    # Fetch final report
    final_data = monitor_client.fetch("final_report")
    final_report = ""
    if isinstance(final_data, dict):
        final_report = final_data.get("content", "")

    monitor_client.disconnect()

    # ═══════════════════════════════════════════════════════════════
    total_duration = time.monotonic() - experiment_start
    total_cost = sum(s.cost_usd for s in all_stats)
    total_tokens = sum(s.tokens for s in all_stats)
    succeeded = sum(1 for s in all_stats if s.success)
    failed = sum(1 for s in all_stats if not s.success)
    durations = [s.duration_ms for s in all_stats if s.success]

    print(f"\n{'=' * 65}")
    print(f"  RESULTS")
    print(f"{'=' * 65}")
    print(f"\n  Duration:       {total_duration:.1f}s")
    print(f"  Agents:         10 ({succeeded} succeeded, {failed} failed)")
    print(f"  LLM calls:      {succeeded} through kernel SYS_THINK")
    print(f"  Total tokens:   {total_tokens:,}")
    print(f"  Total cost:     ${total_cost:.6f}")
    if durations:
        print(f"  Avg LLM latency: {sum(durations)/len(durations):.0f}ms")
        print(f"  Min/Max:        {min(durations):.0f}ms / {max(durations):.0f}ms")
    print(f"  Replay entries: {rec_status.get('entry_count', 0)}")
    print(f"  Kernel memory:  {sys_metrics.get('rss_bytes', 0) / 1024 / 1024:.1f} MB")

    print(f"\n  Per-Agent:")
    print(f"  {'Name':<16} {'Role':<13} {'Time':>8} {'Tokens':>7} {'Cost':>10} {'Status'}")
    print(f"  {'-'*16} {'-'*13} {'-'*8} {'-'*7} {'-'*10} {'-'*6}")
    for s in all_stats:
        t_str = f"{s.duration_ms:.0f}ms"
        c_str = f"${s.cost_usd:.6f}" if s.cost_usd > 0 else "-"
        print(f"  {s.name:<16} {s.role:<13} {t_str:>8} {s.tokens:>7} {c_str:>10} {'OK' if s.success else 'FAIL'}")

    # Save report
    if final_report:
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = REPORTS_DIR / f"real_experiment_{ts}.md"
        with open(report_path, "w") as f:
            f.write(f"# CLOVE 10-Agent Experiment Report\n\n")
            f.write(f"*Generated {datetime.datetime.now().isoformat()} — "
                    f"{total_duration:.1f}s, {succeeded} agents, ${total_cost:.6f} cost*\n\n")
            f.write(final_report)
            f.write(f"\n\n---\n*Kernel: CLOVE v2.0.0 | "
                    f"Tokens: {total_tokens:,} | "
                    f"Replay entries: {rec_status.get('entry_count', 0)} | "
                    f"Kernel RSS: {sys_metrics.get('rss_bytes', 0) / 1024 / 1024:.1f} MB*\n")
        print(f"\n  Report saved: {report_path}")

    # Save metrics JSON
    metrics_path = Path(__file__).parent / "real_experiment_metrics.json"
    with open(metrics_path, "w") as f:
        json.dump({
            "duration_s": round(total_duration, 2),
            "agents": succeeded,
            "failures": failed,
            "total_tokens": total_tokens,
            "total_cost_usd": round(total_cost, 6),
            "avg_latency_ms": round(sum(durations)/len(durations), 1) if durations else 0,
            "replay_entries": rec_status.get("entry_count", 0),
            "kernel_rss_mb": round(sys_metrics.get("rss_bytes", 0) / 1024 / 1024, 1),
            "state_store_size": sys_metrics.get("state_store_size", 0),
            "per_agent": [
                {"name": s.name, "role": s.role, "duration_ms": round(s.duration_ms, 1),
                 "tokens": s.tokens, "cost_usd": round(s.cost_usd, 6), "success": s.success}
                for s in all_stats
            ],
        }, f, indent=2)
    print(f"  Metrics saved: {metrics_path}")

    print(f"\n  Verify with:")
    print(f"    curl localhost:8080/api/health")
    print(f"    ./build/cli/clove_cli audit --limit 20")
    print(f"    open http://localhost:8080/dashboard")


if __name__ == "__main__":
    run()
