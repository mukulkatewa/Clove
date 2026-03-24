#!/usr/bin/env python3
"""
CLOVE 30-Minute Stress Test

Runs continuous cycles through the CLOVE kernel for 30 minutes.
Every cycle: 10 agents (3 researchers, 3 writers, 2 reviewers, 1 synthesizer, 1 PII scanner)
No sleep between cycles — back to back, as fast as the LLMs respond.

Start kernel first:
    ./build/kernel/clove_kernel

Then run:
    python3 examples/ten-agents/stress_test.py
"""

import json
import os
import sys
import time
import threading
import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent / "sdk" / "python"))
from clove_sdk.client import CloveClient

REPORTS_DIR = Path(__file__).parent / "stress_reports"
REPORTS_DIR.mkdir(exist_ok=True)

DURATION_MINUTES = 30
if len(sys.argv) > 1:
    try:
        DURATION_MINUTES = int(sys.argv[1])
    except ValueError:
        pass

TOPICS = [
    "How AI agents are changing software development",
    "Security challenges of autonomous AI agents",
    "Multi-agent coordination patterns",
    "AI agent cost optimization",
    "Sandboxing for trustworthy AI deployment",
    "Open source agent frameworks compared",
    "Execution replay for AI compliance",
    "Edge deployment of AI agents",
    "Economics of large AI agent fleets",
    "Privacy-preserving AI agents",
    "AI agents in healthcare",
    "Building resilient AI pipelines",
    "The future of agent infrastructure",
    "Real-time AI agent monitoring",
    "AI agent marketplace economics",
    "Federated learning with AI agents",
    "AI agents for scientific research",
    "Autonomous coding agents",
    "AI agent debugging and observability",
    "Multi-tenant agent isolation",
]


def make_client():
    c = CloveClient()
    c.connect()
    return c


def llm_call(prompt, max_retries=2):
    """Make an LLM call through the kernel with retry."""
    for attempt in range(max_retries + 1):
        try:
            client = make_client()
            resp = client.think(prompt)
            client.disconnect()
            return resp
        except Exception as e:
            if attempt < max_retries:
                time.sleep(0.5)
                continue
            return {"success": False, "error": str(e), "tokens": 0, "cost_usd": 0}


