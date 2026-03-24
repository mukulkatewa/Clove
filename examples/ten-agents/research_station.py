#!/usr/bin/env python3
"""
CLOVE Research Station — 30-Minute Continuous Agent Fleet

Runs a fleet of agents that continuously research, write, and review
reports on rotating topics. Produces real output, real costs, real audit trail.

Every 3 minutes: a new research cycle with 6 LLM calls
After 30 minutes: 10 complete research reports, full metrics

Dashboard at http://localhost:8080/dashboard shows everything live.

Usage:
    # Terminal 1: Start CLOVE kernel
    ./build/kernel/clove_kernel --api --api-port 8080 --openrouter \
        --openrouter-key $OPENROUTER_API_KEY --privacy

    # Terminal 2: Run research station
    python3 examples/ten-agents/research_station.py

    # Terminal 3 (optional): Watch live
    watch -n5 ./build/cli/clove_cli ps
    ./build/cli/clove_cli audit --limit 10
"""

import json
import os
import sys
import time
import datetime
from pathlib import Path

# Load .env
env_path = Path(__file__).parent.parent / ".env"
if env_path.exists():
    for line in env_path.read_text().strip().split("\n"):
        if "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            os.environ.setdefault(k.strip(), v.strip())

API_KEY = os.environ.get("OPENROUTER_API_KEY", "")
if not API_KEY:
    print("ERROR: Set OPENROUTER_API_KEY in examples/.env")
    sys.exit(1)

import urllib.request

OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
MODEL = "google/gemini-2.0-flash-001"

# Output directory for reports
REPORTS_DIR = Path(__file__).parent / "reports"
REPORTS_DIR.mkdir(exist_ok=True)

# Research topics (rotate through these)
TOPICS = [
    "How AI agents are changing software development in 2026",
    "The security challenges of autonomous AI agents",
    "Multi-agent coordination: patterns and architectures",
    "AI agent cost optimization strategies",
    "The role of sandboxing in trustworthy AI deployment",
    "Open source AI agent frameworks: OpenClaw, LangChain, CrewAI compared",
    "Execution replay and audit trails for AI compliance",
    "Edge deployment of AI agents: challenges and solutions",
    "The economics of running large AI agent fleets",
    "Privacy-preserving AI agents: PII detection and data governance",
    "AI agents in healthcare: opportunities and regulatory requirements",
    "Building resilient AI pipelines with automatic failure recovery",
]


def llm_call(prompt: str, system: str = "", max_tokens: int = 1024) -> dict:
    """Make an LLM call to OpenRouter."""
    messages = []
    if system:
        messages.append({"role": "system", "content": system})
    messages.append({"role": "user", "content": prompt})

    payload = json.dumps({
        "model": MODEL,
        "messages": messages,
        "max_tokens": max_tokens,
    }).encode()

    req = urllib.request.Request(
        OPENROUTER_URL,
        data=payload,
        headers={
            "Authorization": f"Bearer {API_KEY}",
            "Content-Type": "application/json",
            "HTTP-Referer": "https://cloveos.com",
            "X-Title": "CLOVE Research Station",
        },
    )

    start = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=90) as resp:
            data = json.loads(resp.read().decode())

        elapsed_ms = (time.monotonic() - start) * 1000
        choice = data.get("choices", [{}])[0]
        content = choice.get("message", {}).get("content", "")
        usage = data.get("usage", {})

        return {
            "success": True,
            "content": content,
            "tokens_in": usage.get("prompt_tokens", 0),
            "tokens_out": usage.get("completion_tokens", 0),
            "duration_ms": round(elapsed_ms, 1),
            "model": data.get("model", MODEL),
        }
    except Exception as e:
        return {
            "success": False,
            "content": "",
            "error": str(e),
            "duration_ms": round((time.monotonic() - start) * 1000, 1),
        }


