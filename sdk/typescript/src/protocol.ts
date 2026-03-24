/**
 * Binary IPC protocol for CLOVE kernel communication.
 *
 * Header layout (17 bytes, little-endian, packed):
 *   magic         uint32    0x41474E54 ("AGNT")
 *   agent_id      uint32
 *   opcode        uint8
 *   payload_size  int64
 *
 * Payload is a UTF-8 JSON string immediately following the header.
 */

/** Protocol magic number — ASCII "AGNT". */
export const PROTOCOL_MAGIC = 0x41474e54;

/** Header size in bytes: uint32 + uint32 + uint8 + int64 = 17. */
export const HEADER_SIZE = 17;

/**
 * All CLOVE kernel syscall opcodes.
 */
export const enum SyscallOp {
  // ── Core ──────────────────────────────────────────────────────
  SYS_NOOP            = 0x00,
  SYS_THINK           = 0x01,
  SYS_EXEC            = 0x02,
  SYS_READ            = 0x03,
  SYS_WRITE           = 0x04,

  // ── Agents ────────────────────────────────────────────────────
  SYS_SPAWN           = 0x10,
  SYS_KILL            = 0x11,
  SYS_LIST            = 0x12,
  SYS_PAUSE           = 0x14,
  SYS_RESUME          = 0x15,

  // ── IPC ───────────────────────────────────────────────────────
  SYS_SEND            = 0x20,
  SYS_RECV            = 0x21,
  SYS_BROADCAST       = 0x22,
  SYS_REGISTER        = 0x23,

  // ── State ─────────────────────────────────────────────────────
  SYS_STORE           = 0x30,
  SYS_FETCH           = 0x31,
  SYS_DELETE          = 0x32,
  SYS_KEYS            = 0x33,

  // ── Permissions ───────────────────────────────────────────────
  SYS_GET_PERMS       = 0x40,
  SYS_SET_PERMS       = 0x41,
  SYS_AUTH            = 0x42,
  SYS_POLICY_UPDATE   = 0x43,

  // ── HTTP ──────────────────────────────────────────────────────
  SYS_HTTP            = 0x50,

  // ── Events ────────────────────────────────────────────────────
  SYS_SUBSCRIBE       = 0x60,
  SYS_UNSUBSCRIBE     = 0x61,
  SYS_POLL_EVENTS     = 0x62,
  SYS_EMIT            = 0x63,

  // ── Recording / Replay ────────────────────────────────────────
  SYS_RECORD_START    = 0x70,
  SYS_RECORD_STOP     = 0x71,
  SYS_RECORD_STATUS   = 0x72,
  SYS_REPLAY_START    = 0x73,
  SYS_REPLAY_STATUS   = 0x74,

  // ── Audit ─────────────────────────────────────────────────────
  SYS_GET_AUDIT_LOG   = 0x76,
  SYS_SET_AUDIT_CONFIG = 0x77,

  // ── Async ─────────────────────────────────────────────────────
  SYS_ASYNC_POLL      = 0x80,

  // ── Worlds ────────────────────────────────────────────────────
  SYS_WORLD_CREATE    = 0xa0,
  SYS_WORLD_DESTROY   = 0xa1,
  SYS_WORLD_LIST      = 0xa2,
  SYS_WORLD_JOIN      = 0xa3,
  SYS_WORLD_LEAVE     = 0xa4,
  SYS_WORLD_EVENT     = 0xa5,
  SYS_WORLD_STATE     = 0xa6,
  SYS_WORLD_SNAPSHOT  = 0xa7,
  SYS_WORLD_RESTORE   = 0xa8,

  // ── Tunnels ───────────────────────────────────────────────────
  SYS_TUNNEL_CONNECT      = 0xb0,
  SYS_TUNNEL_DISCONNECT   = 0xb1,
  SYS_TUNNEL_STATUS       = 0xb2,
  SYS_TUNNEL_LIST_REMOTES = 0xb3,
  SYS_TUNNEL_CONFIG       = 0xb4,

  // ── Metrics ───────────────────────────────────────────────────
  SYS_METRICS_SYSTEM      = 0xc0,
  SYS_METRICS_AGENT       = 0xc1,
  SYS_METRICS_ALL_AGENTS  = 0xc2,
  SYS_METRICS_CGROUP      = 0xc3,

  // ── LLM / PII / MCP / A2A / Creds / OTel ─────────────────────
  SYS_LLM_CONFIG          = 0xd0,
  SYS_PII_SCAN            = 0xd1,
  SYS_PII_REDACT          = 0xd2,
  SYS_MCP_CALL            = 0xd3,
  SYS_MCP_LIST            = 0xd4,
  SYS_A2A_SEND            = 0xd5,
  SYS_A2A_RECV            = 0xd6,
  SYS_POLICY_RECOMMEND    = 0xd7,
  SYS_CREDS_GET           = 0xd8,
  SYS_OTEL_SPAN           = 0xd9,

  // ── Context / Artifacts ───────────────────────────────────────
  SYS_DOC_CREATE          = 0xe0,
  SYS_DOC_READ            = 0xe1,
  SYS_DOC_UPDATE          = 0xe2,
  SYS_DOC_LIST            = 0xe3,
  SYS_DOC_DELETE          = 0xe4,
  SYS_CHAIN_CREATE        = 0xe5,
  SYS_CHAIN_GET           = 0xe6,
  SYS_CHAIN_FORK          = 0xe7,
  SYS_CONTEXT_ASSEMBLE    = 0xe8,

  // ── Memory Blocks ────────────────────────────────────────────
  SYS_MEM_CREATE          = 0xe9,
  SYS_MEM_READ            = 0xea,
  SYS_MEM_WRITE           = 0xeb,
  SYS_MEM_APPEND          = 0xec,
  SYS_MEM_DELETE          = 0xed,
  SYS_MEM_LIST            = 0xee,
  SYS_MEM_SHARE           = 0xef,

  // ── Budget ────────────────────────────────────────────────────
  SYS_SET_BUDGET          = 0xf1,
  SYS_GET_BUDGET          = 0xf2,
  SYS_SET_PRIORITY        = 0xf3,
  SYS_GET_PRIORITY        = 0xf4,

  // ── Diagnostics ───────────────────────────────────────────────
  SYS_LLM_REPORT          = 0xf0,
  SYS_HELLO               = 0xfe,
  SYS_EXIT                = 0xff,
}

