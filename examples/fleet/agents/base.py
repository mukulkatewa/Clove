"""Base agent class — every agent process imports this."""

import json
import os
import sys
import time
import signal
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent.parent.parent / "sdk" / "python"))
from clove_sdk.client import CloveClient


class FleetAgent:
    """
    Base class for a CLOVE fleet agent.
    Each agent runs as its own process, connects to the kernel,
    and loops: check messages → check events → do work → repeat.
    """

    def __init__(self, name: str, role: str):
        self.name = name
        self.role = role
        self.client = CloveClient()
        self.running = True
        self.cycle_count = 0
        signal.signal(signal.SIGTERM, self._shutdown)
        signal.signal(signal.SIGINT, self._shutdown)

    def _shutdown(self, *args):
        self.running = False

    def log(self, msg: str):
        ts = time.strftime("%H:%M:%S")
        print(f"  [{ts}] [{self.name}] {msg}", flush=True)

    def start(self):
        """Connect to kernel, register, and enter main loop."""
        self.client.connect()
        self.client.register_name(self.name)
        hello = self.client.hello()
        self.log(f"Connected to kernel v{hello.get('kernel_version', '?')}")

        # Subscribe to events
        self.client.subscribe("CUSTOM")
        self.client.subscribe("STATE_CHANGED")
        self.log(f"Ready (role: {self.role})")

        try:
            while self.running:
                self.cycle_count += 1

                # Check messages
                msgs = self.client.recv_messages(max_count=10)
                if isinstance(msgs, list):
                    for msg in msgs:
                        self.on_message(msg)
                elif isinstance(msgs, dict) and msgs.get("messages"):
                    for msg in msgs["messages"]:
                        self.on_message(msg)

                # Check events
                events = self.client.poll_events(max_events=10)
                if isinstance(events, list):
                    for evt in events:
                        self.on_event(evt)
                elif isinstance(events, dict) and events.get("events"):
                    for evt in events["events"]:
                        self.on_event(evt)

                # Agent-specific work
                self.on_tick()

                # Don't hammer the kernel — small sleep between polls
                time.sleep(0.5)

        except KeyboardInterrupt:
            pass
        except Exception as e:
            self.log(f"ERROR: {e}")
        finally:
            self.log("Shutting down")
            self.client.disconnect()

    def think(self, prompt: str) -> dict:
        """Make an LLM call through the kernel."""
        return self.client.think(prompt)

    def store(self, key: str, value):
        """Store value in kernel state."""
        self.client.store(key, value)

    def fetch(self, key: str):
        """Fetch value from kernel state."""
        return self.client.fetch(key)

    def emit(self, event_data: dict):
        """Emit a CUSTOM event through the kernel event bus."""
        self.client.emit("CUSTOM", event_data)

    def send_to(self, agent_id: int, content: str):
        """Send an IPC message to another agent."""
        self.client.send_message(target_id=agent_id, content=content)

    # Override these in subclasses
    def on_message(self, msg):
        pass

    def on_event(self, evt):
        pass

    def on_tick(self):
        pass
