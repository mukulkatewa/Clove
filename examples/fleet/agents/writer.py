#!/usr/bin/env python3
"""
Writer Agent — waits for writing phase, reads research from state, drafts sections.
"""

import json
import sys
import time
from base import FleetAgent


class Writer(FleetAgent):
    def __init__(self, agent_num: int):
        super().__init__(f"writer-{agent_num}", "writer")
        self.agent_num = agent_num
        self.current_cycle = 0
        self.working = False

    def on_event(self, evt):
        data = evt if isinstance(evt, dict) else {}
        if isinstance(data.get("data"), dict):
            data = data["data"]

        if data.get("type") == "phase_change" and data.get("phase") == "writing" and not self.working:
            cycle = data.get("cycle", 0)
            if cycle > self.current_cycle:
                self.current_cycle = cycle
                self.do_write()

    def do_write(self):
        self.working = True
        idx = self.agent_num - 1

        # Read research from kernel state
        research = self.fetch(f"cycle{self.current_cycle}:research:{idx}")
        if not research or not isinstance(research, dict):
            self.log(f"No research found for index {idx}")
            self.working = False
            return

        findings = research.get("content", "")
        subtopic = research.get("subtopic", "unknown")
        self.log(f"Writing about: {subtopic[:50]}...")

        start = time.monotonic()
        resp = self.think(
            f"Write a clear, professional paragraph about: {subtopic}\n\n"
            f"Based on these research findings:\n{findings}\n\n"
            f"Max 150 words. Be specific and insightful."
        )
        elapsed = (time.monotonic() - start) * 1000

        if resp.get("success"):
            self.store(f"cycle{self.current_cycle}:draft:{idx}", {
                "subtopic": subtopic,
                "content": resp["content"],
                "tokens": resp.get("tokens", 0),
                "cost_usd": resp.get("cost_usd", 0),
            })
            self.log(f"Done ({elapsed:.0f}ms, {resp.get('tokens', 0)} tokens)")
            self.emit({"type": "draft_done", "agent": self.name, "cycle": self.current_cycle})
        else:
            self.log(f"FAILED: {resp.get('error', 'unknown')}")

        self.working = False


if __name__ == "__main__":
    num = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    Writer(num).start()