/**
 * Pack a CLOVE IPC message (17-byte header + UTF-8 JSON payload).
 *
 * @param agentId  32-bit agent identifier.
 * @param opcode   Syscall opcode (SyscallOp value).
 * @param payload  JSON string payload (default empty).
 * @returns Buffer ready to send over the Unix socket.
 */
export function packMessage(
  agentId: number,
  opcode: number,
  payload: string = "",
): Buffer {
  const payloadBytes = payload ? Buffer.from(payload, "utf-8") : Buffer.alloc(0);
  const header = Buffer.alloc(HEADER_SIZE);

  header.writeUInt32LE(PROTOCOL_MAGIC, 0);
  header.writeUInt32LE(agentId, 4);
  header.writeUInt8(opcode, 8);
  header.writeBigInt64LE(BigInt(payloadBytes.length), 9);

  return Buffer.concat([header, payloadBytes]);
}

/**
 * Parsed CLOVE header fields.
 */
export interface HeaderFields {
  magic: number;
  agentId: number;
  opcode: number;
  payloadSize: number;
}

/**
 * Unpack a 17-byte CLOVE header.
 *
 * @param data  Buffer of at least HEADER_SIZE (17) bytes.
 * @returns Parsed header fields.
 * @throws Error if data is too short or magic doesn't match.
 */
export function unpackHeader(data: Buffer): HeaderFields {
  if (data.length < HEADER_SIZE) {
    throw new Error(
      `Header too short: got ${data.length} bytes, need ${HEADER_SIZE}`,
    );
  }

  const magic = data.readUInt32LE(0);
  const agentId = data.readUInt32LE(4);
  const opcode = data.readUInt8(8);
  const payloadSize = Number(data.readBigInt64LE(9));

  if (magic !== PROTOCOL_MAGIC) {
    throw new Error(
      `Bad magic: 0x${magic.toString(16).toUpperCase().padStart(8, "0")} ` +
        `(expected 0x${PROTOCOL_MAGIC.toString(16).toUpperCase().padStart(8, "0")})`,
    );
  }

  return { magic, agentId, opcode, payloadSize };
}
