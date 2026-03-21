"""High-level agent abstraction with tool decorator and event loop."""

from __future__ import annotations

import time
from typing import Callable

from clove_sdk.client import CloveClient


class Agent:
    """Declarative agent that connects to the CLOVE kernel and runs an
    event loop polling for messages and events.

    Usage::

        agent = Agent("my-agent")

        @agent.tool
        def greet(name: str) -> str:
            return f"Hello, {name}!"

        agent.run()
    """

    def __init__(
        self, name: str, socket_path: str = "/tmp/clove.sock"
    ) -> None:
        self.name = name
        self.client = CloveClient(socket_path)
        self._tools: dict[str, Callable] = {}

    # ── Tool registration ───────────────────────────────────────────

    def tool(self, func: Callable) -> Callable:
        """Decorator to register a function as an agent tool."""
        self._tools[func.__name__] = func
        return func

    # ── Event loop ──────────────────────────────────────────────────

    def run(self) -> None:
        """Connect, register, and enter the main event loop.

        Polls for IPC messages and events. Stops on ``KeyboardInterrupt``.
        """
        self.client.connect()
        self.client.register_name(self.name)
        self.client.hello()

        try:
            while True:
                msgs = self.client.recv_messages(max_count=10)
                for msg in msgs:
                    self._handle_message(msg)

                events = self.client.poll_events(max_events=10)
                for event in events:
                    self._handle_event(event)

                time.sleep(0.1)
        except KeyboardInterrupt:
            pass
        finally:
            self.client.disconnect()

    # ── Overridable handlers ────────────────────────────────────────

    def _handle_message(self, msg: dict) -> None:
        """Process an incoming IPC message.

        Override in a subclass or use the ``@agent.tool`` decorator to
        register named handlers.
        """

    def _handle_event(self, event: dict) -> None:
        """Process an incoming event.  Override in a subclass."""