def run_cycle(cycle_num, topic):
    """Run one full cycle: research → write → review → synthesize → PII scan."""
    cycle_start = time.monotonic()
    stats = {
        "cycle": cycle_num,
        "topic": topic,
        "agents": [],
        "success": True,
        "errors": [],
    }

    # Phase 1: 3 researchers in parallel
    research_results = [None, None, None]
    subtopics = [
        f"{topic} — current state",
        f"{topic} — challenges",
        f"{topic} — future outlook",
    ]

    def researcher(idx):
        start = time.monotonic()
        resp = llm_call(f"Research concisely: {subtopics[idx]}. 3 bullet points. Max 100 words.")
        elapsed = (time.monotonic() - start) * 1000
        research_results[idx] = resp
        stats["agents"].append({
            "name": f"researcher-{idx+1}", "role": "researcher",
            "duration_ms": round(elapsed, 1),
            "tokens": resp.get("tokens", 0),
            "cost_usd": resp.get("cost_usd", 0),
            "success": resp.get("success", False),
        })
        if resp.get("success"):
            try:
                c = make_client()
                c.store(f"cycle{cycle_num}:research:{idx}", resp.get("content", ""))
                c.disconnect()
            except Exception:
                pass

    threads = [threading.Thread(target=researcher, args=(i,)) for i in range(3)]
    for t in threads: t.start()
    for t in threads: t.join()

    # Phase 2: 3 writers in parallel
    def writer(idx):
        findings = research_results[idx].get("content", "") if research_results[idx] else ""
        start = time.monotonic()
        resp = llm_call(f"Write 1 paragraph about: {subtopics[idx]}. Based on: {findings[:300]}. Max 100 words.")
        elapsed = (time.monotonic() - start) * 1000
        stats["agents"].append({
            "name": f"writer-{idx+1}", "role": "writer",
            "duration_ms": round(elapsed, 1),
            "tokens": resp.get("tokens", 0),
            "cost_usd": resp.get("cost_usd", 0),
            "success": resp.get("success", False),
        })
        if resp.get("success"):
            try:
                c = make_client()
                c.store(f"cycle{cycle_num}:draft:{idx}", resp.get("content", ""))
                c.disconnect()
            except Exception:
                pass

    threads = [threading.Thread(target=writer, args=(i,)) for i in range(3)]
    for t in threads: t.start()
    for t in threads: t.join()

    # Phase 3: 2 reviewers in parallel
    def reviewer(idx):
        try:
            c = make_client()
            draft = c.fetch(f"cycle{cycle_num}:draft:{idx}")
            c.disconnect()
        except Exception:
            draft = ""
        draft_text = draft if isinstance(draft, str) else str(draft) if draft else ""
        start = time.monotonic()
        resp = llm_call(f"Rate this 1-10 in one sentence: {draft_text[:300]}")
        elapsed = (time.monotonic() - start) * 1000
        stats["agents"].append({
            "name": f"reviewer-{idx+1}", "role": "reviewer",
            "duration_ms": round(elapsed, 1),
            "tokens": resp.get("tokens", 0),
            "cost_usd": resp.get("cost_usd", 0),
            "success": resp.get("success", False),
        })

    threads = [threading.Thread(target=reviewer, args=(i,)) for i in range(2)]
    for t in threads: t.start()
    for t in threads: t.join()

    # Phase 4: synthesizer
    start = time.monotonic()
    resp = llm_call(f"Write a 2-sentence summary about: {topic}. Be insightful.")
    elapsed = (time.monotonic() - start) * 1000
    stats["agents"].append({
        "name": "synthesizer", "role": "synthesizer",
        "duration_ms": round(elapsed, 1),
        "tokens": resp.get("tokens", 0),
        "cost_usd": resp.get("cost_usd", 0),
        "success": resp.get("success", False),
    })
    summary = resp.get("content", "") if resp.get("success") else ""

    # Phase 5: PII scan on output
    try:
        c = make_client()
        pii = c.pii_scan(summary + " test@example.com 555-123-4567")
        c.disconnect()
        stats["agents"].append({
            "name": "pii-scanner", "role": "monitor",
            "duration_ms": 0,
            "tokens": 0,
            "cost_usd": 0,
            "success": True,
            "pii_found": pii.get("count", 0),
        })
    except Exception:
        pass

    stats["duration_s"] = round(time.monotonic() - cycle_start, 1)
    stats["total_tokens"] = sum(a.get("tokens", 0) for a in stats["agents"])
    stats["total_cost"] = sum(a.get("cost_usd", 0) for a in stats["agents"])
    stats["succeeded"] = sum(1 for a in stats["agents"] if a.get("success"))
    stats["failed"] = sum(1 for a in stats["agents"] if not a.get("success"))

    # Save summary
    if summary:
        report_path = REPORTS_DIR / f"cycle-{cycle_num:03d}.md"
        with open(report_path, "w") as f:
            f.write(f"# {topic}\n\n{summary}\n\n---\n*Cycle {cycle_num} | {stats['duration_s']}s | "
                    f"{stats['total_tokens']} tokens | ${stats['total_cost']:.6f}*\n")

    return stats


