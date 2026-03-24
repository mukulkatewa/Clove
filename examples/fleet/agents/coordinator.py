#!/usr/bin/env python3
"""
Coordinator Agent — assigns research tasks, tracks progress, triggers phases.

Runs continuously. Every 3 minutes, picks a new topic from the queue,
assigns subtopics to researchers, and kicks off the pipeline.
"""

import json
import time
from base import FleetAgent

TOPICS = [
    "How AI agents are transforming cybersecurity in 2026",
    "The economics of running thousand-agent fleets",
    "Multi-agent systems for scientific discovery",
    "Privacy-preserving autonomous AI agents",
    "AI agent sandboxing: containers vs kernels",
    "Real-time coordination patterns for agent swarms",
    "The regulatory landscape for autonomous AI (EU AI Act)",
    "Building fault-tolerant agent pipelines",
    "AI agents in financial services: risk and compliance",
    "The future of human-agent collaboration",
]


class Coordinator(FleetAgent):
    def __init__(self):
        super().__init__("coordinator", "coordinator")
        self.current_topic = None
        self.current_cycle = 0
        self.phase = "idle"  # idle → researching → writing → reviewing → synthesizing → done
        self.phase_start = 0
        self.completed_research = 0
        self.completed_drafts = 0
        self.completed_reviews = 0
        self.last_cycle_time = 0

    def on_tick(self):
        # Start a new cycle if idle
        if self.phase == "idle" or self.phase == "done":
            if self.current_cycle < len(TOPICS):
                self.start_new_cycle()
            elif self.phase == "done":
                self.running = False  # all topics done

        # Check for phase timeouts (60s max per phase)
        if self.phase not in ("idle", "done") and time.monotonic() - self.phase_start > 60:
            self.log(f"Phase '{self.phase}' timed out, moving on")
            self.advance_phase()

    def start_new_cycle(self):
        self.current_cycle += 1
        self.current_topic = TOPICS[(self.current_cycle - 1) % len(TOPICS)]
        self.phase = "researching"
        self.phase_start = time.monotonic()
        self.completed_research = 0
        self.completed_drafts = 0
        self.completed_reviews = 0

        self.log(f"=== Cycle {self.current_cycle}: {self.current_topic[:50]}... ===")

        # Store the task
        self.store("current_task", {
            "cycle": self.current_cycle,
            "topic": self.current_topic,
            "phase": "researching",
            "subtopics": [
                f"{self.current_topic} — current state",
                f"{self.current_topic} — key challenges",
                f"{self.current_topic} — future directions",
            ]
        })

        # Signal researchers to start
        self.emit({
            "type": "new_task",
            "cycle": self.current_cycle,
            "topic": self.current_topic,
            "phase": "researching",
        })

    def on_event(self, evt):
        data = evt if isinstance(evt, dict) else {}
        if isinstance(data.get("data"), dict):
            data = data["data"]

        evt_type = data.get("type", "")

        if evt_type == "research_done":
            self.completed_research += 1
            self.log(f"Research complete ({self.completed_research}/3)")
            if self.completed_research >= 3:
                self.advance_phase()

        elif evt_type == "draft_done":
            self.completed_drafts += 1
            self.log(f"Draft complete ({self.completed_drafts}/3)")
            if self.completed_drafts >= 3:
                self.advance_phase()

        elif evt_type == "review_done":
            self.completed_reviews += 1
            self.log(f"Review complete ({self.completed_reviews}/2)")
            if self.completed_reviews >= 2:
                self.advance_phase()

        elif evt_type == "synthesis_done":
            cycle_time = time.monotonic() - self.phase_start
            self.log(f"Cycle {self.current_cycle} COMPLETE")
            self.phase = "done"

    def advance_phase(self):
        if self.phase == "researching":
            self.phase = "writing"
            self.phase_start = time.monotonic()
            self.store("current_task", {
                "cycle": self.current_cycle,
                "topic": self.current_topic,
                "phase": "writing",
            })
            self.emit({"type": "phase_change", "phase": "writing", "cycle": self.current_cycle})
            self.log("Phase → writing")

        elif self.phase == "writing":
            self.phase = "reviewing"
            self.phase_start = time.monotonic()
            self.store("current_task", {
                "cycle": self.current_cycle,
                "topic": self.current_topic,
                "phase": "reviewing",
            })
            self.emit({"type": "phase_change", "phase": "reviewing", "cycle": self.current_cycle})
            self.log("Phase → reviewing")

        elif self.phase == "reviewing":
            self.phase = "synthesizing"
            self.phase_start = time.monotonic()
            self.emit({"type": "phase_change", "phase": "synthesizing", "cycle": self.current_cycle})
            self.log("Phase → synthesizing")

        elif self.phase == "synthesizing":
            self.phase = "done"
            self.emit({"type": "cycle_complete", "cycle": self.current_cycle})
            self.log("Phase → done")


if __name__ == "__main__":
    Coordinator().start()
