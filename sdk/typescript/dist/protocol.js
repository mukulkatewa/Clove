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
export var SyscallOp;
(function (SyscallOp) {
    // ── Core ──────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_NOOP"] = 0] = "SYS_NOOP";
    SyscallOp[SyscallOp["SYS_THINK"] = 1] = "SYS_THINK";
    SyscallOp[SyscallOp["SYS_EXEC"] = 2] = "SYS_EXEC";
    SyscallOp[SyscallOp["SYS_READ"] = 3] = "SYS_READ";
    SyscallOp[SyscallOp["SYS_WRITE"] = 4] = "SYS_WRITE";
    // ── Agents ────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_SPAWN"] = 16] = "SYS_SPAWN";
    SyscallOp[SyscallOp["SYS_KILL"] = 17] = "SYS_KILL";
    SyscallOp[SyscallOp["SYS_LIST"] = 18] = "SYS_LIST";
    SyscallOp[SyscallOp["SYS_PAUSE"] = 20] = "SYS_PAUSE";
    SyscallOp[SyscallOp["SYS_RESUME"] = 21] = "SYS_RESUME";
    // ── IPC ───────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_SEND"] = 32] = "SYS_SEND";
    SyscallOp[SyscallOp["SYS_RECV"] = 33] = "SYS_RECV";
    SyscallOp[SyscallOp["SYS_BROADCAST"] = 34] = "SYS_BROADCAST";
    SyscallOp[SyscallOp["SYS_REGISTER"] = 35] = "SYS_REGISTER";
    // ── State ─────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_STORE"] = 48] = "SYS_STORE";
    SyscallOp[SyscallOp["SYS_FETCH"] = 49] = "SYS_FETCH";
    SyscallOp[SyscallOp["SYS_DELETE"] = 50] = "SYS_DELETE";
    SyscallOp[SyscallOp["SYS_KEYS"] = 51] = "SYS_KEYS";
    // ── Permissions ───────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_GET_PERMS"] = 64] = "SYS_GET_PERMS";
    SyscallOp[SyscallOp["SYS_SET_PERMS"] = 65] = "SYS_SET_PERMS";
    SyscallOp[SyscallOp["SYS_AUTH"] = 66] = "SYS_AUTH";
    SyscallOp[SyscallOp["SYS_POLICY_UPDATE"] = 67] = "SYS_POLICY_UPDATE";
    // ── HTTP ──────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_HTTP"] = 80] = "SYS_HTTP";
    // ── Events ────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_SUBSCRIBE"] = 96] = "SYS_SUBSCRIBE";
    SyscallOp[SyscallOp["SYS_UNSUBSCRIBE"] = 97] = "SYS_UNSUBSCRIBE";
    SyscallOp[SyscallOp["SYS_POLL_EVENTS"] = 98] = "SYS_POLL_EVENTS";
    SyscallOp[SyscallOp["SYS_EMIT"] = 99] = "SYS_EMIT";
    // ── Recording / Replay ────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_RECORD_START"] = 112] = "SYS_RECORD_START";
    SyscallOp[SyscallOp["SYS_RECORD_STOP"] = 113] = "SYS_RECORD_STOP";
    SyscallOp[SyscallOp["SYS_RECORD_STATUS"] = 114] = "SYS_RECORD_STATUS";
    SyscallOp[SyscallOp["SYS_REPLAY_START"] = 115] = "SYS_REPLAY_START";
    SyscallOp[SyscallOp["SYS_REPLAY_STATUS"] = 116] = "SYS_REPLAY_STATUS";
    // ── Audit ─────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_GET_AUDIT_LOG"] = 118] = "SYS_GET_AUDIT_LOG";
    SyscallOp[SyscallOp["SYS_SET_AUDIT_CONFIG"] = 119] = "SYS_SET_AUDIT_CONFIG";
    // ── Async ─────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_ASYNC_POLL"] = 128] = "SYS_ASYNC_POLL";
    // ── Worlds ────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_WORLD_CREATE"] = 160] = "SYS_WORLD_CREATE";
    SyscallOp[SyscallOp["SYS_WORLD_DESTROY"] = 161] = "SYS_WORLD_DESTROY";
    SyscallOp[SyscallOp["SYS_WORLD_LIST"] = 162] = "SYS_WORLD_LIST";
    SyscallOp[SyscallOp["SYS_WORLD_JOIN"] = 163] = "SYS_WORLD_JOIN";
    SyscallOp[SyscallOp["SYS_WORLD_LEAVE"] = 164] = "SYS_WORLD_LEAVE";
    SyscallOp[SyscallOp["SYS_WORLD_EVENT"] = 165] = "SYS_WORLD_EVENT";
    SyscallOp[SyscallOp["SYS_WORLD_STATE"] = 166] = "SYS_WORLD_STATE";
    SyscallOp[SyscallOp["SYS_WORLD_SNAPSHOT"] = 167] = "SYS_WORLD_SNAPSHOT";
    SyscallOp[SyscallOp["SYS_WORLD_RESTORE"] = 168] = "SYS_WORLD_RESTORE";
    // ── Tunnels ───────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_TUNNEL_CONNECT"] = 176] = "SYS_TUNNEL_CONNECT";
    SyscallOp[SyscallOp["SYS_TUNNEL_DISCONNECT"] = 177] = "SYS_TUNNEL_DISCONNECT";
    SyscallOp[SyscallOp["SYS_TUNNEL_STATUS"] = 178] = "SYS_TUNNEL_STATUS";
    SyscallOp[SyscallOp["SYS_TUNNEL_LIST_REMOTES"] = 179] = "SYS_TUNNEL_LIST_REMOTES";
    SyscallOp[SyscallOp["SYS_TUNNEL_CONFIG"] = 180] = "SYS_TUNNEL_CONFIG";
    // ── Metrics ───────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_METRICS_SYSTEM"] = 192] = "SYS_METRICS_SYSTEM";
    SyscallOp[SyscallOp["SYS_METRICS_AGENT"] = 193] = "SYS_METRICS_AGENT";
    SyscallOp[SyscallOp["SYS_METRICS_ALL_AGENTS"] = 194] = "SYS_METRICS_ALL_AGENTS";
    SyscallOp[SyscallOp["SYS_METRICS_CGROUP"] = 195] = "SYS_METRICS_CGROUP";
    // ── LLM / PII / MCP / A2A / Creds / OTel ─────────────────────
    SyscallOp[SyscallOp["SYS_LLM_CONFIG"] = 208] = "SYS_LLM_CONFIG";
    SyscallOp[SyscallOp["SYS_PII_SCAN"] = 209] = "SYS_PII_SCAN";
    SyscallOp[SyscallOp["SYS_PII_REDACT"] = 210] = "SYS_PII_REDACT";
    SyscallOp[SyscallOp["SYS_MCP_CALL"] = 211] = "SYS_MCP_CALL";
    SyscallOp[SyscallOp["SYS_MCP_LIST"] = 212] = "SYS_MCP_LIST";
    SyscallOp[SyscallOp["SYS_A2A_SEND"] = 213] = "SYS_A2A_SEND";
    SyscallOp[SyscallOp["SYS_A2A_RECV"] = 214] = "SYS_A2A_RECV";
    SyscallOp[SyscallOp["SYS_POLICY_RECOMMEND"] = 215] = "SYS_POLICY_RECOMMEND";
    SyscallOp[SyscallOp["SYS_CREDS_GET"] = 216] = "SYS_CREDS_GET";
    SyscallOp[SyscallOp["SYS_OTEL_SPAN"] = 217] = "SYS_OTEL_SPAN";
    // ── Context / Artifacts ───────────────────────────────────────
    SyscallOp[SyscallOp["SYS_DOC_CREATE"] = 224] = "SYS_DOC_CREATE";
    SyscallOp[SyscallOp["SYS_DOC_READ"] = 225] = "SYS_DOC_READ";
    SyscallOp[SyscallOp["SYS_DOC_UPDATE"] = 226] = "SYS_DOC_UPDATE";
    SyscallOp[SyscallOp["SYS_DOC_LIST"] = 227] = "SYS_DOC_LIST";
    SyscallOp[SyscallOp["SYS_DOC_DELETE"] = 228] = "SYS_DOC_DELETE";
    SyscallOp[SyscallOp["SYS_CHAIN_CREATE"] = 229] = "SYS_CHAIN_CREATE";
    SyscallOp[SyscallOp["SYS_CHAIN_GET"] = 230] = "SYS_CHAIN_GET";
    SyscallOp[SyscallOp["SYS_CHAIN_FORK"] = 231] = "SYS_CHAIN_FORK";
    SyscallOp[SyscallOp["SYS_CONTEXT_ASSEMBLE"] = 232] = "SYS_CONTEXT_ASSEMBLE";
    // ── Memory Blocks ────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_MEM_CREATE"] = 233] = "SYS_MEM_CREATE";
    SyscallOp[SyscallOp["SYS_MEM_READ"] = 234] = "SYS_MEM_READ";
    SyscallOp[SyscallOp["SYS_MEM_WRITE"] = 235] = "SYS_MEM_WRITE";
    SyscallOp[SyscallOp["SYS_MEM_APPEND"] = 236] = "SYS_MEM_APPEND";
    SyscallOp[SyscallOp["SYS_MEM_DELETE"] = 237] = "SYS_MEM_DELETE";
    SyscallOp[SyscallOp["SYS_MEM_LIST"] = 238] = "SYS_MEM_LIST";
    SyscallOp[SyscallOp["SYS_MEM_SHARE"] = 239] = "SYS_MEM_SHARE";
    // ── Budget ────────────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_SET_BUDGET"] = 241] = "SYS_SET_BUDGET";
    SyscallOp[SyscallOp["SYS_GET_BUDGET"] = 242] = "SYS_GET_BUDGET";
    SyscallOp[SyscallOp["SYS_SET_PRIORITY"] = 243] = "SYS_SET_PRIORITY";
    SyscallOp[SyscallOp["SYS_GET_PRIORITY"] = 244] = "SYS_GET_PRIORITY";
    // ── Diagnostics ───────────────────────────────────────────────
    SyscallOp[SyscallOp["SYS_LLM_REPORT"] = 240] = "SYS_LLM_REPORT";
    SyscallOp[SyscallOp["SYS_HELLO"] = 254] = "SYS_HELLO";
    SyscallOp[SyscallOp["SYS_EXIT"] = 255] = "SYS_EXIT";
})(SyscallOp || (SyscallOp = {}));
/**
 * Pack a CLOVE IPC message (17-byte header + UTF-8 JSON payload).
 *
 * @param agentId  32-bit agent identifier.
 * @param opcode   Syscall opcode (SyscallOp value).
 * @param payload  JSON string payload (default empty).
 * @returns Buffer ready to send over the Unix socket.
 */
