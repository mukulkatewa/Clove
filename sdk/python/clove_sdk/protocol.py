"""Binary IPC protocol for CLOVE kernel communication.

Header layout (17 bytes, little-endian, packed):
    magic       uint32      0x41474E54 ("AGNT")
    agent_id    uint32
    opcode      uint8
    payload_size int64

Payload is a UTF-8 JSON string immediately following the header.
"""

import struct
from enum import IntEnum


PROTOCOL_MAGIC = 0x41474E54
HEADER_FORMAT = "<IIBq"  # little-endian: uint32, uint32, uint8, int64
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)  # 17


class SyscallOp(IntEnum):
    # ── Core ────────────────────────────────────────────────────────
    SYS_NOOP            = 0x00
    SYS_THINK           = 0x01
    SYS_EXEC            = 0x02
    SYS_READ            = 0x03
    SYS_WRITE           = 0x04

    # ── Agents ──────────────────────────────────────────────────────
    SYS_SPAWN           = 0x10
    SYS_KILL            = 0x11
    SYS_LIST            = 0x12
    SYS_PAUSE           = 0x14
    SYS_RESUME          = 0x15

    # ── IPC ─────────────────────────────────────────────────────────
    SYS_SEND            = 0x20
    SYS_RECV            = 0x21
    SYS_BROADCAST       = 0x22
    SYS_REGISTER        = 0x23

    # ── State ───────────────────────────────────────────────────────
    SYS_STORE           = 0x30
    SYS_FETCH           = 0x31
    SYS_DELETE           = 0x32
    SYS_KEYS            = 0x33

    # ── Permissions ─────────────────────────────────────────────────
    SYS_GET_PERMS       = 0x40
    SYS_SET_PERMS       = 0x41
    SYS_AUTH            = 0x42
    SYS_POLICY_UPDATE   = 0x43

    # ── HTTP ────────────────────────────────────────────────────────
    SYS_HTTP            = 0x50

    # ── Events ──────────────────────────────────────────────────────
    SYS_SUBSCRIBE       = 0x60
    SYS_UNSUBSCRIBE     = 0x61
    SYS_POLL_EVENTS     = 0x62
    SYS_EMIT            = 0x63

    # ── Recording / Replay ──────────────────────────────────────────
    SYS_RECORD_START    = 0x70
    SYS_RECORD_STOP     = 0x71
    SYS_RECORD_STATUS   = 0x72
    SYS_REPLAY_START    = 0x73
    SYS_REPLAY_STATUS   = 0x74

    # ── Audit ───────────────────────────────────────────────────────
    SYS_GET_AUDIT_LOG   = 0x76
    SYS_SET_AUDIT_CONFIG = 0x77

    # ── Async ───────────────────────────────────────────────────────
    SYS_ASYNC_POLL      = 0x80

    # ── Worlds ──────────────────────────────────────────────────────
    SYS_WORLD_CREATE    = 0xA0
    SYS_WORLD_DESTROY   = 0xA1
    SYS_WORLD_LIST      = 0xA2
    SYS_WORLD_JOIN      = 0xA3
    SYS_WORLD_LEAVE     = 0xA4
    SYS_WORLD_EVENT     = 0xA5
    SYS_WORLD_STATE     = 0xA6
    SYS_WORLD_SNAPSHOT  = 0xA7
    SYS_WORLD_RESTORE   = 0xA8

    # ── Tunnels ─────────────────────────────────────────────────────
    SYS_TUNNEL_CONNECT      = 0xB0
    SYS_TUNNEL_DISCONNECT   = 0xB1
    SYS_TUNNEL_STATUS       = 0xB2
    SYS_TUNNEL_LIST_REMOTES = 0xB3
    SYS_TUNNEL_CONFIG       = 0xB4

    # ── Metrics ─────────────────────────────────────────────────────
    SYS_METRICS_SYSTEM      = 0xC0
    SYS_METRICS_AGENT       = 0xC1
    SYS_METRICS_ALL_AGENTS  = 0xC2
    SYS_METRICS_CGROUP      = 0xC3

    # ── LLM / PII / MCP / A2A / Creds / OTel ───────────────────────
    SYS_LLM_CONFIG          = 0xD0
    SYS_PII_SCAN            = 0xD1
    SYS_PII_REDACT          = 0xD2
    SYS_MCP_CALL            = 0xD3
    SYS_MCP_LIST            = 0xD4
    SYS_A2A_SEND            = 0xD5
    SYS_A2A_RECV            = 0xD6
    SYS_POLICY_RECOMMEND    = 0xD7
    SYS_CREDS_GET           = 0xD8
    SYS_OTEL_SPAN           = 0xD9

    # ── Context / Artifacts ──────────────────────────────────────────
    SYS_DOC_CREATE          = 0xE0
    SYS_DOC_READ            = 0xE1
    SYS_DOC_UPDATE          = 0xE2
    SYS_DOC_LIST            = 0xE3
    SYS_DOC_DELETE          = 0xE4
    SYS_CHAIN_CREATE        = 0xE5
    SYS_CHAIN_GET           = 0xE6
    SYS_CHAIN_FORK          = 0xE7
    SYS_CONTEXT_ASSEMBLE    = 0xE8

    # ── Memory Blocks ────────────────────────────────────────────────
    SYS_MEM_CREATE          = 0xE9
    SYS_MEM_READ            = 0xEA
    SYS_MEM_WRITE           = 0xEB
    SYS_MEM_APPEND          = 0xEC
    SYS_MEM_DELETE          = 0xED
    SYS_MEM_LIST            = 0xEE
    SYS_MEM_SHARE           = 0xEF

    # ── Budget ────────────────────────────────────────────────────────
    SYS_SET_BUDGET          = 0xF1
    SYS_GET_BUDGET          = 0xF2
    SYS_SET_PRIORITY        = 0xF3
    SYS_GET_PRIORITY        = 0xF4

    # ── Diagnostics ─────────────────────────────────────────────────
    SYS_LLM_REPORT          = 0xF0
    SYS_HELLO               = 0xFE
    SYS_EXIT                = 0xFF


def pack_message(agent_id: int, opcode: int, payload: str = "") -> bytes:
    """Pack a CLOVE IPC message (header + payload bytes).

    Args:
        agent_id: 32-bit agent identifier.
        opcode:   Syscall opcode (SyscallOp value).
        payload:  JSON string payload (default empty).

    Returns:
        Raw bytes ready to send over the Unix socket.
    """
    payload_bytes = payload.encode("utf-8") if payload else b""
    header = struct.pack(
        HEADER_FORMAT,
        PROTOCOL_MAGIC,
        agent_id,
        opcode,
        len(payload_bytes),
    )
    return header + payload_bytes


def unpack_header(data: bytes) -> tuple[int, int, int, int]:
    """Unpack a 17-byte CLOVE header.

    Args:
        data: At least HEADER_SIZE (17) bytes.

    Returns:
        Tuple of (magic, agent_id, opcode, payload_size).

    Raises:
        ValueError: If the data is too short or magic doesn't match.
    """
    if len(data) < HEADER_SIZE:
        raise ValueError(
            f"Header too short: got {len(data)} bytes, need {HEADER_SIZE}"
        )
    magic, agent_id, opcode, payload_size = struct.unpack(
        HEADER_FORMAT, data[:HEADER_SIZE]
    )
    if magic != PROTOCOL_MAGIC:
        raise ValueError(
            f"Bad magic: 0x{magic:08X} (expected 0x{PROTOCOL_MAGIC:08X})"
        )
    return magic, agent_id, opcode, payload_size
