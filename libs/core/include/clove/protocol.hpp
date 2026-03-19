#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clove {

// ---------------------------------------------------------------------------
// Wire constants
// ---------------------------------------------------------------------------
constexpr uint32_t PROTOCOL_MAGIC   = 0x41474E54; // "AGNT"
constexpr uint8_t  PROTOCOL_VERSION = 2;
constexpr size_t   MAX_PAYLOAD_SIZE = 1 * 1024 * 1024; // 1 MB

// ---------------------------------------------------------------------------
// Syscall opcodes — 66 total
// ---------------------------------------------------------------------------
enum class SyscallOp : uint8_t {
    // Core
    SYS_NOOP            = 0x00,
    SYS_THINK           = 0x01,
    SYS_EXEC            = 0x02,
    SYS_READ            = 0x03,
    SYS_WRITE           = 0x04,

    // Agent
    SYS_SPAWN           = 0x10,
    SYS_KILL            = 0x11,
    SYS_LIST            = 0x12,
    SYS_PAUSE           = 0x14,
    SYS_RESUME          = 0x15,

    // IPC
    SYS_SEND            = 0x20,
    SYS_RECV            = 0x21,
    SYS_BROADCAST       = 0x22,
    SYS_REGISTER        = 0x23,

    // State
    SYS_STORE           = 0x30,
    SYS_FETCH           = 0x31,
    SYS_DELETE           = 0x32,
    SYS_KEYS            = 0x33,

    // Permissions
    SYS_GET_PERMS       = 0x40,
    SYS_SET_PERMS       = 0x41,
    SYS_AUTH            = 0x42,
    SYS_POLICY_UPDATE   = 0x43,

    // Network
    SYS_HTTP            = 0x50,

    // Events
    SYS_SUBSCRIBE       = 0x60,
    SYS_UNSUBSCRIBE     = 0x61,
    SYS_POLL_EVENTS     = 0x62,
    SYS_EMIT            = 0x63,

    // Replay
    SYS_RECORD_START    = 0x70,
    SYS_RECORD_STOP     = 0x71,
    SYS_RECORD_STATUS   = 0x72,
    SYS_REPLAY_START    = 0x73,
    SYS_REPLAY_STATUS   = 0x74,

    // Audit
    SYS_GET_AUDIT_LOG   = 0x76,
    SYS_SET_AUDIT_CONFIG = 0x77,

    // Async
    SYS_ASYNC_POLL      = 0x80,

    // World
    SYS_WORLD_CREATE    = 0xA0,
    SYS_WORLD_DESTROY   = 0xA1,
    SYS_WORLD_LIST      = 0xA2,
    SYS_WORLD_JOIN      = 0xA3,
    SYS_WORLD_LEAVE     = 0xA4,
    SYS_WORLD_EVENT     = 0xA5,
    SYS_WORLD_STATE     = 0xA6,
    SYS_WORLD_SNAPSHOT  = 0xA7,
    SYS_WORLD_RESTORE   = 0xA8,

    // Tunnel
    SYS_TUNNEL_CONNECT      = 0xB0,
    SYS_TUNNEL_DISCONNECT   = 0xB1,
    SYS_TUNNEL_STATUS       = 0xB2,
    SYS_TUNNEL_LIST_REMOTES = 0xB3,
    SYS_TUNNEL_CONFIG       = 0xB4,

    // Metrics
    SYS_METRICS_SYSTEM      = 0xC0,
    SYS_METRICS_AGENT       = 0xC1,
    SYS_METRICS_ALL_AGENTS  = 0xC2,
    SYS_METRICS_CGROUP      = 0xC3,

    // Inference Gateway
    SYS_LLM_CONFIG          = 0xD0,

