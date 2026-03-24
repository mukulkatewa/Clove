#!/usr/bin/env python3
"""
Reviewer Agent — waits for review phase, reads drafts, provides feedback.
"""

import json
import sys
import time
from base import FleetAgent


class Reviewer(FleetAgent):
    def __init__(self, agent_num: int):
        super().__init__(f"reviewer-{agent_num}", "reviewer")
        self.agent_num = agent_num
        self.current_cycle = 0
        self.working = False

    def on_event(self, evt):
        data = evt if isinstance(evt, dict) else {}
        if isinstance(data.get("data"), dict):
            data = data["data"]

        if data.get("type") == "phase_change" and data.get("phase") == "reviewing" and not self.working:
            cycle = data.get("cycle", 0)
            if cycle > self.current_cycle:
                self.current_cycle = cycle
                self.do_review()

    def do_review(self):
        self.working = True

        # Reviewer 1 reviews drafts 0 and 1, reviewer 2 reviews draft 2
        if self.agent_num == 1:
            indices = [0, 1]
        else:
            indices = [2]

        for idx in indices:
            draft = self.fetch(f"cycle{self.current_cycle}:draft:{idx}")
            if not draft or not isinstance(draft, dict):
                self.log(f"No draft found for index {idx}")
                continue

            content = draft.get("content", "")
            subtopic = draft.get("subtopic", "unknown")
            self.log(f"Reviewing: {subtopic[:40]}...")

            # PII scan before review
            pii = self.client.pii_scan(content)
            pii_count = pii.get("count", 0) if isinstance(pii, dict) else 0
            if pii_count > 0:
                self.log(f"PII detected in draft: {pii_count} matches")

            start = time.monotonic()
            resp = self.think(
                f"Review this draft. Rate 1-10. One sentence on quality. One on improvement.\n\n{content}"
            )
            elapsed = (time.monotonic() - start) * 1000

            if resp.get("success"):
                self.store(f"cycle{self.current_cycle}:review:{idx}", {
                    "content": resp["content"],
                    "tokens": resp.get("tokens", 0),
                    "pii_found": pii_count,
                })
                self.log(f"Reviewed ({elapsed:.0f}ms)")
            else:
                self.log(f"FAILED: {resp.get('error', 'unknown')}")

        self.emit({"type": "review_done", "agent": self.name, "cycle": self.current_cycle})
        self.working = False


if __name__ == "__main__":
    num = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    Reviewer(num).start()
