"""Unit tests for the CLOVE binary IPC protocol."""

from clove_sdk.protocol import (
    HEADER_SIZE,
    PROTOCOL_MAGIC,
    SyscallOp,
    pack_message,
    unpack_header,
)


def test_pack_unpack_roundtrip():
    data = pack_message(42, SyscallOp.SYS_NOOP, "hello")
    magic, agent_id, opcode, payload_size = unpack_header(data[:HEADER_SIZE])
    assert magic == PROTOCOL_MAGIC
    assert agent_id == 42
    assert opcode == SyscallOp.SYS_NOOP
    assert payload_size == 5


def test_empty_payload():
    data = pack_message(1, SyscallOp.SYS_HELLO)
    magic, agent_id, opcode, payload_size = unpack_header(data[:HEADER_SIZE])
    assert payload_size == 0


def test_header_size():
    data = pack_message(0, SyscallOp.SYS_NOOP)
    assert len(data) == HEADER_SIZE


def test_header_size_is_17():
    assert HEADER_SIZE == 17


def test_payload_follows_header():
    payload_str = '{"key":"value"}'
    data = pack_message(7, SyscallOp.SYS_STORE, payload_str)
    assert data[HEADER_SIZE:] == payload_str.encode("utf-8")


def test_magic_validation():
    import struct
    bad = struct.pack("<IIBq", 0xDEADBEEF, 0, 0, 0)
    try:
        unpack_header(bad)
        assert False, "Should have raised ValueError"
    except ValueError:
        pass


def test_all_opcodes_have_unique_values():
    values = [op.value for op in SyscallOp]
    assert len(values) == len(set(values))


def test_unicode_payload():
    data = pack_message(1, SyscallOp.SYS_THINK, '{"prompt":"cafe\u0301"}')
    _, _, _, payload_size = unpack_header(data[:HEADER_SIZE])
    raw_payload = data[HEADER_SIZE:]
    assert len(raw_payload) == payload_size
    assert raw_payload.decode("utf-8") == '{"prompt":"cafe\u0301"}'