def main():
    print("=" * 65)
    print("  CLOVE 30-Minute Stress Test")
    print("  Continuous 10-agent cycles through the kernel")
    print("=" * 65)

    # Verify kernel
    try:
        client = make_client()
        hello = client.hello()
        client.disconnect()
        print(f"\n  Kernel: v{hello.get('kernel_version', '?')}")
    except Exception as e:
        print(f"\n  ERROR: Cannot connect to kernel: {e}")
        print(f"  Run: ./build/kernel/clove_kernel")
        sys.exit(1)

    # Start recording
    try:
        client = make_client()
        client.record_start()
        client.disconnect()
    except Exception:
        pass

    end_time = time.monotonic() + (DURATION_MINUTES * 60)
    start_time = time.monotonic()
    all_cycles = []
    cycle_num = 0

    total_tokens = 0
    total_cost = 0.0
    total_agents = 0
    total_failures = 0

    print(f"  Duration: {DURATION_MINUTES} minutes")
    print(f"  Topics: {len(TOPICS)} in rotation")
    print(f"  Started: {datetime.datetime.now().strftime('%H:%M:%S')}")
    print(f"  Ends at: {(datetime.datetime.now() + datetime.timedelta(minutes=DURATION_MINUTES)).strftime('%H:%M:%S')}")
    print(f"\n  No sleep between cycles — running as fast as possible.\n")

    try:
        while time.monotonic() < end_time:
            cycle_num += 1
            topic = TOPICS[(cycle_num - 1) % len(TOPICS)]
            elapsed_min = (time.monotonic() - start_time) / 60
            remaining_min = (end_time - time.monotonic()) / 60

            ts = datetime.datetime.now().strftime('%H:%M:%S')
            print(f"  [{ts}] Cycle {cycle_num:3d} | {elapsed_min:.1f}m elapsed | {remaining_min:.1f}m left | {topic[:45]}...")

            stats = run_cycle(cycle_num, topic)
            all_cycles.append(stats)

            total_tokens += stats["total_tokens"]
            total_cost += stats["total_cost"]
            total_agents += stats["succeeded"]
            total_failures += stats["failed"]

            ok = stats["succeeded"]
            fail = stats["failed"]
            print(f"             {stats['duration_s']}s | {ok} ok {fail} fail | "
                  f"{stats['total_tokens']} tokens | ${stats['total_cost']:.4f} | "
                  f"running total: ${total_cost:.4f}")

    except KeyboardInterrupt:
        print(f"\n  Stopped by user at cycle {cycle_num}")

    # Stop recording
    replay_entries = 0
    kernel_rss = 0
    state_size = 0
    llm_total = 0
    try:
        client = make_client()
        client.record_stop()
        rec = client.record_status()
        replay_entries = rec.get("entry_count", 0)
        metrics = client.system_metrics()
        kernel_rss = metrics.get("rss_bytes", 0) / 1024 / 1024
        state_size = metrics.get("state_store_size", 0)
        llm_total = metrics.get("llm_total_completed", 0)
        client.disconnect()
    except Exception:
        pass

    total_elapsed = time.monotonic() - start_time

    # Final summary
    print(f"\n{'=' * 65}")
    print(f"  STRESS TEST RESULTS")
    print(f"{'=' * 65}")
    print(f"\n  Duration:         {total_elapsed/60:.1f} minutes")
    print(f"  Cycles:           {cycle_num}")
    print(f"  Agents total:     {total_agents} succeeded, {total_failures} failed")
    print(f"  LLM calls:        {llm_total} (kernel reported)")
    print(f"  Total tokens:     {total_tokens:,}")
    print(f"  Total cost:       ${total_cost:.4f}")
    if cycle_num > 0:
        print(f"  Avg cycle time:   {total_elapsed/cycle_num:.1f}s")
        print(f"  Cycles/minute:    {cycle_num/(total_elapsed/60):.1f}")
        print(f"  Agents/minute:    {total_agents/(total_elapsed/60):.1f}")
    print(f"  Replay entries:   {replay_entries}")
    print(f"  Kernel RSS:       {kernel_rss:.1f} MB")
    print(f"  State store:      {state_size} entries")
    print(f"  Reports saved:    {len(list(REPORTS_DIR.glob('cycle-*.md')))}")

    # Save final metrics
    metrics_path = Path(__file__).parent / "stress_test_results.json"
    with open(metrics_path, "w") as f:
        json.dump({
            "duration_minutes": round(total_elapsed / 60, 2),
            "cycles": cycle_num,
            "total_agents": total_agents,
            "total_failures": total_failures,
            "total_tokens": total_tokens,
            "total_cost_usd": round(total_cost, 6),
            "avg_cycle_time_s": round(total_elapsed / max(cycle_num, 1), 1),
            "cycles_per_minute": round(cycle_num / max(total_elapsed / 60, 0.1), 2),
            "replay_entries": replay_entries,
            "kernel_rss_mb": round(kernel_rss, 1),
            "state_store_entries": state_size,
        }, f, indent=2)
    print(f"\n  Results: {metrics_path}")
    print(f"  Reports: {REPORTS_DIR}/")


if __name__ == "__main__":
    main()
