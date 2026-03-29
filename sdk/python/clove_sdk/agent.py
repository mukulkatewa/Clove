"""High-level agent abstraction with tool decorator and event loop."""

from __future__ import annotations

import time
from typing import Any, Callable

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
        self,
        name: str,
        role: str = "",
        socket_path: str = "/tmp/clove.sock",
        poll_interval: float = 0.1,
    ) -> None:
        self.name = name
        self.role = role
        self.client = CloveClient(socket_path)
        self.poll_interval = poll_interval
        self._tools: dict[str, Callable] = {}
        self._running = False

    # ── Tool registration ───────────────────────────────────────────

    def tool(self, func: Callable) -> Callable:
        """Decorator to register a function as an agent tool."""
        self._tools[func.__name__] = func
        return func

    def get_tools(self) -> list[str]:
        return list(self._tools.keys())

    def call_tool(self, name: str, *args: Any, **kwargs: Any) -> Any:
        fn = self._tools.get(name)
        if fn is None:
            raise ValueError(f"Unknown tool: {name}")
        return fn(*args, **kwargs)

    # ── Event loop ──────────────────────────────────────────────────

    def run(self) -> None:
        self.client.connect()
        self.client.register_name(self.name)
        self.client.hello()
        self._running = True

        try:
            while self._running:
                msgs = self.client.recv_messages(max_count=10)
                for msg in msgs:
                    self.on_message(msg)

                events = self.client.poll_events(max_events=10)
                for event in events:
                    self.on_event(event)

                self.on_tick()
                time.sleep(self.poll_interval)
        except KeyboardInterrupt:
            pass
        finally:
            self._running = False
            self.client.disconnect()

    def stop(self) -> None:
        self._running = False

    # ── Convenience methods ─────────────────────────────────────────

    def think(self, prompt: str) -> dict:
        return self.client.think(prompt)

    def store(self, key: str, value: Any) -> dict:
        return self.client.store(key, value)

    def fetch(self, key: str) -> Any:
        return self.client.fetch(key)

    def emit(self, event_type: str, data: dict | None = None) -> dict:
        return self.client.emit(event_type, data or {})

    def send_to(self, agent_id: int, content: str) -> dict:
        return self.client.send_message(agent_id, content)

    def broadcast(self, content: str) -> dict:
        return self.client.broadcast(content)

    def remember(self, name: str, content: str, block_type: str = "recall") -> dict:
        return self.client.mem_create(name, content, block_type=block_type)

    def recall(self, query: str, top_k: int = 5) -> list:
        return self.client.mem_search(query, top_k=top_k)

    # ── Override points ─────────────────────────────────────────────

    def on_message(self, msg: dict) -> None:
        """Called for each incoming IPC message. Override in subclass."""

    def on_event(self, event: dict) -> None:
        """Called for each incoming event. Override in subclass."""

    def on_tick(self) -> None:
        """Called every polling cycle. Override for periodic work."""
