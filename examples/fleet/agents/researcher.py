#!/usr/bin/env python3
"""
Researcher Agent — waits for research tasks, calls LLM, stores findings.
Multiple instances run in parallel (researcher-1, researcher-2, researcher-3).
"""

import json
import sys
import time
from base import FleetAgent


class Researcher(FleetAgent):
    def __init__(self, agent_num: int):
        super().__init__(f"researcher-{agent_num}", "researcher")
        self.agent_num = agent_num  # 1, 2, or 3 — determines which subtopic
        self.current_cycle = 0
        self.working = False

    def on_event(self, evt):
        data = evt if isinstance(evt, dict) else {}
        if isinstance(data.get("data"), dict):
            data = data["data"]

        if data.get("type") == "new_task" and not self.working:
            cycle = data.get("cycle", 0)
            if cycle > self.current_cycle:
                self.current_cycle = cycle
                self.do_research()

    def do_research(self):
        self.working = True

        # Get task details from shared state
        task = self.fetch("current_task")
        if not task or not isinstance(task, dict):
            self.log("No task found in state store")
            self.working = False
            return

        subtopics = task.get("subtopics", [])
        idx = self.agent_num - 1
        if idx >= len(subtopics):
            self.working = False
            return

        subtopic = subtopics[idx]
        self.log(f"Researching: {subtopic[:50]}...")

        start = time.monotonic()
        resp = self.think(
            f"Research this topic concisely: {subtopic}. "
            f"Provide 3-4 key findings as bullet points. Be specific. Max 150 words."
        )
        elapsed = (time.monotonic() - start) * 1000

        if resp.get("success"):
            # Store findings in kernel state
            self.store(f"cycle{self.current_cycle}:research:{idx}", {
                "subtopic": subtopic,
                "content": resp["content"],
                "tokens": resp.get("tokens", 0),
                "cost_usd": resp.get("cost_usd", 0),
                "duration_ms": round(elapsed, 1),
            })
            self.log(f"Done ({elapsed:.0f}ms, {resp.get('tokens', 0)} tokens)")

            # Signal coordinator
            self.emit({"type": "research_done", "agent": self.name, "cycle": self.current_cycle})
        else:
            self.log(f"FAILED: {resp.get('error', 'unknown')}")

        self.working = False


if __name__ == "__main__":
    num = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    Researcher(num).start()