    // v2 opcodes
    SYS_PII_SCAN            = 0xD1,
    SYS_PII_REDACT          = 0xD2,
    SYS_MCP_CALL            = 0xD3,
    SYS_MCP_LIST            = 0xD4,
    SYS_A2A_SEND            = 0xD5,
    SYS_A2A_RECV            = 0xD6,
    SYS_POLICY_RECOMMEND    = 0xD7,
    SYS_CREDS_GET           = 0xD8,
    SYS_OTEL_SPAN           = 0xD9,

    // Meta
    SYS_LLM_REPORT          = 0xF0,
    SYS_HELLO               = 0xFE,
    SYS_EXIT                = 0xFF,
};

/// Human-readable name for every opcode.
[[nodiscard]] const char* opcode_to_string(SyscallOp op) noexcept;

// ---------------------------------------------------------------------------
// MessageHeader — 17 bytes, packed, little-endian on the wire
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct MessageHeader {
    uint32_t magic        = PROTOCOL_MAGIC;
    uint32_t agent_id     = 0;
    uint8_t  opcode       = 0;
    uint64_t payload_size = 0;
};
#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 17, "MessageHeader must be exactly 17 bytes");

// ---------------------------------------------------------------------------
// Message — header + variable-length payload
// ---------------------------------------------------------------------------
struct Message {
    MessageHeader          header;
    std::vector<uint8_t>   payload;

    // --- Factories ----------------------------------------------------------

    /// Build a message from components.
    static Message create(uint32_t agent_id, SyscallOp op,
                          std::vector<uint8_t> payload = {}) {
        Message m;
        m.header.magic        = PROTOCOL_MAGIC;
        m.header.agent_id     = agent_id;
        m.header.opcode       = static_cast<uint8_t>(op);
        m.header.payload_size = payload.size();
        m.payload             = std::move(payload);
        return m;
    }

    /// Build a message with a string payload (convenience).
    static Message create(uint32_t agent_id, SyscallOp op,
                          std::string_view text) {
        std::vector<uint8_t> buf(text.begin(), text.end());
        return create(agent_id, op, std::move(buf));
    }

    // --- Accessors ----------------------------------------------------------

    [[nodiscard]] SyscallOp opcode() const noexcept {
        return static_cast<SyscallOp>(header.opcode);
    }

    [[nodiscard]] uint32_t agent_id() const noexcept {
        return header.agent_id;
    }

    /// Return the payload interpreted as a UTF-8 string.
    [[nodiscard]] std::string payload_str() const {
        return {payload.begin(), payload.end()};
    }

    // --- Serialisation ------------------------------------------------------

    /// Serialize the full message (header + payload) into a byte buffer.
    [[nodiscard]] std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf(sizeof(MessageHeader) + payload.size());
        std::memcpy(buf.data(), &header, sizeof(MessageHeader));
        if (!payload.empty()) {
            std::memcpy(buf.data() + sizeof(MessageHeader),
                        payload.data(), payload.size());
        }
        return buf;
    }

    /// Deserialize a message from a raw byte buffer.
    /// Returns std::nullopt on validation failure.
    [[nodiscard]] static std::optional<Message> deserialize(
            const uint8_t* data, size_t len) {
        if (len < sizeof(MessageHeader)) {
            return std::nullopt;
        }

        MessageHeader hdr{};
        std::memcpy(&hdr, data, sizeof(MessageHeader));

        if (hdr.magic != PROTOCOL_MAGIC) {
            return std::nullopt;
        }
        if (hdr.payload_size > MAX_PAYLOAD_SIZE) {
            return std::nullopt;
        }
        if (len < sizeof(MessageHeader) + hdr.payload_size) {
            return std::nullopt;
        }

        Message m;
        m.header = hdr;
        if (hdr.payload_size > 0) {
            m.payload.assign(
                data + sizeof(MessageHeader),
                data + sizeof(MessageHeader) + hdr.payload_size);
        }
        return m;
    }

    /// Overload accepting a vector.
    [[nodiscard]] static std::optional<Message> deserialize(
            const std::vector<uint8_t>& buf) {
        return deserialize(buf.data(), buf.size());
    }
};

} // namespace clove