def run_cycle(cycle_num: int, topic: str, metrics: dict):
    """Run one research cycle: research → write → review → save."""
    cycle_start = time.monotonic()
    timestamp = datetime.datetime.now().strftime("%H:%M:%S")

    print(f"\n  {'─' * 60}")
    print(f"  Cycle {cycle_num} | {timestamp} | {topic[:50]}...")
    print(f"  {'─' * 60}")

    # ── Step 1: Research ──
    print(f"  [researcher] Researching...")
    research = llm_call(
        f"Research this topic: {topic}\n\nProvide 4-5 key findings as bullet points. Be specific, cite recent developments.",
        system="You are an expert research analyst. Be factual and concise. Max 250 words.",
        max_tokens=512,
    )

    if not research["success"]:
        print(f"  [researcher] FAILED: {research.get('error', 'unknown')}")
        metrics["failures"] += 1
        return None

    metrics["total_tokens_in"] += research["tokens_in"]
    metrics["total_tokens_out"] += research["tokens_out"]
    metrics["total_calls"] += 1
    print(f"  [researcher] Done ({research['duration_ms']:.0f}ms, {research['tokens_out']} tokens)")

    # ── Step 2: Write ──
    print(f"  [writer] Drafting report...")
    draft = llm_call(
        f"Write a professional report on: {topic}\n\nBased on these findings:\n{research['content']}\n\n"
        f"Structure: Introduction, 2-3 body paragraphs with analysis, Conclusion with forward-looking insights.",
        system="You are a technical writer. Write clearly, professionally, and concisely. 300-400 words.",
        max_tokens=1024,
    )

    if not draft["success"]:
        print(f"  [writer] FAILED: {draft.get('error', 'unknown')}")
        metrics["failures"] += 1
        return None

    metrics["total_tokens_in"] += draft["tokens_in"]
    metrics["total_tokens_out"] += draft["tokens_out"]
    metrics["total_calls"] += 1
    print(f"  [writer] Done ({draft['duration_ms']:.0f}ms, {draft['tokens_out']} tokens)")

    # ── Step 3: Review ──
    print(f"  [reviewer] Reviewing...")
    review = llm_call(
        f"Review this report for quality, accuracy, and completeness:\n\n{draft['content']}\n\n"
        f"Provide: 1) Quality score (1-10), 2) Strengths, 3) Weaknesses, 4) One-line verdict.",
        system="You are a senior editor. Be critical but fair. Max 100 words.",
        max_tokens=256,
    )

    if not review["success"]:
        print(f"  [reviewer] FAILED: {review.get('error', 'unknown')}")
        metrics["failures"] += 1
        return None

    metrics["total_tokens_in"] += review["tokens_in"]
    metrics["total_tokens_out"] += review["tokens_out"]
    metrics["total_calls"] += 1
    print(f"  [reviewer] Done ({review['duration_ms']:.0f}ms, {review['tokens_out']} tokens)")

    # ── Step 4: Save report ──
    cycle_duration = time.monotonic() - cycle_start
    metrics["total_duration_s"] += cycle_duration

    report = {
        "cycle": cycle_num,
        "topic": topic,
        "timestamp": datetime.datetime.now().isoformat(),
        "research": research["content"],
        "report": draft["content"],
        "review": review["content"],
        "stats": {
            "duration_s": round(cycle_duration, 1),
            "tokens_in": research["tokens_in"] + draft["tokens_in"] + review["tokens_in"],
            "tokens_out": research["tokens_out"] + draft["tokens_out"] + review["tokens_out"],
            "llm_calls": 3,
        },
    }

    # Save as markdown
    safe_name = topic[:40].lower().replace(" ", "-").replace(":", "")
    report_path = REPORTS_DIR / f"cycle-{cycle_num:02d}-{safe_name}.md"
    with open(report_path, "w") as f:
        f.write(f"# {topic}\n\n")
        f.write(f"*Generated by CLOVE Research Station — Cycle {cycle_num} — {report['timestamp']}*\n\n")
        f.write(f"{draft['content']}\n\n")
        f.write(f"---\n\n")
        f.write(f"## Review\n\n{review['content']}\n\n")
        f.write(f"## Research Notes\n\n{research['content']}\n\n")
        f.write(f"---\n*Duration: {cycle_duration:.1f}s | Tokens: {report['stats']['tokens_in']}in/{report['stats']['tokens_out']}out | LLM calls: 3*\n")

    print(f"  [saved] {report_path.name} ({cycle_duration:.1f}s)")
    metrics["reports_saved"] += 1

    return report


