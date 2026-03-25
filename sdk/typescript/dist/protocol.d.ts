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
export declare const PROTOCOL_MAGIC = 1095192148;
/** Header size in bytes: uint32 + uint32 + uint8 + int64 = 17. */
export declare const HEADER_SIZE = 17;
/**
 * All CLOVE kernel syscall opcodes.
 */
export declare const enum SyscallOp {
    SYS_NOOP = 0,
    SYS_THINK = 1,
    SYS_EXEC = 2,
    SYS_READ = 3,
    SYS_WRITE = 4,
    SYS_SPAWN = 16,
    SYS_KILL = 17,
    SYS_LIST = 18,
    SYS_PAUSE = 20,
    SYS_RESUME = 21,
    SYS_SEND = 32,
    SYS_RECV = 33,
    SYS_BROADCAST = 34,
    SYS_REGISTER = 35,
    SYS_STORE = 48,
    SYS_FETCH = 49,
    SYS_DELETE = 50,
    SYS_KEYS = 51,
    SYS_GET_PERMS = 64,
    SYS_SET_PERMS = 65,
    SYS_AUTH = 66,
    SYS_POLICY_UPDATE = 67,
    SYS_HTTP = 80,
    SYS_SUBSCRIBE = 96,
    SYS_UNSUBSCRIBE = 97,
    SYS_POLL_EVENTS = 98,
    SYS_EMIT = 99,
    SYS_RECORD_START = 112,
    SYS_RECORD_STOP = 113,
    SYS_RECORD_STATUS = 114,
    SYS_REPLAY_START = 115,
    SYS_REPLAY_STATUS = 116,
    SYS_GET_AUDIT_LOG = 118,
    SYS_SET_AUDIT_CONFIG = 119,
    SYS_ASYNC_POLL = 128,
    SYS_WORLD_CREATE = 160,
    SYS_WORLD_DESTROY = 161,
    SYS_WORLD_LIST = 162,
    SYS_WORLD_JOIN = 163,
    SYS_WORLD_LEAVE = 164,
    SYS_WORLD_EVENT = 165,
    SYS_WORLD_STATE = 166,
    SYS_WORLD_SNAPSHOT = 167,
    SYS_WORLD_RESTORE = 168,
    SYS_TUNNEL_CONNECT = 176,
    SYS_TUNNEL_DISCONNECT = 177,
    SYS_TUNNEL_STATUS = 178,
    SYS_TUNNEL_LIST_REMOTES = 179,
    SYS_TUNNEL_CONFIG = 180,
    SYS_METRICS_SYSTEM = 192,
    SYS_METRICS_AGENT = 193,
    SYS_METRICS_ALL_AGENTS = 194,
    SYS_METRICS_CGROUP = 195,
    SYS_LLM_CONFIG = 208,
    SYS_PII_SCAN = 209,
    SYS_PII_REDACT = 210,
    SYS_MCP_CALL = 211,
    SYS_MCP_LIST = 212,
    SYS_A2A_SEND = 213,
    SYS_A2A_RECV = 214,
    SYS_POLICY_RECOMMEND = 215,
    SYS_CREDS_GET = 216,
    SYS_OTEL_SPAN = 217,
    SYS_DOC_CREATE = 224,
    SYS_DOC_READ = 225,
    SYS_DOC_UPDATE = 226,
    SYS_DOC_LIST = 227,
    SYS_DOC_DELETE = 228,
    SYS_CHAIN_CREATE = 229,
    SYS_CHAIN_GET = 230,
    SYS_CHAIN_FORK = 231,
    SYS_CONTEXT_ASSEMBLE = 232,
    SYS_MEM_CREATE = 233,
    SYS_MEM_READ = 234,
    SYS_MEM_WRITE = 235,
    SYS_MEM_APPEND = 236,
    SYS_MEM_DELETE = 237,
    SYS_MEM_LIST = 238,
    SYS_MEM_SHARE = 239,
    SYS_SET_BUDGET = 241,
    SYS_GET_BUDGET = 242,
    SYS_SET_PRIORITY = 243,
    SYS_GET_PRIORITY = 244,
    SYS_LLM_REPORT = 240,
    SYS_HELLO = 254,
    SYS_EXIT = 255
}
/**
 * Pack a CLOVE IPC message (17-byte header + UTF-8 JSON payload).
 *
 * @param agentId  32-bit agent identifier.
 * @param opcode   Syscall opcode (SyscallOp value).
 * @param payload  JSON string payload (default empty).
 * @returns Buffer ready to send over the Unix socket.
 */
export declare function packMessage(agentId: number, opcode: number, payload?: string): Buffer;
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
export declare function unpackHeader(data: Buffer): HeaderFields;
//# sourceMappingURL=protocol.d.ts.map