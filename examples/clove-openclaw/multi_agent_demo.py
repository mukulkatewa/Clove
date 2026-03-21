#!/usr/bin/env python3
"""
Multi-Agent OpenClaw Demo

Shows what's IMPOSSIBLE with NemoClaw/OpenShell but trivial with CLOVE:
Three OpenClaw instances coordinating through kernel IPC.

Agent 1 (researcher): Finds information, stores in shared state
Agent 2 (writer):     Reads state, drafts content, sends to reviewer
Agent 3 (reviewer):   Reviews draft, sends feedback or approves

This demo simulates the coordination using CLOVE's SDK.
In production, each agent would be a real OpenClaw instance.

Usage:
    # Start kernel first
    clove_kernel --api --openrouter --openrouter-key $OPENROUTER_API_KEY &

    # Run demo
    python3 multi_agent_demo.py "Research quantum computing breakthroughs"
"""

import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'sdk', 'python'))

from clove_sdk import CloveClient


def demo_multi_agent(topic: str):
    print("=" * 60)
    print("  CLOVE Multi-Agent Demo")
    print("  3 agents coordinating via kernel IPC")
    print("=" * 60)
    print(f"\n  Topic: {topic}\n")

    client = CloveClient()
    try:
        client.connect()
    except Exception:
        print("  ERROR: CLOVE kernel not running.")
        print("  Start it: clove_kernel --api --openrouter --openrouter-key $OPENROUTER_API_KEY &")
        sys.exit(1)

    hello = client.hello()
    print(f"  Connected to CLOVE kernel v{hello.get('kernel_version', '?')}")

    # Start execution recording (so we can replay later)
    client.record_start()
    print("  Execution recording: ON\n")

    # --- Phase 1: Researcher ---
    print("  [researcher] Starting research...")
    client.register_name("researcher")

    # Store the task
    client.store("task", {"topic": topic, "status": "researching"})

    # Use LLM to research
    research_prompt = f"Research the topic '{topic}'. Provide 3-5 key findings with brief explanations. Be concise."
    research = client.think(research_prompt)

    if research.get("success"):
        findings = research.get("content", "No findings")
        cost = research.get("cost_usd", 0)
        model = research.get("model", "unknown")
        print(f"  [researcher] Found results (model: {model}, cost: ${cost:.4f})")

        # Store findings in shared state
        client.store("findings", {
            "content": findings,
            "model": model,
            "cost_usd": cost,
        })
        client.store("task", {"topic": topic, "status": "writing"})

        # Notify writer via event
        client.emit("CUSTOM", {"event": "research_complete", "agent": "researcher"})
    else:
        print(f"  [researcher] LLM failed: {research.get('error', 'unknown')}")
        print("  Make sure --openrouter and --openrouter-key are set.")
        client.disconnect()
        sys.exit(1)

    # --- Phase 2: Writer ---
    print("\n  [writer] Drafting content...")

    # Read findings from shared state
    findings_data = client.fetch("findings")
    if not findings_data:
        print("  [writer] ERROR: No findings in state store")
        client.disconnect()
        sys.exit(1)

    findings_content = findings_data.get("content", "") if isinstance(findings_data, dict) else str(findings_data)

    # Use LLM to write
    write_prompt = f"Based on these research findings, write a clear, well-structured summary:\n\n{findings_content}\n\nWrite 2-3 paragraphs. Be professional and concise."
    draft = client.think(write_prompt)

    if draft.get("success"):
        draft_content = draft.get("content", "")
        cost = draft.get("cost_usd", 0)
        print(f"  [writer] Draft complete (cost: ${cost:.4f})")

        # Store draft
        client.store("draft", {
            "content": draft_content,
            "cost_usd": cost,
        })
        client.store("task", {"topic": topic, "status": "reviewing"})

        # Send to reviewer via IPC
        client.emit("CUSTOM", {"event": "draft_ready", "agent": "writer"})
    else:
        print(f"  [writer] LLM failed: {draft.get('error', 'unknown')}")

    # --- Phase 3: Reviewer ---
    print("\n  [reviewer] Reviewing draft...")

    draft_data = client.fetch("draft")
    if not draft_data:
        print("  [reviewer] ERROR: No draft in state store")
        client.disconnect()
        sys.exit(1)

    draft_text = draft_data.get("content", "") if isinstance(draft_data, dict) else str(draft_data)

    # Use LLM to review
    review_prompt = f"Review this draft for accuracy, clarity, and completeness. Provide a brief assessment (2-3 sentences) and a quality score (1-10):\n\n{draft_text}"
    review = client.think(review_prompt)

    if review.get("success"):
        review_content = review.get("content", "")
        cost = review.get("cost_usd", 0)
        print(f"  [reviewer] Review complete (cost: ${cost:.4f})")

        # Store review
        client.store("review", {
            "content": review_content,
            "cost_usd": cost,
        })
        client.store("task", {"topic": topic, "status": "complete"})
        client.emit("CUSTOM", {"event": "review_complete", "agent": "reviewer"})

    # --- Summary ---
    print("\n" + "=" * 60)
    print("  Results")
    print("=" * 60)

    # Get total costs
    findings_cost = 0
    draft_cost = 0
    review_cost = 0

    f = client.fetch("findings")
    if isinstance(f, dict):
        findings_cost = f.get("cost_usd", 0)
    d = client.fetch("draft")
    if isinstance(d, dict):
        draft_cost = d.get("cost_usd", 0)
    r = client.fetch("review")
    if isinstance(r, dict):
        review_cost = r.get("cost_usd", 0)

    total_cost = findings_cost + draft_cost + review_cost

    print(f"\n  Topic: {topic}")
    print(f"  Agents: 3 (researcher, writer, reviewer)")
    print(f"  Total LLM cost: ${total_cost:.4f}")
    print(f"    - Research: ${findings_cost:.4f}")
    print(f"    - Writing:  ${draft_cost:.4f}")
    print(f"    - Review:   ${review_cost:.4f}")

    # Print the final draft
    if isinstance(d, dict) and d.get("content"):
        print(f"\n  --- Draft ---")
        for line in d["content"].split('\n'):
            print(f"  {line}")

    if isinstance(r, dict) and r.get("content"):
        print(f"\n  --- Review ---")
        for line in r["content"].split('\n'):
            print(f"  {line}")

    # Stop recording
    client.record_stop()
    rec_status = client.record_status()
    print(f"\n  Execution recording: {rec_status.get('entry_count', 0)} entries captured")
    print(f"  Replay with: clove record status")

    # Audit trail
    audit = client.get_audit_log(limit=5)
    entries = audit.get("entries", [])
    if entries:
        print(f"\n  Recent audit entries: {len(entries)}")

    print(f"\n  Dashboard: http://localhost:8080/dashboard")
    print(f"  Full audit: clove audit")

    client.disconnect()


if __name__ == '__main__':
    if len(sys.argv) < 2:
        topic = "Recent breakthroughs in AI agent safety"
    else:
        topic = ' '.join(sys.argv[1:])

    demo_multi_agent(topic)
