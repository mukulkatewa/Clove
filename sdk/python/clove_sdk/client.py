"""Synchronous client for the CLOVE kernel over Unix domain sockets."""

from __future__ import annotations

import json
import socket
from typing import Any

from clove_sdk.protocol import (
    HEADER_SIZE,
    SyscallOp,
    pack_message,
    unpack_header,
)


class CloveClient:
    """Low-level CLOVE kernel client.

    Communicates over a Unix domain socket using the binary IPC protocol
    (17-byte header + JSON payload).
    """

    def __init__(
        self, socket_path: str = "/tmp/clove.sock", agent_id: int = 0
    ) -> None:
        self.socket_path = socket_path
        self.agent_id = agent_id
        self._sock: socket.socket | None = None

    # ── Connection lifecycle ────────────────────────────────────────

    def connect(self) -> bool:
        """Open the Unix socket connection. Returns True on success."""
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.connect(self.socket_path)
        return True

    def disconnect(self) -> None:
        """Close the socket if open."""
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    def __enter__(self) -> CloveClient:
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.disconnect()

    # ── Transport ───────────────────────────────────────────────────

    def _send(self, opcode: int, payload: dict | None = None) -> dict:
        """Send a syscall and wait for the kernel's response.

        Args:
            opcode:  SyscallOp value.
            payload: Dict serialised to JSON for the request body.

        Returns:
            Parsed JSON dict from the kernel response.
        """
        if self._sock is None:
            raise ConnectionError("Not connected — call connect() first")

        payload_str = json.dumps(payload) if payload else ""
        msg = pack_message(self.agent_id, opcode, payload_str)
        self._sock.sendall(msg)

        # Read the 17-byte response header
        header_data = self._recv_exact(HEADER_SIZE)
        _magic, _agent_id, _opcode, payload_size = unpack_header(header_data)

        # Read the response payload
        if payload_size > 0:
            raw = self._recv_exact(payload_size)
            return json.loads(raw.decode("utf-8"))
        return {}

    def _recv_exact(self, n: int) -> bytes:
        """Read exactly *n* bytes from the socket."""
        buf = bytearray()
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Socket closed while reading")
            buf.extend(chunk)
        return bytes(buf)

    # ── Core ────────────────────────────────────────────────────────

    def hello(self) -> dict:
        return self._send(SyscallOp.SYS_HELLO)

    def noop(self) -> dict:
        return self._send(SyscallOp.SYS_NOOP)

    # ── State ───────────────────────────────────────────────────────

    def store(
        self, key: str, value: Any, scope: str = "global", ttl_ms: int = 0
    ) -> dict:
        return self._send(
            SyscallOp.SYS_STORE,
            {"key": key, "value": value, "scope": scope, "ttl_ms": ttl_ms},
        )

    def fetch(self, key: str) -> Any:
        resp = self._send(SyscallOp.SYS_FETCH, {"key": key})
        return resp.get("value")

    def delete(self, key: str) -> dict:
        return self._send(SyscallOp.SYS_DELETE, {"key": key})

    def keys(self, prefix: str = "") -> list[str]:
        resp = self._send(SyscallOp.SYS_KEYS, {"prefix": prefix})
        return resp.get("keys", [])

    # ── IPC ─────────────────────────────────────────────────────────

    def send_message(self, target_id: int, content: str) -> dict:
        return self._send(
            SyscallOp.SYS_SEND, {"target_id": target_id, "content": content}
        )

    def recv_messages(self, max_count: int = 50) -> list[dict]:
        resp = self._send(SyscallOp.SYS_RECV, {"max_count": max_count})
        return resp.get("messages", [])

    def broadcast(self, content: str) -> dict:
        return self._send(SyscallOp.SYS_BROADCAST, {"content": content})

    def register_name(self, name: str) -> dict:
        return self._send(SyscallOp.SYS_REGISTER, {"name": name})

    # ── Events ──────────────────────────────────────────────────────

    def subscribe(self, event_type: str) -> dict:
        return self._send(SyscallOp.SYS_SUBSCRIBE, {"event_type": event_type})

    def unsubscribe(self, event_type: str) -> dict:
        return self._send(
            SyscallOp.SYS_UNSUBSCRIBE, {"event_type": event_type}
        )

    def poll_events(self, max_events: int = 50) -> list[dict]:
        resp = self._send(
            SyscallOp.SYS_POLL_EVENTS, {"max_events": max_events}
        )
        return resp.get("events", [])

    def emit(self, event_type: str, data: dict | None = None) -> dict:
        payload: dict[str, Any] = {"event_type": event_type}
        if data is not None:
            payload["data"] = data
        return self._send(SyscallOp.SYS_EMIT, payload)

    # ── LLM ─────────────────────────────────────────────────────────

    def think(self, prompt: str) -> dict:
        return self._send(SyscallOp.SYS_THINK, {"prompt": prompt})

    def llm_config(self) -> dict:
        return self._send(SyscallOp.SYS_LLM_CONFIG)

    def llm_report(self) -> dict:
        return self._send(SyscallOp.SYS_LLM_REPORT)

    # ── Permissions ─────────────────────────────────────────────────

    def get_permissions(self, agent_id: int | None = None) -> dict:
        payload: dict[str, Any] = {}
        if agent_id is not None:
            payload["agent_id"] = agent_id
        return self._send(SyscallOp.SYS_GET_PERMS, payload or None)

    def set_permissions(
        self, permissions: dict, agent_id: int | None = None
    ) -> dict:
        payload: dict[str, Any] = {"permissions": permissions}
        if agent_id is not None:
            payload["agent_id"] = agent_id
        return self._send(SyscallOp.SYS_SET_PERMS, payload)

    def auth(self, agent_id: int | None = None) -> dict:
        payload: dict[str, Any] = {}
        if agent_id is not None:
            payload["agent_id"] = agent_id
        return self._send(SyscallOp.SYS_AUTH, payload or None)

    # ── Agents ──────────────────────────────────────────────────────

    def spawn(self, name: str, command: str) -> dict:
        return self._send(
            SyscallOp.SYS_SPAWN, {"name": name, "command": command}
        )

    def kill(self, agent_id: int) -> dict:
        return self._send(SyscallOp.SYS_KILL, {"agent_id": agent_id})

    def list_agents(self) -> list[dict]:
        resp = self._send(SyscallOp.SYS_LIST)
        return resp.get("agents", [])

    def pause(self, agent_id: int) -> dict:
        return self._send(SyscallOp.SYS_PAUSE, {"agent_id": agent_id})

    def resume(self, agent_id: int) -> dict:
        return self._send(SyscallOp.SYS_RESUME, {"agent_id": agent_id})

    # ── File I/O ────────────────────────────────────────────────────

    def read_file(self, path: str) -> dict:
        return self._send(SyscallOp.SYS_READ, {"path": path})

    def write_file(self, path: str, content: str) -> dict:
        return self._send(
            SyscallOp.SYS_WRITE, {"path": path, "content": content}
        )

    def exec_command(
        self, command: str, args: list[str] | None = None
    ) -> dict:
        payload: dict[str, Any] = {"command": command}
        if args is not None:
            payload["args"] = args
        return self._send(SyscallOp.SYS_EXEC, payload)

    # ── HTTP ────────────────────────────────────────────────────────

    def http(
        self,
        url: str,
        method: str = "GET",
        body: str | None = None,
        headers: dict | None = None,
    ) -> dict:
        payload: dict[str, Any] = {"url": url, "method": method}
        if body is not None:
            payload["body"] = body
        if headers is not None:
            payload["headers"] = headers
        return self._send(SyscallOp.SYS_HTTP, payload)

    # ── PII ─────────────────────────────────────────────────────────

    def pii_scan(self, text: str) -> dict:
        return self._send(SyscallOp.SYS_PII_SCAN, {"text": text})

    def pii_redact(self, text: str) -> dict:
        return self._send(SyscallOp.SYS_PII_REDACT, {"text": text})

    # ── Metrics ─────────────────────────────────────────────────────

    def system_metrics(self) -> dict:
        return self._send(SyscallOp.SYS_METRICS_SYSTEM)

    def agent_metrics(self, agent_id: int | None = None) -> dict:
        payload: dict[str, Any] = {}
        if agent_id is not None:
            payload["agent_id"] = agent_id
        return self._send(SyscallOp.SYS_METRICS_AGENT, payload or None)

    # ── Audit ───────────────────────────────────────────────────────

    def get_audit_log(
        self, category: str | None = None, limit: int = 100
    ) -> dict:
        payload: dict[str, Any] = {"limit": limit}
        if category is not None:
            payload["category"] = category
        return self._send(SyscallOp.SYS_GET_AUDIT_LOG, payload)

    # ── Recording / Replay ──────────────────────────────────────────

    def record_start(self, max_entries: int = 50000) -> dict:
        return self._send(
            SyscallOp.SYS_RECORD_START, {"max_entries": max_entries}
        )

    def record_stop(self) -> dict:
        return self._send(SyscallOp.SYS_RECORD_STOP)

    def record_status(self) -> dict:
        return self._send(SyscallOp.SYS_RECORD_STATUS)

    def replay_start(self) -> dict:
        return self._send(SyscallOp.SYS_REPLAY_START)

    def replay_status(self) -> dict:
        return self._send(SyscallOp.SYS_REPLAY_STATUS)

    # ── Worlds ──────────────────────────────────────────────────────

    def world_create(
        self, name: str, metadata: dict | None = None
    ) -> dict:
        payload: dict[str, Any] = {"name": name}
        if metadata is not None:
            payload["metadata"] = metadata
        return self._send(SyscallOp.SYS_WORLD_CREATE, payload)

    def world_destroy(self, world_id: int) -> dict:
        return self._send(
            SyscallOp.SYS_WORLD_DESTROY, {"world_id": world_id}
        )

    def world_list(self) -> list:
        resp = self._send(SyscallOp.SYS_WORLD_LIST)
        return resp.get("worlds", [])

    def world_join(self, world_id: int) -> dict:
        return self._send(SyscallOp.SYS_WORLD_JOIN, {"world_id": world_id})

    def world_leave(self, world_id: int) -> dict:
        return self._send(SyscallOp.SYS_WORLD_LEAVE, {"world_id": world_id})

    # ── MCP ─────────────────────────────────────────────────────────

    def mcp_list(self) -> dict:
        return self._send(SyscallOp.SYS_MCP_LIST)

    def mcp_call(
        self, server: str, tool: str, arguments: dict | None = None
    ) -> dict:
        payload: dict[str, Any] = {"server": server, "tool": tool}
        if arguments is not None:
            payload["arguments"] = arguments
        return self._send(SyscallOp.SYS_MCP_CALL, payload)

    # ── Context / Artifacts ──────────────────────────────────────

    def doc_create(
        self,
        type: str,
        title: str,
        content: str,
        chain_id: str,
        parent_ids: list[str] | None = None,
        metadata: dict | None = None,
    ) -> dict:
        payload: dict[str, Any] = {
            "type": type,
            "title": title,
            "content": content,
            "chain_id": chain_id,
        }
        if parent_ids:
            payload["parent_ids"] = parent_ids
        if metadata:
            payload["metadata"] = metadata
        return self._send(SyscallOp.SYS_DOC_CREATE, payload)

    def doc_read(self, id: str) -> dict:
        return self._send(SyscallOp.SYS_DOC_READ, {"id": id})

    def doc_update(
        self,
        id: str,
        content: str | None = None,
        state: str | None = None,
    ) -> dict:
        payload: dict[str, Any] = {"id": id}
        if content is not None:
            payload["content"] = content
        if state is not None:
            payload["state"] = state
        return self._send(SyscallOp.SYS_DOC_UPDATE, payload)

    def doc_list(
        self,
        chain_id: str | None = None,
        type: str | None = None,
        state: str | None = None,
        limit: int = 100,
    ) -> list[dict]:
        payload: dict[str, Any] = {"limit": limit}
        if chain_id:
            payload["chain_id"] = chain_id
        if type:
            payload["type"] = type
        if state:
            payload["state"] = state
        resp = self._send(SyscallOp.SYS_DOC_LIST, payload)
        return resp.get("artifacts", [])

    def doc_delete(self, id: str) -> dict:
        return self._send(SyscallOp.SYS_DOC_DELETE, {"id": id})

    def chain_create(
        self,
        name: str,
        description: str = "",
        metadata: dict | None = None,
    ) -> dict:
        payload: dict[str, Any] = {
            "name": name,
            "description": description,
        }
        if metadata:
            payload["metadata"] = metadata
        return self._send(SyscallOp.SYS_CHAIN_CREATE, payload)

    def chain_get(self, id: str) -> dict:
        return self._send(SyscallOp.SYS_CHAIN_GET, {"id": id})

    def chain_fork(self, chain_id: str, at_artifact_id: str) -> dict:
        return self._send(
            SyscallOp.SYS_CHAIN_FORK,
            {"chain_id": chain_id, "at_artifact_id": at_artifact_id},
        )

    def context_assemble(
        self, chain_id: str, max_tokens: int = 128000
    ) -> dict:
        return self._send(
            SyscallOp.SYS_CONTEXT_ASSEMBLE,
            {"chain_id": chain_id, "max_tokens": max_tokens},
        )

    def think_with_context(
        self,
        prompt: str,
        chain_id: str,
        model: str | None = None,
    ) -> dict:
        payload: dict[str, Any] = {
            "prompt": prompt,
            "chain_id": chain_id,
            "auto_context": True,
        }
        if model:
            payload["model"] = model
        return self._send(SyscallOp.SYS_THINK, payload)

    # ── Memory Blocks ────────────────────────────────────────────

    def mem_create(
        self,
        name: str,
        type: str = "core",
        access: str = "private",
        content: str = "",
        max_tokens: int = 0,
    ) -> dict:
        payload: dict[str, Any] = {
            "name": name,
            "type": type,
            "access": access,
            "content": content,
        }
        if max_tokens > 0:
            payload["max_tokens"] = max_tokens
        return self._send(SyscallOp.SYS_MEM_CREATE, payload)

    def mem_read(self, id: str | None = None, name: str | None = None) -> dict:
        payload: dict[str, Any] = {}
        if id:
            payload["id"] = id
        if name:
            payload["name"] = name
        return self._send(SyscallOp.SYS_MEM_READ, payload)

    def mem_write(self, id: str, content: str) -> dict:
        return self._send(
            SyscallOp.SYS_MEM_WRITE, {"id": id, "content": content}
        )

    def mem_append(self, id: str, content: str) -> dict:
        return self._send(
            SyscallOp.SYS_MEM_APPEND, {"id": id, "content": content}
        )

    def mem_delete(self, id: str) -> dict:
        return self._send(SyscallOp.SYS_MEM_DELETE, {"id": id})

    def mem_list(self, limit: int = 100) -> list[dict]:
        resp = self._send(SyscallOp.SYS_MEM_LIST, {"limit": limit})
        return resp.get("blocks", [])

    def mem_share(self, id: str, target_agent_id: int) -> dict:
        return self._send(
            SyscallOp.SYS_MEM_SHARE,
            {"id": id, "target_agent_id": target_agent_id},
        )

    # ── Budget ───────────────────────────────────────────────────

    def set_budget(
        self,
        agent_id: int | None = None,
        max_tokens: int | None = None,
        max_steps: int | None = None,
        max_time_ms: int | None = None,
        max_cost_usd: float | None = None,
        kill_on_exceeded: bool = False,
        reset: bool = False,
    ) -> dict:
        payload: dict[str, Any] = {}
        if agent_id is not None:
            payload["agent_id"] = agent_id
        if max_tokens is not None:
            payload["max_tokens"] = max_tokens
        if max_steps is not None:
            payload["max_steps"] = max_steps
        if max_time_ms is not None:
            payload["max_time_ms"] = max_time_ms
        if max_cost_usd is not None:
            payload["max_cost_usd"] = max_cost_usd
        if kill_on_exceeded:
            payload["kill_on_exceeded"] = True
        if reset:
            payload["reset"] = True
        return self._send(SyscallOp.SYS_SET_BUDGET, payload)

    def get_budget(self, agent_id: int | None = None) -> dict:
        payload: dict[str, Any] = {}
        if agent_id is not None:
            payload["agent_id"] = agent_id
        return self._send(SyscallOp.SYS_GET_BUDGET, payload or None)

    # ── Priority / Scheduling ────────────────────────────────────

    def set_priority(
        self, priority: str = "normal", agent_id: int | None = None
    ) -> dict:
        payload: dict[str, Any] = {"priority": priority}
        if agent_id is not None:
            payload["agent_id"] = agent_id
        return self._send(SyscallOp.SYS_SET_PRIORITY, payload)

    def get_priority(self, agent_id: int | None = None) -> dict:
        payload: dict[str, Any] = {}
        if agent_id is not None:
            payload["agent_id"] = agent_id
        return self._send(SyscallOp.SYS_GET_PRIORITY, payload or None)