export function packMessage(agentId, opcode, payload = "") {
    const payloadBytes = payload ? Buffer.from(payload, "utf-8") : Buffer.alloc(0);
    const header = Buffer.alloc(HEADER_SIZE);
    header.writeUInt32LE(PROTOCOL_MAGIC, 0);
    header.writeUInt32LE(agentId, 4);
    header.writeUInt8(opcode, 8);
    header.writeBigInt64LE(BigInt(payloadBytes.length), 9);
    return Buffer.concat([header, payloadBytes]);
}
/**
 * Unpack a 17-byte CLOVE header.
 *
 * @param data  Buffer of at least HEADER_SIZE (17) bytes.
 * @returns Parsed header fields.
 * @throws Error if data is too short or magic doesn't match.
 */
export function unpackHeader(data) {
    if (data.length < HEADER_SIZE) {
        throw new Error(`Header too short: got ${data.length} bytes, need ${HEADER_SIZE}`);
    }
    const magic = data.readUInt32LE(0);
    const agentId = data.readUInt32LE(4);
    const opcode = data.readUInt8(8);
    const payloadSize = Number(data.readBigInt64LE(9));
    if (magic !== PROTOCOL_MAGIC) {
        throw new Error(`Bad magic: 0x${magic.toString(16).toUpperCase().padStart(8, "0")} ` +
            `(expected 0x${PROTOCOL_MAGIC.toString(16).toUpperCase().padStart(8, "0")})`);
    }
    return { magic, agentId, opcode, payloadSize };
}
//# sourceMappingURL=protocol.js.map