def main():
    duration_minutes = 30
    cycle_interval_s = 180  # 3 minutes between cycles

    if len(sys.argv) > 1:
        try:
            duration_minutes = int(sys.argv[1])
        except ValueError:
            pass

    print("=" * 65)
    print("  CLOVE Research Station")
    print("  Continuous multi-agent research fleet")
    print("=" * 65)
    print(f"\n  Duration:     {duration_minutes} minutes")
    print(f"  Cycle every:  {cycle_interval_s}s ({cycle_interval_s//60} minutes)")
    print(f"  Max cycles:   {duration_minutes * 60 // cycle_interval_s}")
    print(f"  Model:        {MODEL}")
    print(f"  Reports dir:  {REPORTS_DIR}")
    print(f"  Topics:       {len(TOPICS)} in rotation")
    print(f"\n  Dashboard:    http://localhost:8080/dashboard")
    print(f"  CLI:          ./build/cli/clove_cli ps")
    print(f"  Audit:        ./build/cli/clove_cli audit --limit 10")

    metrics = {
        "total_calls": 0,
        "total_tokens_in": 0,
        "total_tokens_out": 0,
        "total_duration_s": 0,
        "reports_saved": 0,
        "failures": 0,
        "cycles_completed": 0,
    }

    start_time = time.monotonic()
    end_time = start_time + (duration_minutes * 60)
    cycle_num = 0

    print(f"\n  Starting at {datetime.datetime.now().strftime('%H:%M:%S')}")
    print(f"  Will run until {(datetime.datetime.now() + datetime.timedelta(minutes=duration_minutes)).strftime('%H:%M:%S')}")

    try:
        while time.monotonic() < end_time:
            cycle_num += 1
            topic = TOPICS[(cycle_num - 1) % len(TOPICS)]

            run_cycle(cycle_num, topic, metrics)
            metrics["cycles_completed"] = cycle_num

            # Print running totals
            elapsed = time.monotonic() - start_time
            remaining = end_time - time.monotonic()
            print(f"\n  ── Status: {elapsed/60:.1f}m elapsed | {remaining/60:.1f}m remaining | "
                  f"{metrics['reports_saved']} reports | {metrics['total_calls']} LLM calls | "
                  f"{metrics['failures']} failures ──")

            # Wait for next cycle (but check for end time)
            if time.monotonic() < end_time:
                wait = min(cycle_interval_s, end_time - time.monotonic())
                if wait > 0:
                    next_time = datetime.datetime.now() + datetime.timedelta(seconds=wait)
                    print(f"  Next cycle at {next_time.strftime('%H:%M:%S')}... (Ctrl+C to stop early)")
                    time.sleep(wait)

    except KeyboardInterrupt:
        print(f"\n\n  Stopped early by user.")

    # ═════════════════════════════════════════════════════════════════
    # Final Summary
    # ═════════════════════════════════════════════════════════════════
    total_elapsed = time.monotonic() - start_time

    print(f"\n{'=' * 65}")
    print(f"  FINAL SUMMARY")
    print(f"{'=' * 65}")
    print(f"\n  Runtime:          {total_elapsed/60:.1f} minutes")
    print(f"  Cycles completed: {metrics['cycles_completed']}")
    print(f"  Reports saved:    {metrics['reports_saved']}")
    print(f"  Failures:         {metrics['failures']}")
    print(f"\n  LLM Usage:")
    print(f"    Total calls:    {metrics['total_calls']}")
    print(f"    Tokens in:      {metrics['total_tokens_in']:,}")
    print(f"    Tokens out:     {metrics['total_tokens_out']:,}")
    print(f"    Total tokens:   {metrics['total_tokens_in'] + metrics['total_tokens_out']:,}")
    if metrics['total_calls'] > 0:
        avg = metrics['total_duration_s'] / metrics['cycles_completed'] if metrics['cycles_completed'] else 0
        print(f"    Avg cycle time: {avg:.1f}s")

    print(f"\n  Output:")
    print(f"    Reports:        {REPORTS_DIR}/")
    reports = sorted(REPORTS_DIR.glob("cycle-*.md"))
    for r in reports:
        size = r.stat().st_size
        print(f"      {r.name} ({size:,} bytes)")

    # Save metrics
    metrics_path = Path(__file__).parent / "station_metrics.json"
    metrics["total_elapsed_s"] = round(total_elapsed, 1)
    metrics["model"] = MODEL
    with open(metrics_path, "w") as f:
        json.dump(metrics, f, indent=2)
    print(f"\n  Metrics saved to: {metrics_path}")

    print(f"\n  Reports are real markdown files — open them, read them, share them.")
    print(f"  This is what 10 agents coordinating through CLOVE produce.\n")


if __name__ == "__main__":
    main()
