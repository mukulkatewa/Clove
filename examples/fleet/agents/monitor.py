#!/usr/bin/env python3
"""
Monitor Agent — tracks fleet health, costs, and performance metrics.
Logs status every 30 seconds.
"""

import json
import time
import datetime
from pathlib import Path
from base import FleetAgent


class Monitor(FleetAgent):
    def __init__(self):
        super().__init__("monitor", "monitor")
        self.last_report = 0
        self.events_seen = 0
        self.cycles_completed = 0

    def on_event(self, evt):
        self.events_seen += 1
        data = evt if isinstance(evt, dict) else {}
        if isinstance(data.get("data"), dict):
            data = data["data"]

        if data.get("type") == "cycle_complete":
            self.cycles_completed += 1

    def on_tick(self):
        # Report every 30 seconds
        now = time.monotonic()
        if now - self.last_report < 30:
            return
        self.last_report = now

        # Get kernel metrics
        try:
            metrics = self.client.system_metrics()
            rec = self.client.record_status()

            rss_mb = metrics.get("rss_bytes", 0) / 1024 / 1024
            state = metrics.get("state_store_size", 0)
            llm_req = metrics.get("llm_total_requests", 0)
            llm_done = metrics.get("llm_total_completed", 0)
            replay = rec.get("entry_count", 0)

            self.log(
                f"STATUS: RSS={rss_mb:.1f}MB | state={state} entries | "
                f"LLM={llm_done}/{llm_req} | replay={replay} entries | "
                f"events={self.events_seen} | cycles={self.cycles_completed}"
            )
        except Exception as e:
            self.log(f"Metrics error: {e}")


if __name__ == "__main__":
    Monitor().start()
